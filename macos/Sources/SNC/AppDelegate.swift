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
        compileBlockerRules()
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

    @objc func nextTabAction(_ sender: Any?) {
        activeWindow()?.selectNextTab()
    }

    @objc func previousTabAction(_ sender: Any?) {
        activeWindow()?.selectPreviousTab()
    }

    func windowDidClose(_ controller: BrowserWindowController) {
        windows.removeAll { $0 === controller }
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

    private func compileBlockerRules() {
        let rulesJSON = ContentBlockerRules.loadRulesJSON(from: Bundle.main)
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
        let blockerItem = NSMenuItem(title: "Toggle Ad & Tracker Blocker", action: #selector(toggleBlockerAction(_:)), keyEquivalent: "B")
        blockerItem.keyEquivalentModifierMask = [.command, .shift]
        viewMenu.addItem(blockerItem)
        viewMenu.addItem(.separator())
        let nextTab = NSMenuItem(title: "Next Tab", action: #selector(nextTabAction(_:)), keyEquivalent: "\t")
        nextTab.keyEquivalentModifierMask = [.control]
        viewMenu.addItem(nextTab)
        let prevTab = NSMenuItem(title: "Previous Tab", action: #selector(previousTabAction(_:)), keyEquivalent: "\t")
        prevTab.keyEquivalentModifierMask = [.control, .shift]
        viewMenu.addItem(prevTab)
        viewMenuItem.submenu = viewMenu

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
