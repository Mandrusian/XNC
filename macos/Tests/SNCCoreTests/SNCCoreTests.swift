import Foundation
import Testing
@testable import SNCCore

@Suite("SNC Core Tests")
struct SNCCoreTests {
    @Test("URL resolution and display host")
    func urlResolution() {
        #expect(URLResolver.resolve("").absoluteString == "https://duckduckgo.com/")
        #expect(URLResolver.resolve("  https://webkit.org/ ").absoluteString == "https://webkit.org/")
        #expect(URLResolver.resolve("example.com").absoluteString == "https://example.com")
        #expect(URLResolver.resolve("localhost:3000/api").absoluteString == "http://localhost:3000/api")
        #expect(URLResolver.resolve("127.0.0.1:8080").absoluteString == "http://127.0.0.1:8080")
        #expect(URLResolver.resolve("127.0.0.1.example.com").absoluteString == "https://127.0.0.1.example.com")
        #expect(URLResolver.resolve("about:blank").absoluteString == "about:blank")
        #expect(
            URLResolver.resolve("swift webkit").absoluteString ==
            "https://duckduckgo.com/?q=swift%20webkit"
        )
        #expect(URLResolver.displayHost(for: URL(string: "https://www.apple.com/safari/")) == "apple.com")
    }

    @Test("Content blocker JSON schema")
    func contentBlockerRulesJSON() {
        let json = ContentBlockerRules.defaultRulesJSON()
        #expect(ContentBlockerRules.isValidRulesJSON(json))
        #expect(json.contains("doubleclick"))
        #expect(!json.contains("(|$)"))
        #expect(!ContentBlockerRules.isValidRulesJSON("not json"))
    }

    @Test("BrowserStore bookmark and history persistence")
    func browserStorePersistenceAndPrivacy() throws {
        let tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("snc_swift_test_\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: tempDir) }

        let store = BrowserStore(directoryURL: tempDir)
        #expect(store.toggleBookmark(title: "DuckDuckGo", url: "https://duckduckgo.com/"))
        #expect(store.isBookmarked(url: "https://duckduckgo.com/"))

        store.recordHistory(title: "Secret", url: "https://secret.example.com", isPrivate: true)
        #expect(store.history.isEmpty)

        store.recordHistory(title: "WebKit", url: "https://webkit.org/", isPrivate: false)
        #expect(store.history.count == 1)

        let reloaded = BrowserStore(directoryURL: tempDir)
        #expect(reloaded.isBookmarked(url: "https://duckduckgo.com/"))
        #expect(reloaded.history.first?.title == "WebKit")

        #expect(!reloaded.toggleBookmark(title: "DuckDuckGo", url: "https://duckduckgo.com/"))
        #expect(!reloaded.isBookmarked(url: "https://duckduckgo.com/"))
    }

    @Test("TabState defaults")
    func tabStateDefaults() {
        let normal = TabState()
        #expect(normal.title == "New Tab")
        #expect(!normal.isPrivate)

        let priv = TabState(isPrivate: true)
        #expect(priv.title == "Private Tab")
        #expect(priv.isPrivate)
    }
}
