// Limites de uso de Codex de una cuenta de ChatGPT (ventana de 5h y semanal)
// leidos de chatgpt.com/backend-api/wham/usage, el mismo endpoint interno que
// usa Codex CLI en /status.
//
// Autenticacion: login por codigo de dispositivo, como `codex login
// --device-auth`. El panel pide un codigo, el usuario lo introduce en
// auth.openai.com/codex/device y el panel recibe su propia sesion OAuth, que
// renueva solo. No se usa la cookie del navegador porque Cloudflare bloquea a
// clientes que no son navegador en chatgpt.com/api/auth/session; wham/usage y
// auth.openai.com si responden. El usuario tiene que activar "inicio de sesion
// con codigo de dispositivo" en Ajustes → Seguridad de ChatGPT.
#pragma once

#include <Arduino.h>

#include "ClaudeStats.h"

namespace OpenAIStats {

enum class Login : uint8_t {
    NONE,           // sin sesion
    WAITING_USER,   // codigo pedido, esperando a que el usuario lo introduzca
    CONNECTED,
    ERROR,          // la sesion se ha perdido (refresh rechazado) o fallo el login
};

// Ventanas en el mismo formato que ClaudeStats para reutilizar computePace y
// el render del modo Claude.
struct Usage {
    bool                     hasData = false;
    ClaudeStats::UsageWindow fiveHour;
    ClaudeStats::UsageWindow weekly;
    long                     fiveWindowSec = 5L * 3600L;
    long                     weeklyWindowSec = 7L * 86400L;
};

struct Status {
    Login    login = Login::NONE;
    String   userCode;          // en WAITING_USER
    time_t   codeExpiresAt = 0; // epoch UTC
    String   email;
    String   plan;              // "plus", "pro"...
    String   error;             // ultimo error (login, refresh o fetch)
    uint32_t lastOkAtMs = 0;
    ClaudeStats::HolaStatus hola = ClaudeStats::HolaStatus::NONE;
    String   holaError;
};

extern Usage usage;   // solo PODs: el render lo lee sin bloqueo

// Copia del estado (con Strings) bajo mutex, para la web.
Status status();
// Ultima respuesta de wham/usage, para depurar desde la web.
String lastRawUsage();

void begin();          // carga credenciales y cache
void taskStart();
void requestRefresh();
void requestLogin();   // empieza (o reinicia) el login por codigo
void logout();         // borra la sesion guardada
// Manda un "hola" minimo a Codex, que abre la ventana de 5h. No bloquea: lo
// hace la task. El resultado queda en status().hola / holaError.
void requestOpenWindow();
bool holaPending();
bool isConfigured();   // hay sesion (aunque el ultimo fetch fallara)

}  // namespace OpenAIStats
