// Arranque automatico al iniciar sesion.
//
// Portado de ClaudeStats (../ClaudeStats/Sources/ClaudeStats/Core/LaunchAtLogin.swift).
// Usa SMAppService, que registra la app como login item sin necesidad de un
// helper bundle separado (la via antigua, SMLoginItemSetEnabled, si lo exigia).
//
// SMAppService es macOS 13+. Esta app declara macOS 12 como minimo
// (LSMinimumSystemVersion en Info.plist), asi que todo va detras de un guard de
// disponibilidad: en macOS 12 `isAvailable` es false y la UI oculta la opcion,
// en vez de fallar en silencio.
import Foundation
import ServiceManagement

enum LaunchAtLogin {
    /// False en macOS 12, donde SMAppService no existe. La UI usa esto para
    /// decidir si muestra el toggle.
    static var isAvailable: Bool {
        if #available(macOS 13, *) { return true }
        return false
    }

    static var isEnabled: Bool {
        guard #available(macOS 13, *) else { return false }
        return SMAppService.mainApp.status == .enabled
    }

    /// Devuelve false si el registro fallo (p.ej. la app no esta en una
    /// ubicacion que macOS acepte como login item). El caller revierte el
    /// estado visual del toggle.
    @discardableResult
    static func setEnabled(_ enabled: Bool) -> Bool {
        guard #available(macOS 13, *) else { return false }
        do {
            if enabled {
                if SMAppService.mainApp.status != .enabled {
                    try SMAppService.mainApp.register()
                }
            } else {
                if SMAppService.mainApp.status == .enabled {
                    try SMAppService.mainApp.unregister()
                }
            }
            return true
        } catch {
            NSLog("[launch] toggle de arranque automatico fallido: \(error)")
            return false
        }
    }
}
