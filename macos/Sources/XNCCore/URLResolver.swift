import Foundation

/// Resolves user address bar input into a direct URL, custom SearXNG search, or DuckDuckGo search.
public enum URLResolver {
    public static let defaultHomeURL = URL(string: "https://duckduckgo.com/")!

    public static func normalizeSearXNGURL(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return "" }

        let lower = trimmed.lowercased()
        var normalized: String
        if lower.hasPrefix("http://") || lower.hasPrefix("https://") {
            normalized = trimmed
        } else if let scheme = inferredSchemeIfHostURL(trimmed) {
            normalized = scheme + trimmed
        } else {
            normalized = "https://" + trimmed
        }

        if normalized.contains("{query}") {
            return normalized
        }

        while normalized.count > 8 && normalized.hasSuffix("/") {
            normalized.removeLast()
        }
        if normalized.lowercased().hasSuffix("/search") && normalized.count > 7 {
            normalized.removeLast(7)
        }
        return normalized
    }

    public static func homeURL(searchBaseURL: String = "") -> URL {
        let norm = normalizeSearXNGURL(searchBaseURL)
        guard !norm.isEmpty else { return defaultHomeURL }
        if norm.contains("{query}"), let url = URL(string: norm), let scheme = url.scheme, let host = url.host {
            let port = url.port.map { ":\($0)" } ?? ""
            if let root = URL(string: "\(scheme)://\(host)\(port)/") {
                return root
            }
        }
        return URL(string: norm + "/") ?? defaultHomeURL
    }

    public static func resolve(_ rawInput: String, searchBaseURL: String = "") -> URL {
        let trimmed = rawInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return homeURL(searchBaseURL: searchBaseURL) }

        let lower = trimmed.lowercased()
        if lower.hasPrefix("http://") || lower.hasPrefix("https://") ||
            lower.hasPrefix("file://") || lower.hasPrefix("about:") {
            if let url = URL(string: trimmed) {
                return url
            }
        }

        if let scheme = inferredSchemeIfHostURL(trimmed), let url = URL(string: scheme + trimmed) {
            return url
        }

        let searx = normalizeSearXNGURL(searchBaseURL)
        if !searx.isEmpty {
            let encoded = trimmed.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? trimmed
            if searx.contains("{query}"),
               let url = URL(string: searx.replacingOccurrences(of: "{query}", with: encoded)) {
                return url
            }
            if var comps = URLComponents(string: searx + "/search") {
                comps.queryItems = [URLQueryItem(name: "q", value: trimmed)]
                if let url = comps.url {
                    return url
                }
            }
        }

        var components = URLComponents(string: "https://duckduckgo.com/")!
        components.queryItems = [URLQueryItem(name: "q", value: trimmed)]
        return components.url ?? defaultHomeURL
    }

    public static func displayHost(for url: URL?) -> String {
        guard let url else { return "" }
        if url.scheme == "about" {
            return url.absoluteString
        }
        guard var host = url.host?.lowercased(), !host.isEmpty else {
            return url.absoluteString
        }
        if host.hasPrefix("www.") && host.count > 4 {
            host.removeFirst(4)
        }
        return host
    }

    private static func inferredSchemeIfHostURL(_ input: String) -> String? {
        if input.contains(where: { $0.isWhitespace }) {
            return nil
        }

        let authority = input.split(whereSeparator: { $0 == "/" || $0 == "?" || $0 == "#" }).first.map(String.init) ?? ""
        guard !authority.isEmpty else { return nil }

        var host = authority
        if !host.hasPrefix("["), let colonIndex = host.lastIndex(of: ":") {
            let portPart = host[host.index(after: colonIndex)...]
            guard !portPart.isEmpty, portPart.allSatisfy(\.isNumber) else {
                return nil
            }
            host = String(host[..<colonIndex])
        }

        let lowerHost = host.lowercased()
        if lowerHost == "localhost" || lowerHost == "127.0.0.1" {
            return "http://"
        }
        if isIPv4(lowerHost) {
            return "https://"
        }
        guard !lowerHost.contains(".."), let dotIndex = lowerHost.lastIndex(of: ".") else {
            return nil
        }
        if dotIndex == lowerHost.startIndex {
            return nil
        }
        let tld = lowerHost[lowerHost.index(after: dotIndex)...]
        guard tld.count >= 2 && tld.allSatisfy({ $0.isLetter || $0.isNumber }) else {
            return nil
        }
        return "https://"
    }

    private static func isIPv4(_ host: String) -> Bool {
        let parts = host.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 4 else { return false }
        return parts.allSatisfy { part in
            !part.isEmpty && part.count <= 3 && Int(part).map { $0 >= 0 && $0 <= 255 } == true
        }
    }
}
