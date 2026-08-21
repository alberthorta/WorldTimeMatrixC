// Jitter — raton BLE HID anti-inactividad + servicio de control (NimBLE).
// Ver Jitter.h para el porque. Equivalente funcional de ../gizmo/tablet/main/
// jitter.c, reescrito sobre NimBLE-Arduino (esta base es arduino-esp32 2.0.17).

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include "Jitter.h"
#include "Config.h"
#include "WifiSetup.h"

namespace Jitter {

namespace {

constexpr char DEVICE_NAME[] = "WorldTime Jitter";
constexpr int  RECENTER_LIMIT = 30;   // recentrar si nos alejamos mas de esto (px)

// Parametros de conexion BLE que pedimos al central (macOS) al conectar.
//
// Por que: un jiggler esta mudo casi todo el tiempo (una notificacion cada
// jitterIntervalMs, que por defecto son segundos), pero el enlace BLE despierta
// la radio en CADA connection event, lleve datos o no. Sin pedir nada, macOS
// negocia lo que le conviene a un raton HID (~15ms => ~67 eventos/s), y ese
// airtime se lo roba al A2DP de unos auriculares BT conectados al mismo Mac:
// microcortes de audio. Con slave latency el device se salta eventos cuando no
// tiene nada que enviar: 45ms * (1+30) = ~1.4s efectivos en reposo.
//
// Limites de las Accessory Design Guidelines de Apple, que macOS hace cumplir:
// intervalo >= 15ms y multiplo de 15ms, latency <= 30, timeout <= 6s. Ademas el
// spec exige timeout > 2 * (1+latency) * intervalo => 6s > 2.79s. OK.
constexpr uint16_t CONN_ITVL_MIN = 24;   // 24 * 1.25ms = 30ms
constexpr uint16_t CONN_ITVL_MAX = 36;   // 36 * 1.25ms = 45ms
constexpr uint16_t CONN_LATENCY  = 30;   // maximo que admite Apple
constexpr uint16_t CONN_TIMEOUT  = 600;  // 600 * 10ms = 6s

// Protocolo de comandos (identico a gizmo / BltKeyboardClicker).
enum : uint8_t {
    CMD_CLICK        = 0x10,
    CMD_DOUBLE_CLICK = 0x11,
    CMD_MOVE         = 0x12,
    CMD_SCROLL       = 0x13,
    CMD_JITTER_START = 0x30,   // [int32 LE intervalo][u8 maxStep]
    CMD_JITTER_STOP  = 0x31,
};

// UUIDs del servicio de control (mismos que la app macOS). Solo cmd + status:
// la parte de "config" (brillo/wifi/tz) del tablet Gizmo no aplica aqui.
constexpr char CTRL_SVC_UUID[]   = "6B1D0001-7C9A-4F3E-9B2A-1F4D3C2B1A00";
constexpr char CMD_CHR_UUID[]    = "6B1D0002-7C9A-4F3E-9B2A-1F4D3C2B1A00";
constexpr char STATUS_CHR_UUID[] = "6B1D0003-7C9A-4F3E-9B2A-1F4D3C2B1A00";

// Report map HID de raton: 4 bytes [botones, X, Y, rueda]. Sin Report ID.
const uint8_t MOUSE_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0
};

// --- Estado ----------------------------------------------------------------
NimBLECharacteristic* s_input  = nullptr;   // input report del raton HID
NimBLECharacteristic* s_status = nullptr;   // status characteristic (notify)
volatile bool s_connected = false;
int32_t acc_x = 0, acc_y = 0;               // desplazamiento acumulado (recentrado)

// Config efectiva, con clamps. La fuente de verdad es Config::cfg; estos helpers
// centralizan los limites (intervalo 50ms..10min, paso 1..20px).
uint32_t effInterval() {
    uint32_t iv = Config::cfg.jitterIntervalMs;
    if (iv < 50)     iv = 50;
    if (iv > 600000) iv = 600000;
    return iv;
}
uint8_t effMaxStep() {
    uint8_t s = Config::cfg.jitterMaxStep;
    if (s < 1)  s = 1;
    if (s > 20) s = 20;
    return s;
}

// --- HID -------------------------------------------------------------------
void sendMouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    if (!s_connected || s_input == nullptr) return;
    uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel};
    s_input->setValue(report, sizeof(report));
    s_input->notify();
}

void mouseClick(uint8_t mask) {
    sendMouse(mask, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    sendMouse(0, 0, 0, 0);
}

// --- Status (6 bytes: enabled, int32 LE intervalo, maxStep) ----------------
void buildStatus(uint8_t s[6]) {
    uint32_t iv = effInterval();
    s[0] = Config::cfg.jitterEnabled ? 1 : 0;
    s[1] = (uint8_t)(iv & 0xFF);
    s[2] = (uint8_t)((iv >> 8) & 0xFF);
    s[3] = (uint8_t)((iv >> 16) & 0xFF);
    s[4] = (uint8_t)((iv >> 24) & 0xFF);
    s[5] = effMaxStep();
}

// --- Manejo de comandos del servicio de control ----------------------------
void handleCommand(const uint8_t* d, size_t n) {
    if (n == 0) return;
    // Los opcodes de config (>=0x40, brillo/wifi/... del tablet Gizmo) no
    // aplican en este device: se ignoran silenciosamente.
    if (d[0] >= 0x40) return;

    switch (d[0]) {
    case CMD_CLICK:        mouseClick(n > 1 ? d[1] : 0x01); break;
    case CMD_DOUBLE_CLICK: mouseClick(n > 1 ? d[1] : 0x01);
                           vTaskDelay(pdMS_TO_TICKS(60));
                           mouseClick(n > 1 ? d[1] : 0x01); break;
    case CMD_MOVE:         if (n >= 3) sendMouse(0, (int8_t)d[1], (int8_t)d[2], 0); break;
    case CMD_SCROLL:       if (n >= 2) sendMouse(0, 0, 0, (int8_t)d[1]); break;
    case CMD_JITTER_START: {
        if (n >= 5) {
            uint32_t iv = (uint32_t)d[1] | ((uint32_t)d[2] << 8) |
                          ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 24);
            Config::cfg.jitterIntervalMs = iv;
        }
        if (n >= 6) Config::cfg.jitterMaxStep = d[5];
        Config::cfg.jitterIntervalMs = effInterval();
        Config::cfg.jitterMaxStep    = effMaxStep();
        Config::cfg.jitterEnabled    = true;
        acc_x = acc_y = 0;
        Config::save();
        notifyStatus();
        Serial.printf("[jitter] START int=%u max=%u\n",
                      (unsigned)Config::cfg.jitterIntervalMs, Config::cfg.jitterMaxStep);
        break;
    }
    case CMD_JITTER_STOP:
        Config::cfg.jitterEnabled = false;
        Config::save();
        notifyStatus();
        Serial.println("[jitter] STOP");
        break;
    default:
        break;
    }
}

// --- Callbacks NimBLE ------------------------------------------------------
class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        s_connected = true;
        acc_x = acc_y = 0;
        // Pedimos slave latency alta para no ocupar la radio 2.4GHz en reposo
        // (ver CONN_* arriba). El central manda: macOS puede ignorarlo, y en ese
        // caso simplemente nos quedamos con lo que el negocie.
        server->updateConnParams(desc->conn_handle, CONN_ITVL_MIN, CONN_ITVL_MAX,
                                 CONN_LATENCY, CONN_TIMEOUT);
        Serial.printf("[jitter] host BLE conectado (pedidos itvl=%u-%u lat=%u to=%u)\n",
                      CONN_ITVL_MIN, CONN_ITVL_MAX, CONN_LATENCY, CONN_TIMEOUT);
    }
    void onDisconnect(NimBLEServer*) override {
        s_connected = false;
        Serial.println("[jitter] host BLE desconectado, re-anunciando");
        NimBLEDevice::startAdvertising();
    }
};

class CmdCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        std::string v = c->getValue();
        handleCommand(reinterpret_cast<const uint8_t*>(v.data()), v.size());
    }
};

// El status characteristic sirve el valor cacheado en lectura; lo refrescamos
// aqui por si algo cambio entre notificaciones.
class StatusCb : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic* c) override {
        uint8_t s[6];
        buildStatus(s);
        c->setValue(s, sizeof(s));
    }
};

ServerCb s_serverCb;
CmdCb    s_cmdCb;
StatusCb s_statusCb;

// --- Motor del jitter ------------------------------------------------------
void jitterStep() {
    uint8_t maxStep = effMaxStep();
    float dist = 1.0f + (float)(esp_random() % maxStep);   // 1..maxStep
    float rad;
    if (acc_x * acc_x + acc_y * acc_y > RECENTER_LIMIT * RECENTER_LIMIT) {
        // Nos hemos alejado: apunta de vuelta al centro con algo de dispersion.
        float back = atan2f((float)-acc_y, (float)-acc_x);
        float spread = ((float)(esp_random() % 121) - 60.0f) * (float)M_PI / 180.0f;
        rad = back + spread;
    } else {
        rad = (float)(esp_random() % 36000) / 100.0f * (float)M_PI / 180.0f;
    }
    int8_t dx = (int8_t)lroundf(dist * cosf(rad));
    int8_t dy = (int8_t)lroundf(dist * sinf(rad));
    if (dx == 0 && dy == 0) dx = 1;
    acc_x += dx;
    acc_y += dy;
    sendMouse(0, dx, dy, 0);
}

void jitterTask(void*) {
    // Snapshot para detectar cambios de config que lleguen por la web (o BLE)
    // y reflejarlos a los suscriptores del status characteristic.
    bool     lastEnabled  = Config::cfg.jitterEnabled;
    uint32_t lastInterval = Config::cfg.jitterIntervalMs;
    uint8_t  lastStep      = Config::cfg.jitterMaxStep;
    for (;;) {
        if (Config::cfg.jitterEnabled  != lastEnabled  ||
            Config::cfg.jitterIntervalMs != lastInterval ||
            Config::cfg.jitterMaxStep    != lastStep) {
            if (Config::cfg.jitterEnabled != lastEnabled) acc_x = acc_y = 0;
            lastEnabled  = Config::cfg.jitterEnabled;
            lastInterval = Config::cfg.jitterIntervalMs;
            lastStep      = Config::cfg.jitterMaxStep;
            notifyStatus();
        }
        if (Config::cfg.jitterEnabled && s_connected) jitterStep();
        vTaskDelay(pdMS_TO_TICKS(effInterval()));
    }
}

}  // namespace

// --- API publica -----------------------------------------------------------
void begin() {
    // Coexistencia WiFi+BT del ESP32-S3: el controlador BT time-sharea la radio
    // con la WiFi via modem-sleep. El proyecto arranca con WiFi.setSleep(false)
    // (WIFI_PS_NONE) por latencia HTTP; con eso, esp_bt_controller_enable()
    // ABORTA en coex_enable (boot-loop). Por eso: (1) solo levantamos BLE en modo
    // STA — en AP la SoftAP no puede dormir y el jitter no hace falta durante el
    // setup de WiFi; (2) reactivamos el modem-sleep antes de init.
    if (WifiSetup::currentMode() != WifiSetup::Mode::Sta) {
        Serial.println("[jitter] WiFi no-STA: BLE no inicializado (jitter en pausa)");
        return;
    }
    WiFi.setSleep(true);   // WIFI_PS_MIN_MODEM: imprescindible para el coex BT+WiFi

    NimBLEDevice::init(DEVICE_NAME);
    // Emparejamiento "Just Works" (como un raton normal): bonding + Secure
    // Connections, sin MITM ni passkey — asi macOS empareja sin pedir codigo.
    NimBLEDevice::setSecurityAuth(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&s_serverCb);

    // Raton HID BLE (crea los servicios HID + battery + device info).
    NimBLEHIDDevice* hid = new NimBLEHIDDevice(server);
    hid->manufacturer()->setValue("gizmo");
    hid->pnp(0x02, 0x16C0, 0x05DF, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t*)MOUSE_REPORT_MAP, sizeof(MOUSE_REPORT_MAP));
    s_input = hid->inputReport(0);   // report id 0 (el map no define Report ID)
    hid->startServices();
    hid->setBatteryLevel(100);

    // Servicio de control propio (compatible con la app macOS).
    NimBLEService* ctrl = server->createService(CTRL_SVC_UUID);
    NimBLECharacteristic* cmd = ctrl->createCharacteristic(
        CMD_CHR_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    cmd->setCallbacks(&s_cmdCb);
    s_status = ctrl->createCharacteristic(
        STATUS_CHR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    s_status->setCallbacks(&s_statusCb);
    { uint8_t s[6]; buildStatus(s); s_status->setValue(s, sizeof(s)); }
    ctrl->start();

    // Advertising: aparencia de raton + servicio HID. El servicio de control
    // (128-bit) NO se anuncia por tamaño del payload; la app macOS lo localiza
    // con retrieveConnectedPeripherals una vez macOS lo empareja como raton.
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C2);   // HID Mouse
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->start();

    // Motor en core 0 (junto a WiFi/BLE), fuera del core 1 donde vive el render.
    xTaskCreatePinnedToCore(jitterTask, "jitter", 3072, nullptr, 4, nullptr, 0);

    Serial.printf("[jitter] BLE up (name='%s' enabled=%d int=%u max=%u)\n",
                  DEVICE_NAME, Config::cfg.jitterEnabled,
                  (unsigned)Config::cfg.jitterIntervalMs, Config::cfg.jitterMaxStep);
}

bool hostConnected() { return s_connected; }

void notifyStatus() {
    if (s_status == nullptr) return;
    uint8_t s[6];
    buildStatus(s);
    s_status->setValue(s, sizeof(s));
    if (s_connected) s_status->notify();
}

}  // namespace Jitter
