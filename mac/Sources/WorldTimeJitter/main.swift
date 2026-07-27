// WorldTimeJitter - app de barra de menu para macOS
// ----------------------------------------------------------------------------
// Controla el JITTER de raton del WorldTime Matrix por Bluetooth. Version
// recortada a solo-jitter (portada de gizmo/mac, sin la ventana de config del
// tablet: brillo/WiFi/TZ/Claude — el WorldTime ya tiene su propia web UI).
//
// Soporta VARIOS dispositivos que exponen el servicio de control (el WorldTime
// Matrix "WorldTime Jitter" y cualquier otro compatible, p.ej. el tablet
// "Gizmo Jitter"): el menu "Dispositivo" permite elegir a cual mandar los
// comandos. La eleccion se recuerda entre sesiones.
//
// El protocolo debe coincidir con el firmware (src/Jitter.cpp).
// ----------------------------------------------------------------------------

import AppKit
import CoreBluetooth

// MARK: - UUIDs (coinciden con el firmware, src/Jitter.cpp)
let kControlService = CBUUID(string: "6B1D0001-7C9A-4F3E-9B2A-1F4D3C2B1A00")
let kCmdChar        = CBUUID(string: "6B1D0002-7C9A-4F3E-9B2A-1F4D3C2B1A00")
let kStatusChar     = CBUUID(string: "6B1D0003-7C9A-4F3E-9B2A-1F4D3C2B1A00")

// MARK: - Protocolo
enum Op: UInt8 {
    case click = 0x10, doubleClick = 0x11, move = 0x12, scroll = 0x13
    case jitterStart = 0x30, jitterStop = 0x31
}

// MARK: - Controlador BLE (multi-dispositivo)
final class BLEController: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!

    // Todos los dispositivos conocidos con el servicio de control
    private var known: [UUID: CBPeripheral] = [:]
    private var names: [UUID: String] = [:]

    // Dispositivo activo (el seleccionado y ya con caracteristica de control)
    private var active: CBPeripheral?
    private var cmdChar: CBCharacteristic?

    private var selectedID: UUID? {
        didSet { UserDefaults.standard.set(selectedID?.uuidString, forKey: "selectedDevice") }
    }
    private var retryTimer: Timer?

    var onConnectionChange: ((Bool) -> Void)?
    /// Config reportada por el dispositivo: (activo, intervaloMs, maxStep px).
    var onStatus: ((Bool, UInt32, UInt8) -> Void)?
    /// La lista de dispositivos o la seleccion han cambiado.
    var onDevicesChanged: (() -> Void)?

    var isConnected: Bool { cmdChar != nil }

    /// Dispositivos conocidos (id, nombre), ordenados por nombre.
    var devices: [(id: UUID, name: String)] {
        known.keys.map { (id: $0, name: names[$0] ?? "Dispositivo") }
                  .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
    }
    var selected: UUID? { selectedID }
    var selectedName: String? { selectedID.flatMap { names[$0] } }

    func start() {
        if let s = UserDefaults.standard.string(forKey: "selectedDevice") {
            selectedID = UUID(uuidString: s)
        }
        central = CBCentralManager(delegate: self, queue: .main)
    }

    /// Revisa el enlace (p.ej. al abrir el menu).
    func ensureConnected() { refresh() }

    /// Elegir dispositivo: desconecta el anterior y conecta el nuevo.
    func select(_ id: UUID) {
        guard selectedID != id else { return }
        if let a = active, a.identifier != id { central.cancelPeripheralConnection(a) }
        active = nil
        cmdChar = nil
        selectedID = id
        notifyChange()
        notifyDevices()
        connectSelected()
    }

    private func send(_ bytes: [UInt8]) {
        guard let p = active, p.state == .connected, let c = cmdChar else {
            cmdChar = nil
            notifyChange()
            connectSelected()
            return
        }
        let type: CBCharacteristicWriteType =
            c.properties.contains(.write) ? .withResponse : .withoutResponse
        p.writeValue(Data(bytes), for: c, type: type)
    }

    func jitterStart(intervalMs: UInt32, maxStep: UInt8) {
        send([Op.jitterStart.rawValue,
              UInt8(intervalMs & 0xFF),
              UInt8((intervalMs >> 8) & 0xFF),
              UInt8((intervalMs >> 16) & 0xFF),
              UInt8((intervalMs >> 24) & 0xFF),
              maxStep])
    }
    func jitterStop() { send([Op.jitterStop.rawValue]) }

    // MARK: Descubrimiento
    private func refresh() {
        guard central?.state == .poweredOn else { return }
        // Los dispositivos emparejados como raton ya estan conectados al sistema.
        for p in central.retrieveConnectedPeripherals(withServices: [kControlService]) {
            register(p, name: p.name)
        }
        if !central.isScanning {
            central.scanForPeripherals(withServices: [kControlService])
        }
        connectSelected()
    }

    private func register(_ p: CBPeripheral, name: String?) {
        let id = p.identifier
        let isNew = known[id] == nil
        known[id] = p
        if let n = name, !n.isEmpty {
            names[id] = n
        } else if names[id] == nil {
            names[id] = p.name ?? "Dispositivo \(id.uuidString.prefix(4))"
        }
        // Si no habia seleccion previa, elige el primero que aparezca.
        if selectedID == nil { selectedID = id }
        if isNew { notifyDevices() }
        connectSelected()
    }

    /// Conecta y prepara el dispositivo seleccionado (si hace falta).
    private func connectSelected() {
        guard let id = selectedID, let p = known[id] else { return }
        if cmdChar != nil, active?.identifier == id { return }  // ya listo
        active = p
        p.delegate = self
        switch p.state {
        case .connected:    p.discoverServices([kControlService])
        case .connecting:   break
        default:            central.connect(p)
        }
    }

    // MARK: CBCentralManagerDelegate
    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        if c.state == .poweredOn {
            refresh()
            retryTimer = Timer.scheduledTimer(withTimeInterval: 3, repeats: true) { [weak self] _ in
                self?.refresh()
            }
        } else {
            cmdChar = nil
            notifyChange()
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let advName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        register(p, name: advName ?? p.name)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        p.discoverServices([kControlService])
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        if p.identifier == selectedID {
            cmdChar = nil
            notifyChange()
            central.connect(p)   // reintentar
        }
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        if p.identifier == active?.identifier {
            cmdChar = nil
            notifyChange()
        }
        // Si sigue siendo el seleccionado, dejar una conexion pendiente.
        if p.identifier == selectedID { central.connect(p) }
    }

    // MARK: CBPeripheralDelegate
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        for s in p.services ?? [] where s.uuid == kControlService {
            p.discoverCharacteristics([kCmdChar, kStatusChar], for: s)
        }
    }

    func peripheral(_ p: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        if invalidatedServices.contains(where: { $0.uuid == kControlService }) {
            if p.identifier == active?.identifier { cmdChar = nil; notifyChange() }
            p.discoverServices([kControlService])
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if error != nil { cmdChar = nil; notifyChange(); connectSelected() }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard p.identifier == selectedID else { return }  // solo controlamos el seleccionado
        // Actualiza el nombre real ahora que estamos conectados.
        if let n = p.name, !n.isEmpty { names[p.identifier] = n; notifyDevices() }
        for ch in s.characteristics ?? [] {
            if ch.uuid == kCmdChar { cmdChar = ch; active = p }
            if ch.uuid == kStatusChar {
                p.setNotifyValue(true, for: ch)
                p.readValue(for: ch)  // leer estado del jitter al conectar
            }
        }
        notifyChange()
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard p.identifier == active?.identifier, let d = ch.value else { return }
        if ch.uuid == kStatusChar, d.count >= 6 {
            let on = d[0] != 0
            let interval = UInt32(d[1]) | (UInt32(d[2]) << 8) | (UInt32(d[3]) << 16) | (UInt32(d[4]) << 24)
            let maxStep = d[5]
            onStatus?(on, interval, maxStep)
        }
    }

    private func notifyChange() {
        let connected = isConnected
        DispatchQueue.main.async { self.onConnectionChange?(connected) }
    }
    private func notifyDevices() {
        DispatchQueue.main.async { self.onDevicesChanged?() }
    }
}

// MARK: - App de barra de menu
final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let ble = BLEController()
    private var statusItem: NSStatusItem!

    private var connected = false
    private var jitterActive = false
    private var intervalMs: UInt32 = 1000
    private var maxStep: UInt8 = 4
    private var checkingForUpdate = false

    private let intervals: [(String, UInt32)] = [
        ("250 ms", 250), ("500 ms", 500), ("1 s", 1000), ("2 s", 2000),
        ("5 s", 5000), ("10 s", 10000), ("30 s", 30000), ("60 s", 60000),
    ]
    private let steps: [UInt8] = [2, 3, 4, 6, 10]

    // Buscar actualizaciones al arrancar. Por defecto ON: el sentido de tener
    // auto-update es no tener que acordarse de mirarlo.
    private var autoCheckUpdates: Bool {
        get { UserDefaults.standard.bool(forKey: "autoCheckUpdates") }
        set { UserDefaults.standard.set(newValue, forKey: "autoCheckUpdates") }
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        UserDefaults.standard.register(defaults: ["autoCheckUpdates": true])
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        updateIcon()

        ble.onConnectionChange = { [weak self] c in self?.connected = c; self?.refresh() }
        ble.onStatus = { [weak self] active, interval, step in
            self?.jitterActive = active
            self?.intervalMs = interval
            self?.maxStep = step
            self?.refresh()
        }
        ble.onDevicesChanged = { [weak self] in self?.refresh() }
        ble.start()
        refresh()

        // Chequeo de updates unos segundos despues del arranque, para no
        // competir con el escaneo BLE inicial.
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self, self.autoCheckUpdates else { return }
            Task { await self.runUpdateCheck(silent: true) }
        }
    }

    private func refresh() { rebuildMenu(); updateIcon() }

    func menuWillOpen(_ menu: NSMenu) { ble.ensureConnected() }

    private func updateIcon() {
        guard let button = statusItem.button else { return }
        let name = connected
            ? (jitterActive ? "cursorarrow.motionlines" : "cursorarrow")
            : "cursorarrow.slash"
        if let img = NSImage(systemSymbolName: name, accessibilityDescription: "WorldTime Jitter") {
            img.isTemplate = true
            button.image = img; button.title = ""
        } else {
            button.title = connected ? (jitterActive ? "〜" : "🖱️") : "⛔️"
        }
    }

    private func intervalLabel(_ ms: UInt32) -> String {
        ms % 1000 == 0 ? "\(ms / 1000) s" : "\(ms) ms"
    }

    private func rebuildMenu() {
        let menu = NSMenu()

        let title = connected ? "● \(ble.selectedName ?? "Conectado")" : "○ Buscando dispositivos…"
        let header = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        header.isEnabled = false
        menu.addItem(header)

        if connected {
            let state = NSMenuItem(
                title: jitterActive ? "Jitter ACTIVO · \(intervalLabel(intervalMs))" : "Jitter detenido",
                action: nil, keyEquivalent: "")
            state.isEnabled = false
            menu.addItem(state)
        }
        menu.addItem(.separator())

        // --- Selector de dispositivo ---
        let devMenu = NSMenu()
        let devs = ble.devices
        if devs.isEmpty {
            let none = NSMenuItem(title: "Ninguno detectado", action: nil, keyEquivalent: "")
            none.isEnabled = false
            devMenu.addItem(none)
        } else {
            for d in devs {
                let it = NSMenuItem(title: d.name, action: #selector(pickDevice(_:)), keyEquivalent: "")
                it.target = self
                it.representedObject = d.id.uuidString
                it.state = (d.id == ble.selected) ? .on : .off
                devMenu.addItem(it)
            }
        }
        let devItem = NSMenuItem(title: "Dispositivo", action: nil, keyEquivalent: "")
        devItem.submenu = devMenu
        menu.addItem(devItem)
        menu.addItem(.separator())

        let toggle = NSMenuItem(title: jitterActive ? "Detener jitter" : "Iniciar jitter",
                                action: #selector(toggleJitter), keyEquivalent: "")
        toggle.target = self
        toggle.isEnabled = connected
        menu.addItem(toggle)
        menu.addItem(.separator())

        // Intervalo
        let intItem = NSMenuItem(title: "Intervalo entre movimientos", action: nil, keyEquivalent: "")
        let intMenu = NSMenu()
        for (idx, (label, ms)) in intervals.enumerated() {
            let it = NSMenuItem(title: label, action: #selector(pickInterval(_:)), keyEquivalent: "")
            it.target = self; it.tag = idx; it.isEnabled = connected
            it.state = (ms == intervalMs) ? .on : .off
            intMenu.addItem(it)
        }
        intMenu.addItem(.separator())
        let custom = NSMenuItem(title: "Personalizado…", action: #selector(pickCustomInterval), keyEquivalent: "")
        custom.target = self; custom.isEnabled = connected
        intMenu.addItem(custom)
        intItem.submenu = intMenu
        intItem.isEnabled = connected
        menu.addItem(intItem)

        // Distancia maxima
        let stepItem = NSMenuItem(title: "Distancia máx. (px)", action: nil, keyEquivalent: "")
        let stepMenu = NSMenu()
        for s in steps {
            let it = NSMenuItem(title: "\(s) px", action: #selector(pickStep(_:)), keyEquivalent: "")
            it.target = self; it.tag = Int(s); it.isEnabled = connected
            it.state = (s == maxStep) ? .on : .off
            stepMenu.addItem(it)
        }
        stepItem.submenu = stepMenu
        stepItem.isEnabled = connected
        menu.addItem(stepItem)
        menu.addItem(.separator())

        // --- Ajustes de la app (arranque automatico + updates) ---
        if LaunchAtLogin.isAvailable {
            let login = NSMenuItem(title: "Abrir al iniciar sesión",
                                   action: #selector(toggleLaunchAtLogin), keyEquivalent: "")
            login.target = self
            login.state = LaunchAtLogin.isEnabled ? .on : .off
            menu.addItem(login)
        }

        let autoUp = NSMenuItem(title: "Buscar actualizaciones al arrancar",
                                action: #selector(toggleAutoCheckUpdates), keyEquivalent: "")
        autoUp.target = self
        autoUp.state = autoCheckUpdates ? .on : .off
        menu.addItem(autoUp)

        let checkNow = NSMenuItem(title: checkingForUpdate ? "Buscando…" : "Buscar actualizaciones ahora",
                                  action: #selector(checkForUpdatesNow), keyEquivalent: "")
        checkNow.target = self
        checkNow.isEnabled = !checkingForUpdate
        menu.addItem(checkNow)

        let version = NSMenuItem(title: "Versión \(UpdateChecker.currentVersion)",
                                 action: nil, keyEquivalent: "")
        version.isEnabled = false
        menu.addItem(version)
        menu.addItem(.separator())

        let salir = NSMenuItem(title: "Salir", action: #selector(quit), keyEquivalent: "q")
        salir.target = self
        menu.addItem(salir)

        menu.delegate = self
        statusItem.menu = menu
    }

    private func applyIfActive() {
        if jitterActive { ble.jitterStart(intervalMs: intervalMs, maxStep: maxStep) }
    }

    // MARK: Acciones
    @objc private func pickDevice(_ sender: NSMenuItem) {
        guard let s = sender.representedObject as? String, let id = UUID(uuidString: s) else { return }
        ble.select(id)
        refresh()
    }

    @objc private func toggleJitter() {
        if jitterActive {
            ble.jitterStop(); jitterActive = false
        } else {
            ble.jitterStart(intervalMs: intervalMs, maxStep: maxStep); jitterActive = true
        }
        refresh()
    }

    @objc private func pickInterval(_ sender: NSMenuItem) {
        intervalMs = intervals[sender.tag].1
        applyIfActive(); refresh()
    }

    @objc private func pickStep(_ sender: NSMenuItem) {
        maxStep = UInt8(sender.tag)
        applyIfActive(); refresh()
    }

    @objc private func pickCustomInterval() {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "Intervalo personalizado"
        alert.informativeText = "Milisegundos entre movimientos (mín. 50):"
        alert.addButton(withTitle: "Aceptar")
        alert.addButton(withTitle: "Cancelar")
        let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 200, height: 24))
        field.stringValue = "\(intervalMs)"
        alert.accessoryView = field
        if alert.runModal() == .alertFirstButtonReturn, let v = UInt32(field.stringValue.trimmingCharacters(in: .whitespaces)) {
            intervalMs = max(50, v)
            applyIfActive(); refresh()
        }
    }

    // MARK: Arranque automatico y actualizaciones
    @objc private func toggleLaunchAtLogin() {
        let target = !LaunchAtLogin.isEnabled
        if !LaunchAtLogin.setEnabled(target) {
            let a = NSAlert()
            a.messageText = "No se pudo cambiar el arranque automático"
            a.informativeText = "macOS rechazó registrar la app como elemento de inicio. "
                + "Suele pasar si la app no está en /Applications; muévela ahí y reinténtalo."
            a.alertStyle = .warning
            NSApp.activate(ignoringOtherApps: true)
            _ = a.runModal()
        }
        refresh()
    }

    @objc private func toggleAutoCheckUpdates() {
        autoCheckUpdates.toggle()
        refresh()
    }

    @objc private func checkForUpdatesNow() {
        Task { await runUpdateCheck(silent: false) }
    }

    /// `silent`: en el chequeo automatico del arranque solo avisamos si hay algo
    /// nuevo. En el manual siempre se responde algo, aunque sea "ya estas al dia".
    @MainActor
    private func runUpdateCheck(silent: Bool) async {
        guard !checkingForUpdate else { return }
        checkingForUpdate = true
        refresh()
        let status = await UpdateChecker.check()
        checkingForUpdate = false
        refresh()

        switch status {
        case .updateAvailable(let release):
            promptForUpdate(release: release)
        case .upToDate(let current, _):
            guard !silent else { return }
            let a = NSAlert()
            a.messageText = "Ya estás en la última versión"
            a.informativeText = "WorldTime Jitter \(current)."
            a.alertStyle = .informational
            NSApp.activate(ignoringOtherApps: true)
            _ = a.runModal()
        case .failed(let msg):
            guard !silent else {
                NSLog("[update] chequeo automatico fallido: \(msg)")
                return
            }
            let a = NSAlert()
            a.messageText = "No se pudo comprobar si hay actualizaciones"
            a.informativeText = msg
            a.alertStyle = .warning
            NSApp.activate(ignoringOtherApps: true)
            _ = a.runModal()
        case .idle, .checking:
            break
        }
    }

    @MainActor
    private func promptForUpdate(release: LatestRelease) {
        NSApp.activate(ignoringOtherApps: true)
        let alert = NSAlert()
        alert.messageText = "WorldTime Jitter \(release.normalizedVersion) disponible"
        alert.informativeText = "Estás en la \(UpdateChecker.currentVersion). "
            + "¿Actualizar ahora? La app se reiniciará sola.\n\n"
            + "Tras actualizar, macOS volverá a pedirte permiso de Bluetooth una vez."
        alert.addButton(withTitle: "Actualizar")
        alert.addButton(withTitle: "Ahora no")
        alert.alertStyle = .informational

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        Task {
            do {
                try await UpdateInstaller.installAndRestart(release: release)
            } catch {
                let err = NSAlert()
                err.messageText = "La actualización falló"
                err.informativeText = error.localizedDescription
                err.alertStyle = .warning
                NSApp.activate(ignoringOtherApps: true)
                _ = err.runModal()
            }
        }
    }

    @objc private func quit() { NSApplication.shared.terminate(nil) }
}

// MARK: - Arranque
let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
