import AppKit
import XNCCore
import WebKit

private final class WeakScriptMessageHandler: NSObject, WKScriptMessageHandler {
    weak var target: TabViewController?

    init(_ target: TabViewController) {
        self.target = target
    }

    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        target?.handleScriptMessage(message)
    }
}

protocol TabViewControllerDelegate: AnyObject {
    var effectiveSearchEngineURL: String { get }
    var bundledSearchEngineURL: String { get }
    var isBundledSearchReady: Bool { get }
    func tabDidUpdate(_ tab: TabViewController)
    func tab(_ tab: TabViewController, didRequestNewTabWith url: URL, isPrivate: Bool)
    func tab(_ tab: TabViewController, didFinishNavigation url: URL, title: String)
    func tab(_ tab: TabViewController, didAddZapperRuleFor host: String)
    func tab(_ tab: TabViewController, didReceiveHomeAction payload: [String: Any])
}

final class TabViewController: NSViewController, WKNavigationDelegate, WKUIDelegate {
    let tabID = UUID()
    let isPrivate: Bool
    let webView: WKWebView
    weak var delegate: TabViewControllerDelegate?

    private unowned let store: BrowserStore
    private var blockerEnabled: Bool
    private(set) var jsEnabled = true
    private(set) var zapperActive = false
    private(set) var state: TabState
    private var observations: [NSKeyValueObservation] = []

    init(
        initialURL: URL?,
        isPrivate: Bool,
        blockerEnabled: Bool,
        ruleList: WKContentRuleList?,
        store: BrowserStore
    ) {
        self.isPrivate = isPrivate
        self.blockerEnabled = blockerEnabled
        self.store = store
        self.state = TabState(id: tabID, urlString: initialURL?.absoluteString ?? "", isPrivate: isPrivate)

        let config = WKWebViewConfiguration()
        config.websiteDataStore = isPrivate ? .nonPersistent() : .default()
        config.preferences.isElementFullscreenEnabled = true
        config.preferences.setValue(true, forKey: "developerExtrasEnabled")
        if blockerEnabled, let ruleList {
            config.userContentController.add(ruleList)
        }

        self.webView = WKWebView(frame: .zero, configuration: config)
        self.webView.allowsBackForwardNavigationGestures = true
        self.webView.setValue(!store.transparentWebsites, forKey: "drawsBackground")
        if #available(macOS 13.3, *) {
            self.webView.isInspectable = true
        }
        super.init(nibName: nil, bundle: nil)

        config.userContentController.add(WeakScriptMessageHandler(self), name: "xncZapper")
        config.userContentController.add(WeakScriptMessageHandler(self), name: "xncHome")
        self.webView.navigationDelegate = self
        self.webView.uiDelegate = self
        installObservers()

        if let initialURL {
            load(url: initialURL)
        } else {
            loadNewTab()
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        observations.forEach { $0.invalidate() }
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "xncZapper")
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "xncHome")
    }

    override func loadView() {
        self.view = webView
    }

    func load(url: URL) {
        webView.load(URLRequest(url: url))
    }

    func loadNewTab() {
        if let fileURL = ContentBlockerRules.newTabFileURL(from: Bundle.main) {
            state.title = isPrivate ? "Private Tab" : "New Tab"
            state.urlString = ""
            webView.loadFileURL(fileURL, allowingReadAccessTo: fileURL.deletingLastPathComponent())
            delegate?.tabDidUpdate(self)
        } else {
            load(url: URLResolver.homeURL(searchBaseURL: store.searchEngineURL))
        }
    }

    func loadHTMLString(_ html: String, title: String) {
        state.title = title
        webView.loadHTMLString(html, baseURL: nil)
        delegate?.tabDidUpdate(self)
    }

    func applyBlocker(enabled: Bool, ruleList: WKContentRuleList?) {
        self.blockerEnabled = enabled
        let controller = webView.configuration.userContentController
        controller.removeAllContentRuleLists()
        if enabled, let ruleList {
            controller.add(ruleList)
        }
        injectCosmeticAndMica()
    }

    private func isBundledSearchPage(_ url: URL?) -> Bool {
        guard let url, let bundled = delegate?.bundledSearchEngineURL, !bundled.isEmpty else { return false }
        return url.absoluteString.hasPrefix(bundled + "/search")
    }

    func applyTransparentMica(_ enabled: Bool) {
        webView.setValue(!enabled, forKey: "drawsBackground")
        if ContentBlockerRules.isNewTabURL(webView.url) {
            syncNewTabState()
            return
        }
        if isBundledSearchPage(webView.url) {
            let lit = enabled ? "true" : "false"
            webView.evaluateJavaScript("if (typeof window.__xncApplyMica === 'function') window.__xncApplyMica(\(lit));", completionHandler: nil)
            return
        }
        webView.evaluateJavaScript(ContentBlockerRules.transparentMicaScript(enabled: enabled), completionHandler: nil)
    }

    func syncNewTabState() {
        let enc = JSONEncoder()
        let encodeStr = { (s: String) -> String in
            (try? enc.encode(s)).flatMap { String(data: $0, encoding: .utf8) } ?? "\"\""
        }
        let eff = encodeStr(delegate?.effectiveSearchEngineURL ?? store.searchEngineURL)
        let cfg = encodeStr(store.searchEngineURL)
        let bundled = encodeStr(delegate?.bundledSearchEngineURL ?? "")
        let ready = (delegate?.isBundledSearchReady ?? false) ? "true" : "false"
        let micaLiteral = store.transparentWebsites ? "true" : "false"
        let js = "if (typeof window.__xncApplyState === 'function') window.__xncApplyState({ searxngUrl: \(eff), configuredSearxngUrl: \(cfg), bundledSearxngUrl: \(bundled), searxngReady: \(ready), micaEnabled: \(micaLiteral) });"
        webView.evaluateJavaScript(js, completionHandler: nil)
    }

    func toggleJavaScript() {
        jsEnabled.toggle()
        webView.reload()
    }

    func setCustomUserAgent(_ ua: String?) {
        webView.customUserAgent = ua
        webView.reload()
    }

    func toggleZapper() {
        let script = ContentBlockerRules.loadZapperScript(from: Bundle.main)
        webView.evaluateJavaScript(script) { [weak self] result, _ in
            DispatchQueue.main.async {
                guard let self else { return }
                self.zapperActive = (result as? String) == "on"
                self.delegate?.tabDidUpdate(self)
            }
        }
    }

    func injectCosmeticAndMica() {
        if ContentBlockerRules.isNewTabURL(webView.url) {
            webView.setValue(!store.transparentWebsites, forKey: "drawsBackground")
            syncNewTabState()
            return
        }
        if isBundledSearchPage(webView.url) {
            webView.setValue(!store.transparentWebsites, forKey: "drawsBackground")
            let lit = store.transparentWebsites ? "true" : "false"
            webView.evaluateJavaScript("if (typeof window.__xncApplyMica === 'function') window.__xncApplyMica(\(lit));", completionHandler: nil)
            return
        }
        let host = webView.url?.host ?? ""
        var css = ContentBlockerRules.cssForHost(
            host,
            store: store,
            shieldEnabled: blockerEnabled,
            bundle: Bundle.main
        )
        let effURL = delegate?.effectiveSearchEngineURL ?? store.searchEngineURL
        let searxHost = effURL.isEmpty
            ? ""
            : (URLResolver.homeURL(searchBaseURL: effURL).host ?? "").lowercased()
        if !searxHost.isEmpty && host.lowercased() == searxHost {
            let searxCSS = ContentBlockerRules.loadSearXNGThemeCSS(from: Bundle.main)
            if !searxCSS.isEmpty {
                css += "\n" + searxCSS
            }
        }
        webView.evaluateJavaScript(ContentBlockerRules.injectCSSScript(css), completionHandler: nil)
        webView.setValue(!store.transparentWebsites, forKey: "drawsBackground")
        if store.transparentWebsites {
            webView.evaluateJavaScript(ContentBlockerRules.transparentMicaScript(enabled: true), completionHandler: nil)
        }
    }

    func openInspectorWindow() {
        if #available(macOS 13.3, *) {
            webView.isInspectable = true
        }
        let sel = Selector(("_inspector"))
        if webView.responds(to: sel), let inspector = webView.perform(sel)?.takeUnretainedValue() as? NSObject {
            let showSel = Selector(("show:"))
            if inspector.responds(to: showSel) {
                inspector.perform(showSel, with: nil)
                return
            }
        }
        let alert = NSAlert()
        alert.messageText = "Web Inspector Enabled"
        alert.informativeText = "Right-click anywhere on the page and choose \"Inspect Element\", or attach Safari -> Develop -> XNC."
        alert.runModal()
    }

    func handleScriptMessage(_ message: WKScriptMessage) {
        if message.name == "xncHome" {
            if let dict = message.body as? [String: Any] {
                delegate?.tab(self, didReceiveHomeAction: dict)
            } else if let raw = message.body as? String,
                      let data = raw.data(using: .utf8),
                      let dict = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
                delegate?.tab(self, didReceiveHomeAction: dict)
            }
            return
        }
        guard message.name == "xncZapper",
              let raw = message.body as? String,
              let data = raw.data(using: .utf8),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let host = json["host"] as? String,
              let selector = json["selector"] as? String else {
            return
        }
        store.addCustomSelector(selector, for: host)
        delegate?.tab(self, didAddZapperRuleFor: host)
    }

    private func installObservers() {
        observations = [
            webView.observe(\.title, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if ContentBlockerRules.isNewTabURL(view.url) {
                        self.state.title = self.isPrivate ? "Private Tab" : "New Tab"
                        self.delegate?.tabDidUpdate(self)
                        return
                    }
                    let raw = view.title?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                    self.state.title = raw.isEmpty ? (self.isPrivate ? "Private Tab" : "New Tab") : raw
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.url, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    if ContentBlockerRules.isNewTabURL(view.url) {
                        self.state.urlString = ""
                    } else {
                        self.state.urlString = view.url?.absoluteString ?? self.state.urlString
                    }
                    self.zapperActive = false
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.isLoading, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.state.isLoading = view.isLoading
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.estimatedProgress, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.state.estimatedProgress = view.estimatedProgress
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.canGoBack, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.state.canGoBack = view.canGoBack
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.canGoForward, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.state.canGoForward = view.canGoForward
                    self.delegate?.tabDidUpdate(self)
                }
            }
        ]
    }

    // MARK: - WKNavigationDelegate

    func webView(
        _ webView: WKWebView,
        decidePolicyFor navigationAction: WKNavigationAction,
        preferences: WKWebpagePreferences,
        decisionHandler: @escaping (WKNavigationActionPolicy, WKWebpagePreferences) -> Void
    ) {
        preferences.allowsContentJavaScript = jsEnabled
        decisionHandler(.allow, preferences)
    }

    func webView(_ webView: WKWebView, didCommit navigation: WKNavigation!) {
        injectCosmeticAndMica()
    }

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        injectCosmeticAndMica()
        guard let url = webView.url else { return }
        if ContentBlockerRules.isNewTabURL(url) {
            state.urlString = ""
            state.title = isPrivate ? "Private Tab" : "New Tab"
            delegate?.tabDidUpdate(self)
            return
        }
        let title = webView.title ?? url.absoluteString
        state.urlString = url.absoluteString
        if !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            state.title = title
        }
        delegate?.tab(self, didFinishNavigation: url, title: state.title)
        delegate?.tabDidUpdate(self)
    }

    // MARK: - WKUIDelegate

    func webView(
        _ webView: WKWebView,
        createWebViewWith configuration: WKWebViewConfiguration,
        for navigationAction: WKNavigationAction,
        windowFeatures: WKWindowFeatures
    ) -> WKWebView? {
        if navigationAction.targetFrame == nil, let url = navigationAction.request.url {
            delegate?.tab(self, didRequestNewTabWith: url, isPrivate: isPrivate)
        }
        return nil
    }
}
