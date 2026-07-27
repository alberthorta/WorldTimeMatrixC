// Comprobacion de actualizaciones contra las releases de GitHub.
//
// Portado de ClaudeStats (../ClaudeStats/Sources/ClaudeStats/Core/UpdateChecker.swift).
//
// La app comparte el stream de versiones con el firmware: las releases de
// WorldTimeMatrixC llevan `firmware.bin` (para el ESP32) y `WorldTimeJitter.zip`
// (para esta app), y el tag es el mismo para ambos. Es deliberado: el protocolo
// BLE de control tiene que cuadrar entre firmware y app, asi que versionarlos
// juntos evita combinaciones incompatibles.
//
// CFBundleShortVersionString lo inyecta build.sh desde `git describe`, igual que
// scripts/version.py hace con FW_VERSION en el firmware. Un build local fuera de
// un tag reporta la version del ultimo tag, asi que se vera como "al dia" aunque
// vaya por delante — mismo caveat que en el firmware.
import Foundation

struct LatestRelease {
    let tag: String                  // p.ej. "v0.10.0"
    let normalizedVersion: String    // p.ej. "0.10.0"
    let htmlURL: URL
    let downloadURL: URL?            // primer asset .zip, si lo hay
    let publishedAt: String?
}

enum UpdateStatus {
    case idle
    case checking
    case upToDate(current: String, latest: String)
    case updateAvailable(latest: LatestRelease)
    case failed(String)
}

enum UpdateChecker {
    static let repoSlug = "alberthorta/WorldTimeMatrixC"

    static var currentVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "0.0.0"
    }

    static func check() async -> UpdateStatus {
        do {
            let release = try await fetchLatest()
            if compareVersions(currentVersion, release.normalizedVersion) < 0 {
                return .updateAvailable(latest: release)
            }
            return .upToDate(current: currentVersion, latest: release.normalizedVersion)
        } catch {
            return .failed(error.localizedDescription)
        }
    }

    private static func fetchLatest() async throws -> LatestRelease {
        let url = URL(string: "https://api.github.com/repos/\(repoSlug)/releases/latest")!
        var req = URLRequest(url: url)
        req.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        req.setValue("WorldTimeJitter", forHTTPHeaderField: "User-Agent")
        let (data, response) = try await URLSession.shared.data(for: req)
        guard let http = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        guard (200..<300).contains(http.statusCode) else {
            throw NSError(domain: "GitHub", code: http.statusCode,
                          userInfo: [NSLocalizedDescriptionKey: "HTTP \(http.statusCode)"])
        }
        let decoded = try JSONDecoder().decode(GHRelease.self, from: data)
        let normalized = decoded.tag_name.hasPrefix("v")
            ? String(decoded.tag_name.dropFirst()) : decoded.tag_name
        // Solo el .zip: el firmware.bin de la misma release es para el ESP32.
        let downloadURL = decoded.assets.first { $0.name.hasSuffix(".zip") }
            .flatMap { URL(string: $0.browser_download_url) }
        guard let html = URL(string: decoded.html_url) else { throw URLError(.badURL) }
        return LatestRelease(
            tag: decoded.tag_name,
            normalizedVersion: normalized,
            htmlURL: html,
            downloadURL: downloadURL,
            publishedAt: decoded.published_at
        )
    }

    /// -1 si a < b, 0 si iguales, 1 si a > b. Rellena con ceros la mas corta.
    /// Ignora cualquier sufijo no numerico ("0.10.0-3-gabc" -> 0.10.0) para que
    /// un build intermedio no se compare como basura.
    static func compareVersions(_ a: String, _ b: String) -> Int {
        func parts(_ s: String) -> [Int] {
            s.split(separator: ".").map { comp in
                Int(comp.prefix(while: { $0.isNumber })) ?? 0
            }
        }
        let pa = parts(a), pb = parts(b)
        for i in 0..<max(pa.count, pb.count) {
            let x = i < pa.count ? pa[i] : 0
            let y = i < pb.count ? pb[i] : 0
            if x < y { return -1 }
            if x > y { return 1 }
        }
        return 0
    }

    private struct GHRelease: Decodable {
        let tag_name: String
        let html_url: String
        let published_at: String?
        let assets: [GHAsset]
    }

    private struct GHAsset: Decodable {
        let name: String
        let browser_download_url: String
    }
}
