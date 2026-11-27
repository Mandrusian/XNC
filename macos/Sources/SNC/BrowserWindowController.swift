import AppKit
import SNCCore
import WebKit

final class BrowserWindowController: NSWindowController, NSWindowDelegate, NSTextFieldDelegate, TabViewControllerDelegate {
    private unowned let appDelegate: AppDelegate
    private var tabs: [TabViewController] = []
    private var selectedIndex: Int = 0

    private let tabBarStack = NSStackView()
    private let tabBarScroll = NSScrollView()
    private let backButton = NSButton()
    private let forwardButton = NSButton()
    private let reloadButton = NSButton()
    private let shieldButton = NSButton()
    private let addressField = NSTextField()
    private let bookmarkButton = NSButton()
    private let privateTabButton = NSButton()
    private let menuButton = NSButton()
    private let progressBar = NSProgressIndicator()
    private let contentContainer = NSView()

    init(appDelegate: AppDelegate) {
        self.appDelegate = appDelegate
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1120, height: 740),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "SNC"
        window.minSize = NSSize(width: 600, height: 420)
        window.center()
        super.init(window: window)
        window.delegate = self

        buildLayout()
        addTab(url: URLResolver.defaultHomeURL, isPrivate: false)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func windowWillClose(_ notification: Notification) {
        appDelegate.windowDidClose(self)
    }

    // MARK: - Public Tab & Toolbar API

    func addTab(url: URL?, isPrivate: Bool) {
        let tab = TabViewController(
            initialURL: url,
            isPrivate: isPrivate,
            blockerEnabled: appDelegate.blockerEnabled,
            ruleList: appDelegate.contentRuleList
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

    func goBack() {
        currentTab?.webView.goBack()
    }

    func goForward() {
        currentTab?.webView.goForward()
    }

    func toggleBookmark() {
        guard let tab = currentTab, !tab.state.urlString.isEmpty else { return }
        appDelegate.store.toggleBookmark(title: tab.state.title, url: tab.state.urlString)
        syncToolbar()
    }

    func applyBlockerState(enabled: Bool, ruleList: WKContentRuleList?) {
        for tab in tabs {
            tab.applyBlocker(enabled: enabled, ruleList: ruleList)
        }
        syncToolbar()
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

        let rootStack = NSStackView()
        rootStack.orientation = .vertical
        rootStack.spacing = 0
        rootStack.translatesAutoresizingMaskIntoConstraints = false
        root.addSubview(rootStack)

        NSLayoutConstraint.activate([
            rootStack.leadingAnchor.constraint(equalTo: root.leadingAnchor),
            rootStack.trailingAnchor.constraint(equalTo: root.trailingAnchor),
            rootStack.topAnchor.constraint(equalTo: root.topAnchor),
            rootStack.bottomAnchor.constraint(equalTo: root.bottomAnchor)
        ])

        // Tab strip
        let tabBarContainer = NSView()
        tabBarContainer.wantsLayer = true
        tabBarContainer.translatesAutoresizingMaskIntoConstraints = false
        tabBarContainer.heightAnchor.constraint(equalToConstant: 32).isActive = true

        tabBarStack.orientation = .horizontal
        tabBarStack.spacing = 4
        tabBarStack.edgeInsets = NSEdgeInsets(top: 4, left: 8, bottom: 4, right: 8)
        tabBarStack.translatesAutoresizingMaskIntoConstraints = false

        let docView = NSView()
        docView.translatesAutoresizingMaskIntoConstraints = false
        docView.addSubview(tabBarStack)
        NSLayoutConstraint.activate([
            tabBarStack.leadingAnchor.constraint(equalTo: docView.leadingAnchor),
            tabBarStack.trailingAnchor.constraint(lessThanOrEqualTo: docView.trailingAnchor),
            tabBarStack.topAnchor.constraint(equalTo: docView.topAnchor),
            tabBarStack.bottomAnchor.constraint(equalTo: docView.bottomAnchor),
            docView.heightAnchor.constraint(equalToConstant: 32)
        ])

        tabBarScroll.documentView = docView
        tabBarScroll.hasHorizontalScroller = false
        tabBarScroll.hasVerticalScroller = false
        tabBarScroll.drawsBackground = false
        tabBarScroll.translatesAutoresizingMaskIntoConstraints = false

        let newTabBtn = makeIconButton(title: "+", tooltip: "New Tab (⌘T)", action: #selector(handleNewTab))
        newTabBtn.setContentHuggingPriority(.required, for: .horizontal)

        let tabRow = NSStackView(views: [tabBarScroll, newTabBtn])
        tabRow.orientation = .horizontal
        tabRow.spacing = 4
        tabRow.edgeInsets = NSEdgeInsets(top: 0, left: 0, bottom: 0, right: 8)
        tabRow.translatesAutoresizingMaskIntoConstraints = false
        tabBarContainer.addSubview(tabRow)
        NSLayoutConstraint.activate([
            tabRow.leadingAnchor.constraint(equalTo: tabBarContainer.leadingAnchor),
            tabRow.trailingAnchor.constraint(equalTo: tabBarContainer.trailingAnchor),
            tabRow.topAnchor.constraint(equalTo: tabBarContainer.topAnchor),
            tabRow.bottomAnchor.constraint(equalTo: tabBarContainer.bottomAnchor)
        ])

        // Toolbar
        configureButton(backButton, symbol: "chevron.left", fallback: "◀", tooltip: "Back (⌘[)", action: #selector(handleBack))
        configureButton(forwardButton, symbol: "chevron.right", fallback: "▶", tooltip: "Forward (⌘])", action: #selector(handleForward))
        configureButton(reloadButton, symbol: "arrow.clockwise", fallback: "↻", tooltip: "Reload (⌘R)", action: #selector(handleReload))
        configureButton(shieldButton, symbol: "shield.fill", fallback: "🛡", tooltip: "Toggle Tracker Blocker (⌘⇧B)", action: #selector(handleToggleBlocker))
        configureButton(bookmarkButton, symbol: "star", fallback: "☆", tooltip: "Bookmark Page (⌘D)", action: #selector(handleBookmark))
        configureButton(privateTabButton, symbol: "eye.slash", fallback: "Priv", tooltip: "New Private Tab (⌘⇧N)", action: #selector(handleNewPrivateTab))
        configureButton(menuButton, symbol: "line.3.horizontal", fallback: "☰", tooltip: "Bookmarks & History", action: #selector(handleShowMenu(_:)))

        addressField.placeholderString = "Search DuckDuckGo or enter URL"
        addressField.bezelStyle = .roundedBezel
        addressField.font = .systemFont(ofSize: 13)
        addressField.focusRingType = .default
        addressField.lineBreakMode = .byTruncatingTail
        addressField.delegate = self
        addressField.setContentHuggingPriority(.defaultLow, for: .horizontal)
        addressField.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        let toolbar = NSStackView(views: [
            backButton, forwardButton, reloadButton, shieldButton,
            addressField,
            bookmarkButton, privateTabButton, menuButton
        ])
        toolbar.orientation = .horizontal
        toolbar.spacing = 6
        toolbar.edgeInsets = NSEdgeInsets(top: 6, left: 10, bottom: 6, right: 10)
        toolbar.translatesAutoresizingMaskIntoConstraints = false
        toolbar.heightAnchor.constraint(equalToConstant: 40).isActive = true

        // Progress bar
        progressBar.style = .bar
        progressBar.isIndeterminate = false
        progressBar.minValue = 0
        progressBar.maxValue = 1
        progressBar.controlSize = .small
        progressBar.isHidden = true
        progressBar.translatesAutoresizingMaskIntoConstraints = false
        progressBar.heightAnchor.constraint(equalToConstant: 3).isActive = true

        let separator = NSBox()
        separator.boxType = .separator

        contentContainer.translatesAutoresizingMaskIntoConstraints = false

        rootStack.addArrangedSubview(tabBarContainer)
        rootStack.addArrangedSubview(toolbar)
        rootStack.addArrangedSubview(progressBar)
        rootStack.addArrangedSubview(separator)
        rootStack.addArrangedSubview(contentContainer)

        tabBarContainer.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        toolbar.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        progressBar.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        separator.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
        contentContainer.widthAnchor.constraint(equalTo: rootStack.widthAnchor).isActive = true
    }

    private func makeIconButton(title: String, tooltip: String, action: Selector) -> NSButton {
        let btn = NSButton(title: title, target: self, action: action)
        btn.bezelStyle = .texturedRounded
        btn.toolTip = tooltip
        btn.font = .systemFont(ofSize: 13, weight: .medium)
        return btn
    }

    private func configureButton(_ btn: NSButton, symbol: String, fallback: String, tooltip: String, action: Selector) {
        btn.target = self
        btn.action = action
        btn.bezelStyle = .texturedRounded
        btn.toolTip = tooltip
        if let img = NSImage(systemSymbolName: symbol, accessibilityDescription: tooltip) {
            btn.image = img
            btn.imagePosition = .imageOnly
        } else {
            btn.title = fallback
        }
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
            pill.layer?.cornerRadius = 6
            pill.edgeInsets = NSEdgeInsets(top: 2, left: 8, bottom: 2, right: 6)
            if isSelected {
                pill.layer?.backgroundColor = NSColor.controlAccentColor.withAlphaComponent(0.18).cgColor
            } else {
                pill.layer?.backgroundColor = NSColor.windowBackgroundColor.withAlphaComponent(0.6).cgColor
            }

            let prefix = tab.isPrivate ? "Private · " : ""
            let rawTitle = tab.state.title
            let display = prefix + (rawTitle.count > 24 ? String(rawTitle.prefix(23)) + "…" : rawTitle)

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
        window?.title = st.isPrivate ? "SNC (Private) — \(st.title)" : "SNC — \(st.title)"

        backButton.isEnabled = st.canGoBack
        forwardButton.isEnabled = st.canGoForward

        if let reloadImg = NSImage(systemSymbolName: st.isLoading ? "xmark" : "arrow.clockwise", accessibilityDescription: "Reload") {
            reloadButton.image = reloadImg
        } else {
            reloadButton.title = st.isLoading ? "✕" : "↻"
        }

        if let shieldImg = NSImage(
            systemSymbolName: appDelegate.blockerEnabled ? "shield.fill" : "shield.slash",
            accessibilityDescription: "Tracker Blocker"
        ) {
            shieldButton.image = shieldImg
        }
        shieldButton.contentTintColor = appDelegate.blockerEnabled ? .systemGreen : .secondaryLabelColor
        shieldButton.toolTip = appDelegate.blockerEnabled ? "Tracker Blocker: ON (⌘⇧B)" : "Tracker Blocker: OFF (⌘⇧B)"

        let isStarred = appDelegate.store.isBookmarked(url: st.urlString)
        if let starImg = NSImage(systemSymbolName: isStarred ? "star.fill" : "star", accessibilityDescription: "Bookmark") {
            bookmarkButton.image = starImg
        } else {
            bookmarkButton.title = isStarred ? "★" : "☆"
        }
        bookmarkButton.contentTintColor = isStarred ? .systemYellow : nil

        if window?.firstResponder != addressField.currentEditor() {
            addressField.stringValue = st.urlString
        }

        progressBar.isHidden = !st.isLoading
        progressBar.doubleValue = st.estimatedProgress
    }

    // MARK: - Actions

    @objc private func handleNewTab() { addTab(url: URLResolver.defaultHomeURL, isPrivate: false) }
    @objc private func handleNewPrivateTab() { addTab(url: URLResolver.defaultHomeURL, isPrivate: true) }
    @objc private func handleBack() { goBack() }
    @objc private func handleForward() { goForward() }
    @objc private func handleReload() { reloadOrStopCurrentTab() }
    @objc private func handleToggleBlocker() { appDelegate.toggleBlockerAction(nil) }
    @objc private func handleBookmark() { toggleBookmark() }

    @objc private func handleSelectTab(_ sender: NSButton) {
        selectTab(at: sender.tag)
    }

    @objc private func handleCloseTab(_ sender: NSButton) {
        closeTab(at: sender.tag)
    }

    @objc private func handleShowMenu(_ sender: NSButton) {
        let menu = NSMenu()
        let store = appDelegate.store

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
            let url = URLResolver.resolve(addressField.stringValue)
            currentTab?.load(url: url)
            window?.makeFirstResponder(currentTab?.webView)
            return true
        }
        return false
    }

    // MARK: - TabViewControllerDelegate

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
}
