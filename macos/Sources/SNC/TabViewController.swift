import AppKit
import SNCCore
import WebKit

protocol TabViewControllerDelegate: AnyObject {
    func tabDidUpdate(_ tab: TabViewController)
    func tab(_ tab: TabViewController, didRequestNewTabWith url: URL, isPrivate: Bool)
    func tab(_ tab: TabViewController, didFinishNavigation url: URL, title: String)
}

final class TabViewController: NSViewController, WKNavigationDelegate, WKUIDelegate {
    let tabID = UUID()
    let isPrivate: Bool
    let webView: WKWebView
    weak var delegate: TabViewControllerDelegate?

    private(set) var state: TabState
    private var observations: [NSKeyValueObservation] = []

    init(initialURL: URL?, isPrivate: Bool, blockerEnabled: Bool, ruleList: WKContentRuleList?) {
        self.isPrivate = isPrivate
        self.state = TabState(id: tabID, urlString: initialURL?.absoluteString ?? "", isPrivate: isPrivate)

        let config = WKWebViewConfiguration()
        config.websiteDataStore = isPrivate ? .nonPersistent() : .default()
        config.preferences.isElementFullscreenEnabled = true
        if blockerEnabled, let ruleList {
            config.userContentController.add(ruleList)
        }

        self.webView = WKWebView(frame: .zero, configuration: config)
        self.webView.allowsBackForwardNavigationGestures = true
        super.init(nibName: nil, bundle: nil)

        self.webView.navigationDelegate = self
        self.webView.uiDelegate = self
        installObservers()

        if let initialURL {
            load(url: initialURL)
        }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    deinit {
        observations.forEach { $0.invalidate() }
    }

    override func loadView() {
        self.view = webView
    }

    func load(url: URL) {
        webView.load(URLRequest(url: url))
    }

    func applyBlocker(enabled: Bool, ruleList: WKContentRuleList?) {
        let controller = webView.configuration.userContentController
        controller.removeAllContentRuleLists()
        if enabled, let ruleList {
            controller.add(ruleList)
        }
    }

    private func installObservers() {
        observations = [
            webView.observe(\.title, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    let raw = view.title?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
                    self.state.title = raw.isEmpty ? (self.isPrivate ? "Private Tab" : "New Tab") : raw
                    self.delegate?.tabDidUpdate(self)
                }
            },
            webView.observe(\.url, options: [.new]) { [weak self] view, _ in
                DispatchQueue.main.async {
                    guard let self else { return }
                    self.state.urlString = view.url?.absoluteString ?? self.state.urlString
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

    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let url = webView.url else { return }
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
