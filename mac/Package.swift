// swift-tools-version:5.7
import PackageDescription

let package = Package(
    name: "WorldTimeJitter",
    platforms: [.macOS(.v12)],
    targets: [
        .executableTarget(
            name: "WorldTimeJitter",
            path: "Sources/WorldTimeJitter"
        )
    ]
)
