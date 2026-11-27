// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "SNC",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "SNCCore", targets: ["SNCCore"]),
        .executable(name: "SNC", targets: ["SNC"])
    ],
    targets: [
        .target(
            name: "SNCCore",
            path: "Sources/SNCCore",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "SNC",
            dependencies: ["SNCCore"],
            path: "Sources/SNC",
            swiftSettings: [.swiftLanguageMode(.v5)],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("WebKit")
            ]
        ),
        .testTarget(
            name: "SNCCoreTests",
            dependencies: ["SNCCore"],
            path: "Tests/SNCCoreTests",
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
