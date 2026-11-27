import AppKit
import SNCCore
import WebKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    let store = BrowserStore()
    private(set) var contentRuleList: WKContentRuleList?
    private(set) var blockerEnabled = true
    private var windows: [BrowserWindowController] = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        setupMenu()
        recompileBlockerRules()
        openNewWindow()
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    @objc func newWindowAction(_ sender: Any?) {
        openNewWindow()
    }

    @objc func newTabAction(_ sender: Any?) {
        activeWindow()?.addTab(url: URLResolver.defaultHomeURL, isPrivate: false)
    }

    @objc func newPrivateTabAction(_ sender: Any?) {
        activeWindow()?.addTab(url: URLResolver.defaultHomeURL, isPrivate: true)
    }

    @objc func closeTabAction(_ sender: Any?) {
        activeWindow()?.closeCurrentTabOrWindow()
    }

    @objc func focusAddressBarAction(_ sender: Any?) {
        activeWindow()?.focusAddressBar()
    }

    @objc func reloadPageAction(_ sender: Any?) {
        activeWindow()?.reloadOrStopCurrentTab()
    }

    @objc func goBackAction(_ sender: Any?) {
        activeWindow()?.goBack()
    }

    @objc func goForwardAction(_ sender: Any?) {
        activeWindow()?.goForward()
    }

    @objc func toggleBookmarkAction(_ sender: Any?) {
        activeWindow()?.toggleBookmark()
    }

    @objc func toggleBlockerAction(_ sender: Any?) {
        blockerEnabled.toggle()
        for win in windows {
            win.applyBlockerState(enabled: blockerEnabled, ruleList: contentRuleList)
        }
    }

    @objc func toggleCurrentHostAllowlistAction(_ sender: Any?) {
        guard let host = activeWindow()?.currentHost(), !host.isEmpty else { return }
        store.toggleHostAllowlist(host)
        recompileBlockerRules()
    }

    @objc func toggleZapperAction(_ sender: Any?) {
        activeWindow()?.toggleZapper()
    }

    @objc func openWebInspectorAction(_ sender: Any?) {
        activeWindow()?.openWebInspector()
    }

    @objc func openJSConsoleAction(_ sender: Any?) {
        activeWindow()?.showJSConsole()
    }

    @objc func viewPageSourceAction(_ sender: Any?) {
        activeWindow()?.viewPageSource()
    }

    @objc func toggleJavaScriptAction(_ sender: Any?) {
        activeWindow()?.toggleJavaScript()
    }

    @objc func setUserAgentAction(_ sender: NSMenuItem) {
        let ua = sender.representedObject as? String
        activeWindow()?.setCustomUserAgent(ua)
    }

    @objc func clearBrowsingDataAction(_ sender: Any?) {
        let types = WKWebsiteDataStore.allWebsiteDataTypes()
        WKWebsiteDataStore.default().removeData(ofTypes: types, modifiedSince: .distantPast) { [weak self] in
            DispatchQueue.main.async {
                self?.activeWindow()?.reloadOrStopCurrentTab()
            }
        }
    }

    @objc func nextTabAction(_ sender: Any?) {
        activeWindow()?.selectNextTab()
    }

    @objc func previousTabAction(_ sender: Any?) {
        activeWindow()?.selectPreviousTab()
    }

    func windowDidClose(_ controller: BrowserWindowController) {
        windows.removeAll { $0 === controller }
    }

    func recompileBlockerRules() {
        let rulesJSON = ContentBlockerRules.loadRulesJSON(
            from: Bundle.main,
            excludingAllowlistedHosts: store.allowlistedHosts
        )
        WKContentRuleListStore.default().compileContentRuleList(
            forIdentifier: ContentBlockerRules.ruleListIdentifier,
            encodedContentRuleList: rulesJSON
        ) { [weak self] list, _ in
            DispatchQueue.main.async {
                guard let self else { return }
                self.contentRuleList = list
                for win in self.windows {
                    win.applyBlockerState(enabled: self.blockerEnabled, ruleList: list)
                }
            }
        }
    }

    private func openNewWindow() {
        let win = BrowserWindowController(appDelegate: self)
        windows.append(win)
        win.showWindow(nil)
        win.window?.makeKeyAndOrderFront(nil)
    }

    private func activeWindow() -> BrowserWindowController? {
        if let key = NSApp.keyWindow?.windowController as? BrowserWindowController {
            return key
        }
        return windows.first
    }

    private func setupMenu() {
        let mainMenu = NSMenu()

        let appMenuItem = NSMenuItem()
        mainMenu.addItem(appMenuItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "About SNC", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: "Hide SNC", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        appMenu.addItem(withTitle: "Quit SNC", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appMenuItem.submenu = appMenu

        let fileMenuItem = NSMenuItem()
        mainMenu.addItem(fileMenuItem)
        let fileMenu = NSMenu(title: "File")
        fileMenu.addItem(withTitle: "New Tab", action: #selector(newTabAction(_:)), keyEquivalent: "t")
        let privateItem = NSMenuItem(title: "New Private Tab", action: #selector(newPrivateTabAction(_:)), keyEquivalent: "N")
        privateItem.keyEquivalentModifierMask = [.command, .shift]
        fileMenu.addItem(privateItem)
        fileMenu.addItem(withTitle: "New Window", action: #selector(newWindowAction(_:)), keyEquivalent: "n")
        fileMenu.addItem(.separator())
        fileMenu.addItem(withTitle: "Close Tab", action: #selector(closeTabAction(_:)), keyEquivalent: "w")
        fileMenuItem.submenu = fileMenu

        let editMenuItem = NSMenuItem()
        mainMenu.addItem(editMenuItem)
        let editMenu = NSMenu(title: "Edit")
        editMenu.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
        editMenu.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "Z")
        editMenu.addItem(.separator())
        editMenu.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
        editMenu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
        editMenu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editMenuItem.submenu = editMenu

        let viewMenuItem = NSMenuItem()
        mainMenu.addItem(viewMenuItem)
        let viewMenu = NSMenu(title: "View")
        viewMenu.addItem(withTitle: "Focus Address Bar", action: #selector(focusAddressBarAction(_:)), keyEquivalent: "l")
        viewMenu.addItem(withTitle: "Reload Page", action: #selector(reloadPageAction(_:)), keyEquivalent: "r")
        viewMenu.addItem(withTitle: "Back", action: #selector(goBackAction(_:)), keyEquivalent: "[")
        viewMenu.addItem(withTitle: "Forward", action: #selector(goForwardAction(_:)), keyEquivalent: "]")
        viewMenu.addItem(.separator())
        let blockerItem = NSMenuItem(title: "Toggle uBlock Shield", action: #selector(toggleBlockerAction(_:)), keyEquivalent: "B")
        blockerItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenu.addItem(blockerItem)
        let zapperItem = NSMenuItem(title: "Element Zapper", action: #selector(toggleZapperAction(_:)), keyEquivalent: "Z")
        zapperItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenu.addItem(zapperItem)
        viewMenu.addItem(.separator())
        let nextTab = NSMenuItem(title: "Next Tab", action: #selector(nextTabAction(_:)), keyEquivalent: "\t")
        nextTab.keyEquivalentModifierMask = [.control]
        viewMenu.addItem(nextTab)
        let prevTab = NSMenuItem(title: "Previous Tab", action: #selector(previousTabAction(_:)), keyEquivalent: "\t")
        prevTab.keyEquivalentModifierMask = [.control, .shift]
        viewMenu.addItem(prevTab)
        viewMenuItem.submenu = viewMenu

        let devMenuItem = NSMenuItem()
        mainMenu.addItem(devMenuItem)
        let devMenu = NSMenu(title: "Develop")
        let inspItem = NSMenuItem(title: "Open Web Inspector", action: #selector(openWebInspectorAction(_:)), keyEquivalent: "i")
        inspItem.keyEquivalentModifierMask = [.command, .option]
        devMenu.addItem(inspItem)
        let consoleItem = NSMenuItem(title: "JavaScript Console…", action: #selector(openJSConsoleAction(_:)), keyEquivalent: "j")
        consoleItem.keyEquivalentModifierMask = [.command, .option]
        devMenu.addItem(consoleItem)
        let sourceItem = NSMenuItem(title: "View Page Source", action: #selector(viewPageSourceAction(_:)), keyEquivalent: "u")
        sourceItem.keyEquivalentModifierMask = [.command, .option]
        devMenu.addItem(sourceItem)
        devMenu.addItem(.separator())
        let zapDevItem = NSMenuItem(title: "Element Zapper", action: #selector(toggleZapperAction(_:)), keyEquivalent: "Z")
        zapDevItem.keyEquivalentModifierMask = [.command, .shift]
        devMenu.addItem(zapDevItem)
        devMenu.addItem(withTitle: "Toggle JavaScript on Tab", action: #selector(toggleJavaScriptAction(_:)), keyEquivalent: "")

        let uaItem = NSMenuItem(title: "User-Agent", action: nil, keyEquivalent: "")
        let uaMenu = NSMenu(title: "User-Agent")
        let presets: [(String, String?)] = [
            ("Default (WebKit)", nil),
            ("Safari (macOS)", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"),
            ("Firefox (macOS)", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:132.0) Gecko/20100101 Firefox/132.0"),
            ("Mobile Safari (iPhone)", "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1")
        ]
        for (title, ua) in presets {
            let item = NSMenuItem(title: title, action: #selector(setUserAgentAction(_:)), keyEquivalent: "")
            item.representedObject = ua
            uaMenu.addItem(item)
        }
        uaItem.submenu = uaMenu
        devMenu.addItem(uaItem)

        devMenu.addItem(.separator())
        devMenu.addItem(withTitle: "Clear Browsing Data & Cache", action: #selector(clearBrowsingDataAction(_:)), keyEquivalent: "")
        devMenuItem.submenu = devMenu

        let bookmarksMenuItem = NSMenuItem()
        mainMenu.addItem(bookmarksMenuItem)
        let bookmarksMenu = NSMenu(title: "Bookmarks")
        bookmarksMenu.addItem(withTitle: "Bookmark Current Page", action: #selector(toggleBookmarkAction(_:)), keyEquivalent: "d")
        bookmarksMenuItem.submenu = bookmarksMenu

        let windowMenuItem = NSMenuItem()
        mainMenu.addItem(windowMenuItem)
        let windowMenu = NSMenu(title: "Window")
        windowMenu.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        windowMenuItem.submenu = windowMenu

        NSApp.mainMenu = mainMenu
        NSApp.windowsMenu = windowMenu
    }
}
