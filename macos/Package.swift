// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "XNC",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .library(name: "XNCCore", targets: ["XNCCore"]),
        .executable(name: "XNC", targets: ["XNC"])
    ],
    targets: [
        .target(
            name: "XNCCore",
            path: "Sources/XNCCore",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "XNC",
            dependencies: ["XNCCore"],
            path: "Sources/XNC",
            swiftSettings: [.swiftLanguageMode(.v5)],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("WebKit")
            ]
        ),
        .testTarget(
            name: "XNCCoreTests",
            dependencies: ["XNCCore"],
            path: "Tests/XNCCoreTests",
            swiftSettings: [.swiftLanguageMode(.v5)]
        )
    ]
)
