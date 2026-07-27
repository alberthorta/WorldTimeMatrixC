// Instalacion de la actualizacion: descarga el .zip de la release, sustituye el
// propio bundle y relanza.
//
// Portado de ClaudeStats (../ClaudeStats/Sources/ClaudeStats/Core/UpdateInstaller.swift).
//
// El truco central: un proceso no puede sobrescribirse a si mismo mientras
// corre, asi que escribimos un script helper que espera a que este PID muera,
// hace el swap del .app y lo vuelve a abrir. Lo lanzamos desprendido y salimos.
//
// Aviso sobre Bluetooth: la app va firmada ad-hoc, asi que macOS identifica el
// permiso de Bluetooth por el cdhash del bundle. Al sustituirlo el cdhash cambia
// y el sistema volvera a pedir permiso de Bluetooth la primera vez tras
// actualizar. Es esperado; no es un fallo de la instalacion.
import AppKit
import Foundation

enum UpdateInstaller {
    enum InstallError: LocalizedError {
        case noZip, downloadFailed(Int), unzipFailed, noAppInZip
        var errorDescription: String? {
            switch self {
            case .noZip:                 return "La release no trae ningun .zip de la app"
            case .downloadFailed(let s): return "Descarga fallida (HTTP \(s))"
            case .unzipFailed:           return "No se pudo descomprimir el archivo"
            case .noAppInZip:            return "El archivo descargado no contiene ningun .app"
            }
        }
    }

    static func installAndRestart(release: LatestRelease) async throws {
        guard let downloadURL = release.downloadURL else { throw InstallError.noZip }

        let cache = try cacheDir()
        let zipURL = cache.appendingPathComponent("WorldTimeJitter-\(release.normalizedVersion).zip")
        let stageDir = cache.appendingPathComponent("staged-\(release.normalizedVersion)", isDirectory: true)

        try? FileManager.default.removeItem(at: stageDir)
        try FileManager.default.createDirectory(at: stageDir, withIntermediateDirectories: true)

        // 1. Descarga
        let (tmp, response) = try await URLSession.shared.download(from: downloadURL)
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw InstallError.downloadFailed((response as? HTTPURLResponse)?.statusCode ?? -1)
        }
        try? FileManager.default.removeItem(at: zipURL)
        try FileManager.default.moveItem(at: tmp, to: zipURL)

        // 2. Descomprimir con ditto (respeta la estructura de un .app; unzip no
        //    siempre conserva symlinks ni permisos del bundle).
        try runProcess("/usr/bin/ditto", ["-x", "-k", zipURL.path, stageDir.path])

        // 3. Localizar el .app dentro de lo descomprimido
        let stagedApp = try findAppBundle(in: stageDir)

        // 4. Script helper que hace el swap cuando salgamos
        let destApp = URL(fileURLWithPath: Bundle.main.bundlePath)
        let pid = ProcessInfo.processInfo.processIdentifier
        let logURL = cache.appendingPathComponent("update.log")
        let helperURL = cache.appendingPathComponent("install.sh")

        let script = """
        #!/bin/bash
        set -e
        exec >> "\(logURL.path)" 2>&1
        echo "[$(date)] esperando a que salga el PID \(pid)"
        while kill -0 \(pid) 2>/dev/null; do sleep 0.2; done
        echo "[$(date)] sustituyendo \(destApp.path)"
        rm -rf "\(destApp.path)"
        cp -R "\(stagedApp.path)" "\(destApp.path)"
        codesign --force --deep --sign - "\(destApp.path)" || true
        xattr -dr com.apple.quarantine "\(destApp.path)" || true
        echo "[$(date)] relanzando"
        open "\(destApp.path)"
        """

        try script.write(to: helperURL, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755],
                                              ofItemAtPath: helperURL.path)

        // 5. Lanzar desprendido y salir
        let task = Process()
        task.launchPath = "/bin/bash"
        task.arguments = [helperURL.path]
        task.standardOutput = FileHandle.nullDevice
        task.standardError = FileHandle.nullDevice
        try task.run()

        await MainActor.run { NSApp.terminate(nil) }
    }

    private static func cacheDir() throws -> URL {
        let base = try FileManager.default.url(for: .cachesDirectory, in: .userDomainMask,
                                               appropriateFor: nil, create: true)
        let dir = base.appendingPathComponent("WorldTimeJitter", isDirectory: true)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    private static func findAppBundle(in dir: URL) throws -> URL {
        let fm = FileManager.default
        if let direct = try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)
            .first(where: { $0.pathExtension == "app" }) {
            return direct
        }
        // Un nivel mas abajo (por si el zip trae una carpeta contenedora)
        if let entries = try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil) {
            for sub in entries where (try? sub.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
                if let nested = try? fm.contentsOfDirectory(at: sub, includingPropertiesForKeys: nil)
                    .first(where: { $0.pathExtension == "app" }) {
                    return nested
                }
            }
        }
        throw InstallError.noAppInZip
    }

    private static func runProcess(_ launch: String, _ args: [String]) throws {
        let p = Process()
        p.launchPath = launch
        p.arguments = args
        try p.run()
        p.waitUntilExit()
        if p.terminationStatus != 0 { throw InstallError.unzipFailed }
    }
}
