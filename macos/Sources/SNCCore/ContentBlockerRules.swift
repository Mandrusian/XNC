import Foundation

/// Built-in WebKit Content Blocker rules matching `shared/blocklist.json`.
public enum ContentBlockerRules {
    public static let ruleListIdentifier = "com.mandrusian.snc.tracker-blocker"

    public static let blockedDomains: [String] = [
        "doubleclick.net",
        "googlesyndication.com",
        "googleadservices.com",
        "google-analytics.com",
        "googletagmanager.com",
        "facebook.net",
        "connect.facebook.net",
        "ads.twitter.com",
        "adnxs.com",
        "criteo.com",
        "taboola.com",
        "outbrain.com",
        "scorecardresearch.com",
        "quantserve.com",
        "hotjar.com",
        "mixpanel.com",
        "segment.io",
        "amplitude.com",
        "sentry.io",
        "amazon-adsystem.com",
        "rubiconproject.com",
        "pubmatic.com",
        "openx.net"
    ]

    public static func defaultRulesJSON() -> String {
        let entries: [[String: Any]] = blockedDomains.map { domain in
            let escaped = NSRegularExpression.escapedPattern(for: domain)
            return [
                "trigger": [
                    "url-filter": "^https?://([^/]+\\.)?\(escaped)[:/]",
                    "load-type": ["third-party"]
                ],
                "action": [
                    "type": "block"
                ]
            ]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: entries, options: [.prettyPrinted]),
              let json = String(data: data, encoding: .utf8) else {
            return "[]"
        }
        return json
    }

    public static func loadRulesJSON(from bundle: Bundle? = nil) -> String {
        if let bundleURL = bundle?.url(forResource: "blocklist", withExtension: "json"),
           let contents = try? String(contentsOf: bundleURL, encoding: .utf8),
           isValidRulesJSON(contents) {
            return contents
        }
        return defaultRulesJSON()
    }

    public static func isValidRulesJSON(_ json: String) -> Bool {
        guard let data = json.data(using: .utf8),
              let parsed = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]],
              !parsed.isEmpty else {
            return false
        }
        return parsed.allSatisfy { item in
            let trigger = item["trigger"] as? [String: Any]
            let action = item["action"] as? [String: Any]
            return trigger?["url-filter"] is String && action?["type"] is String
        }
    }
}
