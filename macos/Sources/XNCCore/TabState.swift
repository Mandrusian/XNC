import Foundation

/// Immutable metadata snapshot describing an open browser tab.
public struct TabState: Identifiable, Equatable {
    public let id: UUID
    public var title: String
    public var urlString: String
    public let isPrivate: Bool
    public var isLoading: Bool
    public var estimatedProgress: Double
    public var canGoBack: Bool
    public var canGoForward: Bool

    public init(
        id: UUID = UUID(),
        title: String = "",
        urlString: String = "",
        isPrivate: Bool = false,
        isLoading: Bool = false,
        estimatedProgress: Double = 0,
        canGoBack: Bool = false,
        canGoForward: Bool = false
    ) {
        self.id = id
        self.title = title.isEmpty ? (isPrivate ? "Private Tab" : "New Tab") : title
        self.urlString = urlString
        self.isPrivate = isPrivate
        self.isLoading = isLoading
        self.estimatedProgress = estimatedProgress
        self.canGoBack = canGoBack
        self.canGoForward = canGoForward
    }
}
