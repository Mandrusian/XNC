import Foundation
import Testing
@testable import XNCCore

@Suite("XNC Core Tests")
struct XNCCoreTests {
    @Test("URL resolution, custom SearXNG, and display host")
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

        #expect(URLResolver.normalizeSearXNGURL("searx.example.com/search") == "https://searx.example.com")
        #expect(URLResolver.homeURL(searchBaseURL: "http://localhost:8080/").absoluteString == "http://localhost:8080/")
        #expect(
            URLResolver.resolve("zen mica", searchBaseURL: "https://searx.example.com").absoluteString ==
            "https://searx.example.com/search?q=zen%20mica"
        )
        #expect(URLResolver.displayHost(for: URL(string: "https://www.apple.com/safari/")) == "apple.com")
    }

    @Test("Content blocker JSON schema, allowlist, cosmetic CSS, and transparent Mica")
    func contentBlockerRulesJSON() {
        let json = ContentBlockerRules.defaultRulesJSON()
        #expect(ContentBlockerRules.isValidRulesJSON(json))
        #expect(json.contains("doubleclick"))
        #expect(!json.contains("(|$)"))

        let withAllowlist = ContentBlockerRules.defaultRulesJSON(excludingAllowlistedHosts: ["example.com"])
        #expect(ContentBlockerRules.isValidRulesJSON(withAllowlist))
        #expect(withAllowlist.contains("ignore-previous-rules"))
        #expect(!ContentBlockerRules.isValidRulesJSON("not json"))
        #expect(!ContentBlockerRules.defaultZapperScript.isEmpty)
        #expect(ContentBlockerRules.transparentMicaScript(enabled: true).contains("__xnc-transparent-mica"))
        #expect(ContentBlockerRules.transparentMicaScript(enabled: false).contains("s.remove()"))
    }

    @Test("BrowserStore persistence, allowlist, SearXNG, Mica, and zapper rules")
    func browserStorePersistenceAndPrivacy() throws {
        let tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("xnc_swift_test_\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: tempDir) }

        let store = BrowserStore(directoryURL: tempDir)
        #expect(store.toggleBookmark(title: "SearXNG", url: "https://searx.example.com/"))
        #expect(store.isBookmarked(url: "https://searx.example.com/"))

        store.recordHistory(title: "Secret", url: "https://secret.example.com", isPrivate: true)
        #expect(store.history.isEmpty)

        store.recordHistory(title: "WebKit", url: "https://webkit.org/", isPrivate: false)
        #expect(store.history.count == 1)

        #expect(store.toggleHostAllowlist("Example.com"))
        #expect(store.isHostAllowlisted("example.com"))
        store.addCustomSelector("#cookie-banner", for: "news.ycombinator.com")
        store.addCustomSelector("#cookie-banner", for: "news.ycombinator.com")
        #expect(store.customSelectors(for: "news.ycombinator.com") == ["#cookie-banner"])

        store.setSearchEngineURL("https://searx.example.com/")
        store.setTransparentWebsites(true)

        let css = ContentBlockerRules.cssForHost("news.ycombinator.com", store: store, shieldEnabled: true)
        #expect(css.contains("adsbygoogle"))
        #expect(css.contains("#cookie-banner { display: none !important; }"))

        let reloaded = BrowserStore(directoryURL: tempDir)
        #expect(reloaded.isBookmarked(url: "https://searx.example.com/"))
        #expect(reloaded.history.first?.title == "WebKit")
        #expect(reloaded.isHostAllowlisted("example.com"))
        #expect(reloaded.customSelectors(for: "news.ycombinator.com") == ["#cookie-banner"])
        #expect(reloaded.searchEngineURL == "https://searx.example.com")
        #expect(reloaded.transparentWebsites == true)

        reloaded.clearCustomSelectors(for: "news.ycombinator.com")
        #expect(reloaded.customSelectors(for: "news.ycombinator.com").isEmpty)
        #expect(!reloaded.toggleHostAllowlist("example.com"))
        #expect(!reloaded.isHostAllowlisted("example.com"))
        #expect(!reloaded.toggleBookmark(title: "SearXNG", url: "https://searx.example.com/"))
        #expect(!reloaded.isBookmarked(url: "https://searx.example.com/"))
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
