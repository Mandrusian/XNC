import Foundation

public struct Bookmark: Codable, Equatable {
    public var title: String
    public var url: String

    public init(title: String, url: String) {
        self.title = title
        self.url = url
    }
}

public struct HistoryEntry: Codable, Equatable {
    public var title: String
    public var url: String
    public var visitedAt: Date

    public init(title: String, url: String, visitedAt: Date = Date()) {
        self.title = title
        self.url = url
        self.visitedAt = visitedAt
    }
}

/// Lightweight JSON persistence for bookmarks, history, allowlist, zapper selectors, SearXNG, and Mica toggle.
public final class BrowserStore {
    public static let maxHistoryEntries = 500

    private struct Snapshot: Codable {
        var bookmarks: [Bookmark]
        var history: [HistoryEntry]
        var allowlistedHosts: [String]?
        var customSelectors: [String: [String]]?
        var searchEngineURL: String?
        var transparentWebsites: Bool?
    }

    public let directoryURL: URL
    public private(set) var bookmarks: [Bookmark] = []
    public private(set) var history: [HistoryEntry] = []
    public private(set) var allowlistedHosts: [String] = []
    public private(set) var customSelectors: [String: [String]] = [:]
    public private(set) var searchEngineURL: String = ""
    public private(set) var transparentWebsites: Bool = false

    private var storeFileURL: URL {
        directoryURL.appendingPathComponent("store.json")
    }

    public init(directoryURL: URL = BrowserStore.defaultDirectoryURL()) {
        self.directoryURL = directoryURL
        load()
    }

    public static func defaultDirectoryURL() -> URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support")
        return base.appendingPathComponent("XNC", isDirectory: true)
    }

    public func load() {
        guard let data = try? Data(contentsOf: storeFileURL) else { return }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        if let snapshot = try? decoder.decode(Snapshot.self, from: data) {
            self.bookmarks = snapshot.bookmarks
            self.history = snapshot.history
            self.allowlistedHosts = snapshot.allowlistedHosts ?? []
            self.customSelectors = snapshot.customSelectors ?? [:]
            self.searchEngineURL = URLResolver.normalizeSearXNGURL(snapshot.searchEngineURL ?? "")
            self.transparentWebsites = snapshot.transparentWebsites ?? false
        }
    }

    public func save() {
        try? FileManager.default.createDirectory(at: directoryURL, withIntermediateDirectories: true)
        let snapshot = Snapshot(
            bookmarks: bookmarks,
            history: history,
            allowlistedHosts: allowlistedHosts,
            customSelectors: customSelectors,
            searchEngineURL: searchEngineURL,
            transparentWebsites: transparentWebsites
        )
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted]
        encoder.dateEncodingStrategy = .iso8601
        if let data = try? encoder.encode(snapshot) {
            try? data.write(to: storeFileURL, options: .atomic)
        }
    }

    public func setSearchEngineURL(_ raw: String) {
        searchEngineURL = URLResolver.normalizeSearXNGURL(raw)
        save()
    }

    public func setTransparentWebsites(_ enabled: Bool) {
        transparentWebsites = enabled
        save()
    }

    public func isBookmarked(url: String) -> Bool {
        bookmarks.contains { $0.url == url }
    }

    @discardableResult
    public func toggleBookmark(title: String, url: String) -> Bool {
        guard !url.isEmpty else { return false }
        if let idx = bookmarks.firstIndex(where: { $0.url == url }) {
            bookmarks.remove(at: idx)
            save()
            return false
        }
        let cleanTitle = title.trimmingCharacters(in: .whitespacesAndNewlines)
        bookmarks.append(Bookmark(title: cleanTitle.isEmpty ? url : cleanTitle, url: url))
        save()
        return true
    }

    public func recordHistory(title: String, url: String, isPrivate: Bool, date: Date = Date()) {
        guard !isPrivate, !url.isEmpty, !url.hasPrefix("about:") else { return }
        let cleanTitle = title.trimmingCharacters(in: .whitespacesAndNewlines)
        let resolvedTitle = cleanTitle.isEmpty ? url : cleanTitle

        if let first = history.first, first.url == url {
            history[0] = HistoryEntry(title: resolvedTitle, url: url, visitedAt: date)
            save()
            return
        }

        history.insert(HistoryEntry(title: resolvedTitle, url: url, visitedAt: date), at: 0)
        if history.count > Self.maxHistoryEntries {
            history.removeLast(history.count - Self.maxHistoryEntries)
        }
        save()
    }

    public func clearHistory() {
        history.removeAll()
        save()
    }

    private func normalizeHost(_ host: String) -> String {
        host.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
    }

    public func isHostAllowlisted(_ host: String) -> Bool {
        let norm = normalizeHost(host)
        guard !norm.isEmpty else { return false }
        return allowlistedHosts.contains(norm)
    }

    @discardableResult
    public func toggleHostAllowlist(_ host: String) -> Bool {
        let norm = normalizeHost(host)
        guard !norm.isEmpty else { return false }
        if let idx = allowlistedHosts.firstIndex(of: norm) {
            allowlistedHosts.remove(at: idx)
            save()
            return false
        }
        allowlistedHosts.append(norm)
        save()
        return true
    }

    public func customSelectors(for host: String) -> [String] {
        customSelectors[normalizeHost(host)] ?? []
    }

    public func addCustomSelector(_ selector: String, for host: String) {
        let norm = normalizeHost(host)
        let sel = selector.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !norm.isEmpty, !sel.isEmpty else { return }
        var list = customSelectors[norm] ?? []
        if !list.contains(sel) {
            list.append(sel)
            customSelectors[norm] = list
            save()
        }
    }

    public func clearCustomSelectors(for host: String) {
        let norm = normalizeHost(host)
        if customSelectors.removeValue(forKey: norm) != nil {
            save()
        }
    }
}
