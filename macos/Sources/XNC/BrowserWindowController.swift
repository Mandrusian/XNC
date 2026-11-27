import AppKit
import XNCCore
import WebKit

private extension NSToolbarItem.Identifier {
    static let tabStrip = NSToolbarItem.Identifier("com.mandrusian.xnc.tabStrip")
    static let newTab = NSToolbarItem.Identifier("com.mandrusian.xnc.newTab")
}

final class BrowserWindowController: NSWindowController, NSWindowDelegate, NSTextFieldDelegate, NSToolbarDelegate, TabViewControllerDelegate {
    private unowned let appDelegate: AppDelegate
    private var tabs: [TabViewController] = []
    private var selectedIndex: Int = 0

    private let chromeEffectView = NSVisualEffectView()
    private let tabBarStack = NSStackView()
    private let tabBarScroll = NSScrollView()
    private var tabStripWidthConstraint: NSLayoutConstraint?

    private let backButton = NSButton()
    private let forwardButton = NSButton()
    private let reloadButton = NSButton()
    private let shieldButton = NSButton()
    private let zapperButton = NSButton()
    private let micaButton = NSButton()
    private let addressPill = NSView()
    private let addressField = NSTextField()
    private let bookmarkButton = NSButton()
    private let privateTabButton = NSButton()
    private let devButton = NSButton()
    private let menuButton = NSButton()
    private let progressBar = NSProgressIndicator()
    private let contentContainer = NSView()
    private var keyMonitor: Any?

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1160, height: 760),
            styleMask: [.titled, .closable, .miniaturizable, .resizable, .fullSizeContentView],
            backing: .buffered,
            defer: false
        )
        window.title = "XNC"
        window.titleVisibility = .hidden
        window.titlebarAppearsTransparent = true
        window.toolbarStyle = .unifiedCompact
        window.isMovableByWindowBackground = false
        window.backgroundColor = .clear
        window.collectionBehavior.insert(.fullScreenPrimary)
        window.minSize = NSSize(width: 680, height: 440)
        window.center()
        super.init(window: window)
        window.delegate = self

        let toolbar = NSToolbar(identifier: "XNCTitlebarToolbar")
        toolbar.delegate = self
        toolbar.displayMode = .iconOnly
        toolbar.showsBaselineSeparator = false
        window.toolbar = toolbar

        keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if event.keyCode == 103, event.window === self?.window {
                self?.window?.toggleFullScreen(nil)
                return nil
            }
            return event
        }

        buildLayout()
        syncSearchPlaceholder()
        addTab(url: nil, isPrivate: false)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func windowWillClose(_ notification: Notification) {
        if let monitor = keyMonitor {
            NSEvent.removeMonitor(monitor)
            keyMonitor = nil
        }
        appDelegate.windowDidClose(self)
    }

    func windowDidResize(_ notification: Notification) {
        updateTabStripWidth()
    }

    // MARK: - NSToolbarDelegate (Puts clickable tabs inside the AppKit titlebar!)

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.tabStrip, .flexibleSpace, .newTab]
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        [.tabStrip, .flexibleSpace, .newTab]
    }

    func toolbar(
        _ toolbar: NSToolbar,
        itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier,
        willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {
        if itemIdentifier == .tabStrip {
            tabBarStack.orientation = .horizontal
            tabBarStack.spacing = 4
            tabBarStack.edgeInsets = NSEdgeInsets(top: 2, left: 2, bottom: 2, right: 2)
            tabBarStack.translatesAutoresizingMaskIntoConstraints = false

            let docView = NSView()
            docView.translatesAutoresizingMaskIntoConstraints = false
            docView.addSubview(tabBarStack)
            NSLayoutConstraint.activate([
                tabBarStack.leadingAnchor.constraint(equalTo: docView.leadingAnchor),
                tabBarStack.trailingAnchor.constraint(equalTo: docView.trailingAnchor),
                tabBarStack.topAnchor.constraint(equalTo: docView.topAnchor),
                tabBarStack.bottomAnchor.constraint(equalTo: docView.bottomAnchor)
            ])

            tabBarScroll.documentView = docView
            tabBarScroll.hasHorizontalScroller = false
            tabBarScroll.hasVerticalScroller = false
            tabBarScroll.drawsBackground = false
            tabBarScroll.translatesAutoresizingMaskIntoConstraints = false

            NSLayoutConstraint.activate([
                docView.topAnchor.constraint(equalTo: tabBarScroll.contentView.topAnchor),
                docView.bottomAnchor.constraint(equalTo: tabBarScroll.contentView.bottomAnchor),
                docView.leadingAnchor.constraint(greaterThanOrEqualTo: tabBarScroll.contentView.leadingAnchor)
            ])

            tabStripWidthConstraint?.isActive = false
            let height = tabBarScroll.heightAnchor.constraint(equalToConstant: 28)
            height.priority = .required
            height.isActive = true

            let width = tabBarScroll.widthAnchor.constraint(equalToConstant: 820)
            width.priority = .defaultHigh
            width.isActive = true
            tabStripWidthConstraint = width
            updateTabStripWidth()

            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.view = tabBarScroll
            return item
        }

        if itemIdentifier == .newTab {
            let btn = makeToolbarButton(symbol: "plus", fallback: "+", tooltip: "New Tab (⌘T)", action: #selector(handleNewTab))
            let item = NSToolbarItem(itemIdentifier: itemIdentifier)
            item.view = btn
            item.label = "New Tab"
            return item
        }
        return nil
    }

    private func updateTabStripWidth() {
        guard let win = window else { return }
        tabStripWidthConstraint?.constant = max(200, win.frame.width - 220)
    }

    // MARK: - Public Tab & Toolbar API

    func addTab(url: URL?, isPrivate: Bool) {
        let tab = TabViewController(
            initialURL: url,
            isPrivate: isPrivate,
            blockerEnabled: appDelegate.blockerEnabled,
            ruleList: appDelegate.contentRuleList,
            store: appDelegate.store
        )
        tab.delegate = self
        tabs.append(tab)
        selectTab(at: tabs.count - 1)
    }

    func closeCurrentTabOrWindow() {
        closeTab(at: selectedIndex)
    }

    func closeTab(at index: Int) {
        guard tabs.indices.contains(index) else { return }
        let removed = tabs.remove(at: index)
        removed.view.removeFromSuperview()
        if tabs.isEmpty {
            window?.close()
            return
        }
        let nextIndex = min(index, tabs.count - 1)
        selectTab(at: nextIndex)
    }

    func selectNextTab() {
        guard tabs.count > 1 else { return }
        selectTab(at: (selectedIndex + 1) % tabs.count)
    }

    func selectPreviousTab() {
        guard tabs.count > 1 else { return }
        selectTab(at: (selectedIndex - 1 + tabs.count) % tabs.count)
    }

    func focusAddressBar() {
        window?.makeFirstResponder(addressField)
        addressField.selectText(nil)
    }

    func reloadOrStopCurrentTab() {
        guard let tab = currentTab else { return }
        if tab.state.isLoading {
            tab.webView.stopLoading()
        } else {
            tab.webView.reload()
        }
    }

    func goBack() { currentTab?.webView.goBack() }
    func goForward() { currentTab?.webView.goForward() }

    func currentHost() -> String {
        guard let urlStr = currentTab?.state.urlString, let url = URL(string: urlStr) else {
            return ""
        }
        return (url.host ?? "").lowercased()
    }

    func toggleBookmark() {
        guard let tab = currentTab, !tab.state.urlString.isEmpty else { return }
        appDelegate.store.toggleBookmark(title: tab.state.title, url: tab.state.urlString)
        syncToolbar()
    }

    func toggleZapper() {
        currentTab?.toggleZapper()
    }

    func openWebInspector() {
        currentTab?.openInspectorWindow()
    }

    func toggleJavaScript() {
        currentTab?.toggleJavaScript()
        syncToolbar()
    }

    func setCustomUserAgent(_ ua: String?) {
        currentTab?.setCustomUserAgent(ua)
    }

    func applyBlockerState(enabled: Bool, ruleList: WKContentRuleList?) {
        for tab in tabs {
            tab.applyBlocker(enabled: enabled, ruleList: ruleList)
        }
        syncToolbar()
    }

    func applyTransparentMica(_ enabled: Bool) {
        for tab in tabs {
            tab.applyTransparentMica(enabled)
        }
        syncToolbar()
    }

    func syncSearchPlaceholder() {
        let eff = appDelegate.effectiveSearchEngineURL
        if eff.isEmpty {
            addressField.placeholderString = "Search DuckDuckGo or enter URL"
        } else if eff == appDelegate.localSearXNG.baseURL {
            let status = appDelegate.localSearXNG.isReady ? "Local" : "Starting…"
            addressField.placeholderString = "Search SearXNG (\(status)) or enter URL"
        } else {
            let host = URLResolver.displayHost(for: URLResolver.homeURL(searchBaseURL: eff))
            addressField.placeholderString = "Search SearXNG (\(host)) or enter URL"
        }
    }

    func refreshNewTabSearchState() {
        for tab in tabs where ContentBlockerRules.isNewTabURL(tab.webView.url) {
            tab.syncNewTabState()
        }
    }

    func configureSearchEngine() {
        let alert = NSAlert()
        alert.messageText = "Custom SearXNG Search Engine"
        alert.informativeText = "Leave blank to use XNC's built-in SearXNG, enter a custom URL, or switch to DuckDuckGo:"
        alert.addButton(withTitle: "Save")
        alert.addButton(withTitle: "Bundled SearXNG")
        alert.addButton(withTitle: "DuckDuckGo")
        alert.addButton(withTitle: "Cancel")

        let input = NSTextField(frame: NSRect(x: 0, y: 0, width: 380, height: 24))
        input.placeholderString = appDelegate.localSearXNG.baseURL.isEmpty ? "http://localhost:8080" : appDelegate.localSearXNG.baseURL
        input.stringValue = appDelegate.store.searchEngineURL == "duckduckgo" ? "" : appDelegate.store.searchEngineURL
        alert.accessoryView = input

        let response = alert.runModal()
        switch response {
        case .alertFirstButtonReturn:
            appDelegate.store.setSearchEngineURL(input.stringValue)
        case .alertSecondButtonReturn:
            appDelegate.store.setSearchEngineURL("")
        case .alertThirdButtonReturn:
            appDelegate.store.setSearchEngineURL("duckduckgo")
        default:
            return
        }

        appDelegate.refreshSearchEngineUI()
    }

    func showJSConsole() {
        guard let tab = currentTab else { return }
        let alert = NSAlert()
        alert.messageText = "XNC JavaScript Console"
        alert.informativeText = "Evaluate JavaScript in the active tab:"
        alert.addButton(withTitle: "Run")
        alert.addButton(withTitle: "Close")

        let stack = NSStackView()
        stack.orientation = .vertical
        stack.spacing = 8
        stack.frame = NSRect(x: 0, y: 0, width: 460, height: 190)

        let inputField = NSTextField(string: "document.title")
        inputField.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        inputField.placeholderString = "Enter JavaScript expression…"

        let outputScroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: 460, height: 140))
        outputScroll.hasVerticalScroller = true
        outputScroll.borderType = .bezelBorder
        let outputView = NSTextView(frame: NSRect(x: 0, y: 0, width: 440, height: 140))
        outputView.isEditable = false
        outputView.font = .monospacedSystemFont(ofSize: 11.5, weight: .regular)
        outputView.string = "// Output appears here"
        outputScroll.documentView = outputView

        stack.addArrangedSubview(inputField)
        stack.addArrangedSubview(outputScroll)
        stack.subviews.forEach { $0.widthAnchor.constraint(equalTo: stack.widthAnchor).isActive = true }
        outputScroll.heightAnchor.constraint(equalToConstant: 140).isActive = true

        alert.accessoryView = stack
        if alert.runModal() == .alertFirstButtonReturn {
            let script = inputField.stringValue
            tab.webView.evaluateJavaScript(script) { res, err in
                DispatchQueue.main.async {
                    let resultAlert = NSAlert()
                    resultAlert.messageText = err == nil ? "Evaluation Result" : "JavaScript Error"
                    resultAlert.informativeText = err?.localizedDescription ?? String(describing: res ?? "undefined")
                    resultAlert.runModal()
                }
            }
        }
    }

    func viewPageSource() {
        guard let tab = currentTab else { return }
        let sourceURL = tab.state.urlString
        tab.webView.evaluateJavaScript("document.documentElement.outerHTML") { [weak self] res, _ in
            DispatchQueue.main.async {
                guard let self, let html = res as? String else { return }
                let escaped = html
                    .replacingOccurrences(of: "&", with: "&amp;")
                    .replacingOccurrences(of: "<", with: "&lt;")
                    .replacingOccurrences(of: ">", with: "&gt;")
                let doc = """
                <!doctype html><html><head><meta charset="utf-8">
                <title>Source: \(sourceURL)</title>
                <style>
                  body { margin: 0; padding: 20px; background: #111318; color: #e2e8f0;
                         font: 12.5px/1.55 ui-monospace, SFMono-Regular, Menlo, monospace; }
                  header { margin-bottom: 12px; color: #94a3b8; font-size: 12px; }
                  pre { white-space: pre-wrap; word-break: break-word; }
                </style></head><body>
                <header>Source of \(sourceURL)</header>
                <pre>\(escaped)</pre>
                </body></html>
                """
                let newTab = TabViewController(
                    initialURL: nil,
                    isPrivate: tab.isPrivate,
                    blockerEnabled: self.appDelegate.blockerEnabled,
                    ruleList: self.appDelegate.contentRuleList,
                    store: self.appDelegate.store
                )
                newTab.delegate = self
                self.tabs.append(newTab)
                self.selectTab(at: self.tabs.count - 1)
                newTab.loadHTMLString(doc, title: "Source — \(URLResolver.displayHost(for: URL(string: sourceURL)))")
            }
        }
    }

    private var currentTab: TabViewController? {
        tabs.indices.contains(selectedIndex) ? tabs[selectedIndex] : nil
    }

    private func selectTab(at index: Int) {
        guard tabs.indices.contains(index) else { return }
        selectedIndex = index
        contentContainer.subviews.forEach { $0.removeFromSuperview() }

        let tabView = tabs[index].view
        tabView.translatesAutoresizingMaskIntoConstraints = false
        contentContainer.addSubview(tabView)
        NSLayoutConstraint.activate([
            tabView.leadingAnchor.constraint(equalTo: contentContainer.leadingAnchor),
            tabView.trailingAnchor.constraint(equalTo: contentContainer.trailingAnchor),
            tabView.topAnchor.constraint(equalTo: contentContainer.topAnchor),
            tabView.bottomAnchor.constraint(equalTo: contentContainer.bottomAnchor)
        ])

        rebuildTabBar()
        syncToolbar()
    }

    // MARK: - Layout

    private func buildLayout() {
        guard let root = window?.contentView else { return }

        chromeEffectView.material = .underWindowBackground
        chromeEffectView.blendingMode = .behindWindow
        chromeEffectView.state = .followsWindowActiveState
        chromeEffectView.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(chromeEffectView)

        NSLayoutConstraint.activate([
            chromeEffectView.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            chromeEffectView.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            chromeEffectView.topAnchor.constraint(equalTo: root.topAnchor),
            chromeEffectView.bottomAnchor.constraint(equalTo: root.bottomAnchor)
        ])

        let rootStack = NSStackView()
        rootStack.orientation = .vertical
        rootStack.spacing = 0
        rootStack.translatesAutoresizingMaskIntoConstraints = false
        chromeEffectView.addSubview(rootStack)

        NSLayoutConstraint.activate([
            rootStack.leadingAnchor.constraint(equalTo: chromeEffectView.leadingAnchor),
            rootStack.trailingAnchor.constraint(equalTo: chromeEffectView.trailingAnchor),
            rootStack.topAnchor.constraint(equalTo: chromeEffectView.safeAreaLayoutGuide.topAnchor),
            rootStack.bottomAnchor.constraint(equalTo: chromeEffectView.bottomAnchor)
        ])

        configureButton(backButton, symbol: "chevron.left", fallback: "◀", tooltip: "Back (⌘[)", action: #selector(handleBack))
        configureButton(forwardButton, symbol: "chevron.right", fallback: "▶", tooltip: "Forward (⌘])", action: #selector(handleForward))
        configureButton(reloadButton, symbol: "arrow.clockwise", fallback: "↻", tooltip: "Reload (⌘R)", action: #selector(handleReload))
        configureButton(shieldButton, symbol: "shield.fill", fallback: "uB", tooltip: "uBlock Shield", action: #selector(handleShieldMenu(_:)))
        configureButton(zapperButton, symbol: "bolt.fill", fallback: "Zap", tooltip: "Element Zapper (⌘⇧Z)", action: #selector(handleZapper))
        configureButton(micaButton, symbol: "sparkles", fallback: "Mica", tooltip: "Transparent Mica Websites (⌘⇧M)", action: #selector(handleToggleMica))
        configureButton(bookmarkButton, symbol: "star", fallback: "☆", tooltip: "Bookmark Page (⌘D)", action: #selector(handleBookmark))
        configureButton(privateTabButton, symbol: "eye.slash", fallback: "Priv", tooltip: "New Private Tab (⌘⇧N)", action: #selector(handleNewPrivateTab))
        configureButton(devButton, symbol: "wrench.and.screwdriver", fallback: "Dev", tooltip: "Developer Options", action: #selector(handleDevMenu(_:)))
        configureButton(menuButton, symbol: "line.3.horizontal", fallback: "☰", tooltip: "SearXNG, Bookmarks & History", action: #selector(handleShowMenu(_:)))

        addressPill.wantsLayer = true
        addressPill.layer?.cornerRadius = 8
        addressPill.layer?.backgroundColor = NSColor.labelColor.withAlphaComponent(0.07).cgColor
        addressPill.layer?.borderWidth = 0.5
        addressPill.layer?.borderColor = NSColor.separatorColor.withAlphaComponent(0.35).cgColor
        addressPill.translatesAutoresizingMaskIntoConstraints = false
        addressPill.heightAnchor.constraint(equalToConstant: 28).isActive = true

        addressField.isBordered = false
        addressField.drawsBackground = false
        addressField.focusRingType = .none
        addressField.font = .systemFont(ofSize: 12.5)
        addressField.lineBreakMode = .byTruncatingTail
        addressField.delegate = self
        addressField.translatesAutoresizingMaskIntoConstraints = false
        addressPill.addSubview(addressField)
        NSLayoutConstraint.activate([
            addressField.leadingAnchor.constraint(equalTo: addressPill.leadingAnchor, constant: 10),
            addressField.trailingAnchor.constraint(equalTo: addressPill.trailingAnchor, constant: -10),
            addressField.centerYAnchor.constraint(equalTo: addressPill.centerYAnchor)
        ])
        addressPill.setContentHuggingPriority(.defaultLow, for: .horizontal)
        addressPill.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let toolbar = NSStackView(views: [
            backButton, forwardButton, reloadButton, shieldButton, zapperButton, micaButton,
            addressPill,
            bookmarkButton, privateTabButton, devButton, menuButton
        ])
        toolbar.orientation = .horizontal
        toolbar.spacing = 6
        toolbar.edgeInsets = NSEdgeInsets(top: 4, left: 10, bottom: 7, right: 10)
        toolbar.translatesAutoresizingMaskIntoConstraints = false
        toolbar.heightAnchor.constraint(equalToConstant: 38).isActive = true

        progressBar.style = .bar
        progressBar.isIndeterminate = false
        progressBar.minValue = 0
        progressBar.maxValue = 1
        progressBar.controlSize = .small
        progressBar.isHidden = true
        progressBar.translatesAutoresizingMaskIntoConstraints = false
        progressBar.heightAnchor.constraint(equalToConstant: 2).isActive = true

        let separator = NSBox()
        separator.boxType = .separator

        contentContainer.translatesAutoresizingMaskIntoConstraints = false

        rootStack.addArrangedSubview(toolbar)
        rootStack.addArrangedSubview(progressBar)
        rootStack.addArrangedSubview(separator)
        rootStack.addArrangedSubview(contentContainer)

        toolbar.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        progressBar.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        separator.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        contentContainer.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
    }

    private func makeToolbarButton(symbol: String, fallback: String, tooltip: String, action: Selector) -> NSButton {
        let btn = NSButton()
        configureButton(btn, symbol: symbol, fallback: fallback, tooltip: tooltip, action: action)
        return btn
    }

    private func configureButton(_ btn: NSButton, symbol: String, fallback: String, tooltip: String, action: Selector) {
        btn.target = self
        btn.action = action
        btn.isBordered = false
        btn.bezelStyle = .regularSquare
        btn.wantsLayer = true
        btn.layer?.cornerRadius = 6
        btn.toolTip = tooltip
        if let img = NSImage(systemSymbolName: symbol, accessibilityDescription: tooltip) {
            btn.image = img
            btn.imagePosition = .imageOnly
            btn.symbolConfiguration = NSImage.SymbolConfiguration(pointSize: 13, weight: .medium)
        } else {
            btn.title = fallback
        }
        btn.widthAnchor.constraint(greaterThanOrEqualToConstant: 26).isActive = true
        btn.heightAnchor.constraint(equalToConstant: 26).isActive = true
        btn.setContentHuggingPriority(.required, for: .horizontal)
    }

    private func rebuildTabBar() {
        tabBarStack.arrangedSubviews.forEach {
            tabBarStack.removeArrangedSubview($0)
            $0.removeFromSuperview()
        }

        for (index, tab) in tabs.enumerated() {
            let isSelected = (index == selectedIndex)
            let pill = NSStackView()
            pill.orientation = .horizontal
            pill.spacing = 4
            pill.wantsLayer = true
            pill.layer?.cornerRadius = 7
            pill.edgeInsets = NSEdgeInsets(top: 3, left: 9, bottom: 3, right: 6)
            if isSelected {
                pill.layer?.backgroundColor = NSColor.labelColor.withAlphaComponent(0.13).cgColor
            } else {
                pill.layer?.backgroundColor = NSColor.labelColor.withAlphaComponent(0.04).cgColor
            }

            let prefix = tab.isPrivate ? "Private · " : ""
            let rawTitle = tab.state.title
            let display = prefix + (rawTitle.count > 22 ? String(rawTitle.prefix(21)) + "…" : rawTitle)

            let selectBtn = NSButton(title: display, target: self, action: #selector(handleSelectTab(_:)))
            selectBtn.tag = index
            selectBtn.isBordered = false
            selectBtn.font = .systemFont(ofSize: 11.5, weight: isSelected ? .semibold : .regular)

            let closeBtn = NSButton(title: "×", target: self, action: #selector(handleCloseTab(_:)))
            closeBtn.tag = index
            closeBtn.isBordered = false
            closeBtn.font = .systemFont(ofSize: 12, weight: .medium)

            pill.addArrangedSubview(selectBtn)
            pill.addArrangedSubview(closeBtn)
            tabBarStack.addArrangedSubview(pill)
        }
    }

    private func syncToolbar() {
        guard let tab = currentTab else { return }
        let st = tab.state
        window?.title = st.isPrivate ? "XNC (Private) — \(st.title)" : "XNC — \(st.title)"

        backButton.isEnabled = st.canGoBack
        forwardButton.isEnabled = st.canGoForward

        if let reloadImg = NSImage(systemSymbolName: st.isLoading ? "xmark" : "arrow.clockwise", accessibilityDescription: "Reload") {
            reloadButton.image = reloadImg
        } else {
            reloadButton.title = st.isLoading ? "✕" : "↻"
        }

        let host = currentHost()
        let hostAllowlisted = !host.isEmpty && appDelegate.store.isHostAllowlisted(host)
        let effectiveShield = appDelegate.blockerEnabled && !hostAllowlisted
        if let shieldImg = NSImage(
            systemSymbolName: effectiveShield ? "shield.fill" : "shield.slash",
            accessibilityDescription: "uBlock Shield"
        ) {
            shieldButton.image = shieldImg
        }
        if !appDelegate.blockerEnabled {
            shieldButton.contentTintColor = .secondaryLabelColor
        } else if hostAllowlisted {
            shieldButton.contentTintColor = .systemOrange
        } else {
            shieldButton.contentTintColor = .systemGreen
        }

        zapperButton.contentTintColor = tab.zapperActive ? .systemRed : .labelColor
        zapperButton.layer?.backgroundColor = tab.zapperActive
            ? NSColor.systemRed.withAlphaComponent(0.14).cgColor
            : NSColor.clear.cgColor

        let micaOn = appDelegate.store.transparentWebsites
        micaButton.contentTintColor = micaOn ? .controlAccentColor : .labelColor
        micaButton.layer?.backgroundColor = micaOn
            ? NSColor.controlAccentColor.withAlphaComponent(0.16).cgColor
            : NSColor.clear.cgColor

        let isStarred = appDelegate.store.isBookmarked(url: st.urlString)
        if let starImg = NSImage(systemSymbolName: isStarred ? "star.fill" : "star", accessibilityDescription: "Bookmark") {
            bookmarkButton.image = starImg
        }
        bookmarkButton.contentTintColor = isStarred ? .systemYellow : .labelColor

        if window?.firstResponder != addressField.currentEditor() {
            addressField.stringValue = st.urlString
        }

        progressBar.isHidden = !st.isLoading
        progressBar.doubleValue = st.estimatedProgress
    }

    // MARK: - Actions

    @objc private func handleNewTab() {
        addTab(url: nil, isPrivate: false)
    }
    @objc private func handleNewPrivateTab() {
        addTab(url: nil, isPrivate: true)
    }
    @objc private func handleBack() { goBack() }
    @objc private func handleForward() { goForward() }
    @objc private func handleReload() { reloadOrStopCurrentTab() }
    @objc private func handleZapper() { toggleZapper() }
    @objc private func handleToggleMica() { appDelegate.toggleTransparentMicaAction(nil) }
    @objc private func handleBookmark() { toggleBookmark() }
    @objc private func handleSelectTab(_ sender: NSButton) { selectTab(at: sender.tag) }
    @objc private func handleCloseTab(_ sender: NSButton) { closeTab(at: sender.tag) }

    @objc private func handleShieldMenu(_ sender: NSButton) {
        let menu = NSMenu()
        let host = currentHost()
        let isPausedOnSite = !host.isEmpty && appDelegate.store.isHostAllowlisted(host)
        let status = appDelegate.blockerEnabled
            ? (isPausedOnSite ? "uBlock Shield · Paused on \(host)" : "uBlock Shield · Active (\(ContentBlockerRules.blockedDomains.count) domains)")
            : "uBlock Shield · Globally Disabled"
        let hdr = NSMenuItem(title: status, action: nil, keyEquivalent: "")
        hdr.isEnabled = false
        menu.addItem(hdr)
        menu.addItem(.separator())

        if !host.isEmpty {
            let siteTitle = isPausedOnSite ? "Resume Shield on \(host)" : "Pause Shield on \(host)"
            let siteItem = NSMenuItem(title: siteTitle, action: #selector(handleToggleSiteAllowlist), keyEquivalent: "")
            siteItem.target = self
            menu.addItem(siteItem)
        }

        let globalItem = NSMenuItem(
            title: appDelegate.blockerEnabled ? "Disable Global Shield (⌘⇧B)" : "Enable Global Shield (⌘⇧B)",
            action: #selector(handleToggleGlobalShield),
            keyEquivalent: ""
        )
        globalItem.target = self
        menu.addItem(globalItem)

        menu.addItem(.separator())
        let zapItem = NSMenuItem(title: "Element Zapper & Picker (⌘⇧Z)", action: #selector(handleZapper), keyEquivalent: "")
        zapItem.target = self
        menu.addItem(zapItem)

        if !host.isEmpty {
            let zapped = appDelegate.store.customSelectors(for: host)
            if !zapped.isEmpty {
                let clearItem = NSMenuItem(
                    title: "Clear \(zapped.count) Zapped Rule(s) on \(host)",
                    action: #selector(handleClearZappedRules),
                    keyEquivalent: ""
                )
                clearItem.target = self
                menu.addItem(clearItem)
            }
        }

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 4), in: sender)
    }

    @objc private func handleToggleSiteAllowlist() {
        appDelegate.toggleCurrentHostAllowlistAction(nil)
        currentTab?.injectCosmeticAndMica()
        syncToolbar()
    }

    @objc private func handleToggleGlobalShield() {
        appDelegate.toggleBlockerAction(nil)
    }

    @objc private func handleClearZappedRules() {
        let host = currentHost()
        guard !host.isEmpty else { return }
        appDelegate.store.clearCustomSelectors(for: host)
        currentTab?.webView.reload()
    }

    @objc private func handleDevMenu(_ sender: NSButton) {
        let menu = NSMenu()
        let hdr = NSMenuItem(title: "Developer Tools", action: nil, keyEquivalent: "")
        hdr.isEnabled = false
        menu.addItem(hdr)

        let insp = NSMenuItem(title: "Open Web Inspector (⌥⌘I)", action: #selector(handleOpenInspector), keyEquivalent: "")
        insp.target = self
        menu.addItem(insp)

        let console = NSMenuItem(title: "JavaScript Console… (⌥⌘J)", action: #selector(handleJSConsole), keyEquivalent: "")
        console.target = self
        menu.addItem(console)

        let source = NSMenuItem(title: "View Page Source (⌥⌘U)", action: #selector(handleViewSource), keyEquivalent: "")
        source.target = self
        menu.addItem(source)

        menu.addItem(.separator())
        let zap = NSMenuItem(title: "Element Zapper (⌘⇧Z)", action: #selector(handleZapper), keyEquivalent: "")
        zap.target = self
        menu.addItem(zap)

        let micaTitle = appDelegate.store.transparentWebsites
            ? "Disable Transparent Mica Websites (⌘⇧M)"
            : "Enable Transparent Mica Websites (⌘⇧M)"
        let mica = NSMenuItem(title: micaTitle, action: #selector(handleToggleMica), keyEquivalent: "")
        mica.target = self
        menu.addItem(mica)

        let jsLabel = (currentTab?.jsEnabled ?? true) ? "Disable JavaScript on Tab" : "Enable JavaScript on Tab"
        let jsItem = NSMenuItem(title: jsLabel, action: #selector(handleToggleJS), keyEquivalent: "")
        jsItem.target = self
        menu.addItem(jsItem)

        let uaItem = NSMenuItem(title: "User-Agent", action: nil, keyEquivalent: "")
        let uaMenu = NSMenu()
        let presets: [(String, String?)] = [
            ("Default (WebKit)", nil),
            ("Safari (macOS)", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"),
            ("Firefox (macOS)", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:132.0) Gecko/20100101 Firefox/132.0"),
            ("Mobile Safari (iPhone)", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1")
        ]
        for (title, ua) in presets {
            let item = NSMenuItem(title: title, action: #selector(handleSetUA(_:)), keyEquivalent: "")
            item.target = self
            item.representedObject = ua
            uaMenu.addItem(item)
        }
        uaItem.submenu = uaMenu
        menu.addItem(uaItem)

        menu.addItem(.separator())
        let clearData = NSMenuItem(title: "Clear Cookies & Cache", action: #selector(handleClearSiteData), keyEquivalent: "")
        clearData.target = self
        menu.addItem(clearData)

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 4), in: sender)
    }

    @objc private func handleOpenInspector() { openWebInspector() }
    @objc private func handleJSConsole() { showJSConsole() }
    @objc private func handleViewSource() { viewPageSource() }
    @objc private func handleToggleJS() { toggleJavaScript() }
    @objc private func handleSetUA(_ sender: NSMenuItem) { setCustomUserAgent(sender.representedObject as? String) }
    @objc private func handleClearSiteData() { appDelegate.clearBrowsingDataAction(nil) }

    @objc private func handleShowMenu(_ sender: NSButton) {
        let menu = NSMenu()
        let store = appDelegate.store

        let searxLabel: String
        let cfg = store.searchEngineURL
        if cfg == "duckduckgo" {
            searxLabel = "Search Engine: DuckDuckGo (Set SearXNG…)"
        } else if !cfg.isEmpty {
            searxLabel = "Search Engine: \(cfg)…"
        } else if !appDelegate.localSearXNG.baseURL.isEmpty {
            searxLabel = "Search Engine: Bundled SearXNG…"
        } else {
            searxLabel = "Search Engine: DuckDuckGo (Set SearXNG…)"
        }
        let searxItem = NSMenuItem(title: searxLabel, action: #selector(handleConfigureSearch), keyEquivalent: "")
        searxItem.target = self
        menu.addItem(searxItem)

        let micaItem = NSMenuItem(
            title: store.transparentWebsites ? "Transparent Mica Websites: ON (⌘⇧M)" : "Transparent Mica Websites: OFF (⌘⇧M)",
            action: #selector(handleToggleMica),
            keyEquivalent: ""
        )
        micaItem.target = self
        menu.addItem(micaItem)
        menu.addItem(.separator())

        let bookmarksHeader = NSMenuItem(title: "Bookmarks (\(store.bookmarks.count))", action: nil, keyEquivalent: "")
        bookmarksHeader.isEnabled = false
        menu.addItem(bookmarksHeader)

        if store.bookmarks.isEmpty {
            let empty = NSMenuItem(title: "  No bookmarks yet (⌘D to add)", action: nil, keyEquivalent: "")
            empty.isEnabled = false
            menu.addItem(empty)
        } else {
            for b in store.bookmarks.prefix(20) {
                let item = NSMenuItem(title: "  \(b.title)", action: #selector(handleOpenMenuURL(_:)), keyEquivalent: "")
                item.target = self
                item.representedObject = b.url
                menu.addItem(item)
            }
        }

        menu.addItem(.separator())
        let historyHeader = NSMenuItem(title: "Recent History", action: nil, keyEquivalent: "")
        historyHeader.isEnabled = false
        menu.addItem(historyHeader)

        if store.history.isEmpty {
            let empty = NSMenuItem(title: "  No history yet", action: nil, keyEquivalent: "")
            empty.isEnabled = false
            menu.addItem(empty)
        } else {
            for h in store.history.prefix(15) {
                let item = NSMenuItem(title: "  \(h.title)", action: #selector(handleOpenMenuURL(_:)), keyEquivalent: "")
                item.target = self
                item.representedObject = h.url
                menu.addItem(item)
            }
            menu.addItem(.separator())
            let clearItem = NSMenuItem(title: "Clear History", action: #selector(handleClearHistory), keyEquivalent: "")
            clearItem.target = self
            menu.addItem(clearItem)
        }

        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 4), in: sender)
    }

    @objc private func handleConfigureSearch() { configureSearchEngine() }

    @objc private func handleOpenMenuURL(_ sender: NSMenuItem) {
        guard let urlStr = sender.representedObject as? String, let url = URL(string: urlStr) else { return }
        currentTab?.load(url: url)
    }

    @objc private func handleClearHistory() {
        appDelegate.store.clearHistory()
    }

    // MARK: - NSTextFieldDelegate

    func control(_ control: NSControl, textView: NSTextView, doCommandBy commandSelector: Selector) -> Bool {
        if commandSelector == #selector(NSResponder.insertNewline(_:)) {
            let url = URLResolver.resolve(addressField.stringValue, searchBaseURL: appDelegate.effectiveSearchEngineURL)
            currentTab?.load(url: url)
            window?.makeFirstResponder(currentTab?.webView)
            return true
        }
        return false
    }

    // MARK: - TabViewControllerDelegate

    var effectiveSearchEngineURL: String { appDelegate.effectiveSearchEngineURL }
    var bundledSearchEngineURL: String { appDelegate.localSearXNG.baseURL }
    var isBundledSearchReady: Bool { appDelegate.localSearXNG.isReady }

    func tabDidUpdate(_ tab: TabViewController) {
        rebuildTabBar()
        if tab === currentTab {
            syncToolbar()
        }
    }

    func tab(_ tab: TabViewController, didRequestNewTabWith url: URL, isPrivate: Bool) {
        addTab(url: url, isPrivate: isPrivate)
    }

    func tab(_ tab: TabViewController, didFinishNavigation url: URL, title: String) {
        appDelegate.store.recordHistory(title: title, url: url.absoluteString, isPrivate: tab.isPrivate)
    }

    func tab(_ tab: TabViewController, didAddZapperRuleFor host: String) {
        tab.injectCosmeticAndMica()
    }

    func tab(_ tab: TabViewController, didReceiveHomeAction payload: [String: Any]) {
        guard let type = payload["type"] as? String else { return }
        switch type {
        case "navigate":
            let raw = (payload["input"] as? String) ?? ""
            let url = URLResolver.resolve(raw, searchBaseURL: appDelegate.effectiveSearchEngineURL)
            tab.load(url: url)
        case "setSearxng":
            let raw = (payload["url"] as? String) ?? ""
            appDelegate.store.setSearchEngineURL(raw)
            appDelegate.refreshSearchEngineUI()
            for t in tabs where ContentBlockerRules.isNewTabURL(t.webView.url) {
                t.syncNewTabState()
            }
        case "setMica":
            if let enabled = payload["enabled"] as? Bool, enabled != appDelegate.store.transparentWebsites {
                appDelegate.toggleTransparentMicaAction(nil)
            }
        case "action":
            let name = (payload["name"] as? String) ?? ""
            if name == "newPrivateTab" {
                addTab(url: nil, isPrivate: true)
            } else if name == "zapper" {
                tab.toggleZapper()
            } else if name == "fullscreen" {
                window?.toggleFullScreen(nil)
            } else if name == "clearData" {
                appDelegate.clearBrowsingDataAction(nil)
            }
        default:
            break
        }
    }
}
