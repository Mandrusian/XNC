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

/// Lightweight JSON persistence for bookmarks and non-private browsing history.
public final class BrowserStore {
    public static let maxHistoryEntries = 500

    private struct Snapshot: Codable {
        var bookmarks: [Bookmark]
        var history: [HistoryEntry]
    }

    public let directoryURL: URL
    public private(set) var bookmarks: [Bookmark] = []
    public private(set) var history: [HistoryEntry] = []

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
        return base.appendingPathComponent("SNC", isDirectory: true)
    }

    public func load() {
        guard let data = try? Data(contentsOf: storeFileURL) else { return }
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        if let snapshot = try? decoder.decode(Snapshot.self, from: data) {
            self.bookmarks = snapshot.bookmarks
            self.history = snapshot.history
        }
    }

    public func save() {
        try? FileManager.default.createDirectory(at: directoryURL, withIntermediateDirectories: true)
        let snapshot = Snapshot(bookmarks: bookmarks, history: history)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted]
        encoder.dateEncodingStrategy = .iso8601
        if let data = try? encoder.encode(snapshot) {
            try? data.write(to: storeFileURL, options: .atomic)
        }
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
}
