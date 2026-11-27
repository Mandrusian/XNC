import Foundation

/// Built-in uBlock-style WebKit Content Blocker rules, cosmetic CSS, Element Zapper, and Transparent Mica mode.
public enum ContentBlockerRules {
    public static let ruleListIdentifier = "com.mandrusian.xnc.tracker-blocker"

    public static let blockedDomains: [String] = [
        "doubleclick.net", "googlesyndication.com", "googleadservices.com",
        "google-analytics.com", "googletagmanager.com", "googletagservices.com",
        "pagead2.googlesyndication.com", "adservice.google.com",
        "facebook.net", "connect.facebook.net", "pixel.facebook.com",
        "ads.twitter.com", "analytics.twitter.com", "ads-twitter.com",
        "ads.linkedin.com", "px.ads.linkedin.com", "snap.licdn.com",
        "analytics.tiktok.com", "ads.tiktok.com",
        "adnxs.com", "criteo.com", "criteo.net", "taboola.com", "outbrain.com",
        "scorecardresearch.com", "quantserve.com", "hotjar.com", "mixpanel.com",
        "segment.io", "segment.com", "amplitude.com", "sentry.io", "bugsnag.com",
        "fullstory.com", "clarity.ms", "mouseflow.com", "crazyegg.com",
        "heapanalytics.com", "newrelic.com", "nr-data.net",
        "amazon-adsystem.com", "rubiconproject.com", "pubmatic.com", "openx.net",
        "casalemedia.com", "indexexchange.com", "appnexus.com", "advertising.com",
        "adform.net", "adsrvr.org", "smartadserver.com", "mediavine.com",
        "adthrive.com", "bidswitch.net", "chartbeat.com", "omtrdc.net",
        "demdex.net", "krxd.net", "branch.io", "appsflyer.com", "adjust.com",
        "onetrust.com", "cookiebot.com", "consensu.org", "trustarc.com"
    ]

    public static let defaultCosmeticCSS = """
    ins.adsbygoogle,
    div[id^="google_ads_iframe"],
    div[id^="div-gpt-ad"],
    iframe[src*="doubleclick.net"],
    iframe[src*="googlesyndication.com"],
    [data-ad-slot],
    [data-google-query-id],
    .taboola-container,
    #taboola-below-article-thumbnails,
    .OUTBRAIN,
    [class*="sponsored-post"],
    [class*="ad-banner"],
    #onetrust-banner-sdk,
    #onetrust-consent-sdk,
    #CybotCookiebotDialog,
    .fc-consent-root {
      display: none !important;
    }
    """

    public static let transparentMicaCSS = """
    :root, html, body {
      background: transparent !important;
      background-color: transparent !important;
    }
    main, article, section, header, nav, footer, aside, [role="main"] {
      background-color: rgba(128, 128, 140, 0.08) !important;
    }
    @media (prefers-color-scheme: dark) {
      body {
        color-scheme: dark;
      }
      main, article, section, header, nav, footer, aside, [role="main"] {
        background-color: rgba(18, 18, 24, 0.34) !important;
      }
    }
    @media (prefers-color-scheme: light) {
      main, article, section, header, nav, footer, aside, [role="main"] {
        background-color: rgba(255, 255, 255, 0.42) !important;
      }
    }
    """

    public static let defaultZapperScript = #"""
    (() => {
      if (typeof window.__xncZapperCleanup === 'function') {
        window.__xncZapperCleanup();
        return 'off';
      }
      const overlay = document.createElement('div');
      overlay.id = '__xnc-zapper-overlay';
      Object.assign(overlay.style, {
        position: 'fixed', pointerEvents: 'none', border: '2px solid #ef4444',
        background: 'rgba(239, 68, 68, 0.14)', borderRadius: '4px',
        zIndex: '2147483646', display: 'none', boxSizing: 'border-box', transition: 'all 40ms ease-out'
      });
      const hud = document.createElement('div');
      hud.id = '__xnc-zapper-hud';
      Object.assign(hud.style, {
        position: 'fixed', bottom: '14px', left: '50%', transform: 'translateX(-50%)',
        padding: '7px 14px', borderRadius: '999px', background: 'rgba(20, 20, 24, 0.88)',
        color: '#f8fafc', font: '600 12px/1.3 -apple-system, BlinkMacSystemFont, sans-serif',
        boxShadow: '0 8px 28px rgba(0,0,0,0.35)', border: '1px solid rgba(255,255,255,0.16)',
        backdropFilter: 'blur(12px)', zIndex: '2147483647', pointerEvents: 'none', whiteSpace: 'nowrap'
      });
      hud.textContent = 'XNC Zapper · Click: Zap | Shift+Click: Hide Forever | U: Undo | Esc: Done';
      document.documentElement.appendChild(overlay);
      document.documentElement.appendChild(hud);
      let currentTarget = null;
      const undoStack = [];
      function escapeIdent(s) {
        return (typeof CSS !== 'undefined' && CSS.escape) ? CSS.escape(s) : String(s).replace(/[^a-zA-Z0-9_-]/g, '\\$&');
      }
      function describeElement(el) {
        if (!el || el.nodeType !== 1) return '';
        let desc = el.tagName.toLowerCase();
        if (el.id) desc += '#' + el.id;
        if (el.classList && el.classList.length) desc += '.' + Array.from(el.classList).slice(0, 2).join('.');
        return desc;
      }
      function computeSelector(el) {
        if (!el || el.nodeType !== 1) return '';
        if (el.id) return '#' + escapeIdent(el.id);
        const parts = [];
        let cur = el;
        for (let d = 0; cur && cur.nodeType === 1 && d < 4; d++) {
          let sel = cur.tagName.toLowerCase();
          if (cur.id) { parts.unshift('#' + escapeIdent(cur.id)); break; }
          if (cur.classList && cur.classList.length) {
            const cls = Array.from(cur.classList).filter(Boolean).slice(0, 2);
            if (cls.length) sel += '.' + cls.map(escapeIdent).join('.');
          }
          const p = cur.parentElement;
          if (p) {
            const sibs = Array.from(p.children).filter(c => c.tagName === cur.tagName);
            if (sibs.length > 1) sel += ':nth-of-type(' + (sibs.indexOf(cur) + 1) + ')';
          }
          parts.unshift(sel);
          cur = p;
        }
        return parts.join(' > ');
      }
      function persistRule(host, selector) {
        if (!selector) return;
        const style = document.createElement('style');
        style.setAttribute('data-xnc-zapper', '1');
        style.textContent = selector + ' { display: none !important; }';
        (document.head || document.documentElement).appendChild(style);
        const payload = JSON.stringify({ type: 'zapper', host: host || '', selector });
        try {
          if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.xncZapper) {
            window.webkit.messageHandlers.xncZapper.postMessage(payload);
          }
        } catch (_) {}
      }
      function onMove(e) {
        const el = document.elementFromPoint(e.clientX, e.clientY);
        if (!el || el === overlay || el === hud || el === document.documentElement || el === document.body) {
          overlay.style.display = 'none';
          currentTarget = null;
          return;
        }
        currentTarget = el;
        const r = el.getBoundingClientRect();
        Object.assign(overlay.style, {
          display: 'block', left: r.left + 'px', top: r.top + 'px',
          width: Math.max(2, r.width) + 'px', height: Math.max(2, r.height) + 'px',
          borderColor: e.shiftKey ? '#f59e0b' : '#ef4444',
          background: e.shiftKey ? 'rgba(245, 158, 11, 0.16)' : 'rgba(239, 68, 68, 0.14)'
        });
        hud.textContent = describeElement(el) + ' · ' + Math.round(r.width) + 'x' + Math.round(r.height) +
          ' — Click: Zap | Shift+Click: Hide Always | U: Undo | Esc: Exit';
      }
      function onClick(e) {
        e.preventDefault();
        e.stopImmediatePropagation();
        const target = currentTarget;
        if (!target || !target.parentNode) return;
        if (e.shiftKey) {
          const sel = computeSelector(target);
          persistRule(window.location.hostname, sel);
          hud.textContent = 'Saved rule: ' + sel + ' (Esc to exit)';
        } else {
          undoStack.push({ el: target, parent: target.parentNode, next: target.nextSibling });
          target.remove();
          overlay.style.display = 'none';
          currentTarget = null;
        }
      }
      function onKey(e) {
        if (e.key === 'Escape') { e.preventDefault(); cleanup(); }
        else if ((e.key === 'u' || e.key === 'U') && !e.metaKey && !e.ctrlKey) {
          const item = undoStack.pop();
          if (item && item.parent) item.parent.insertBefore(item.el, item.next || null);
        }
      }
      function cleanup() {
        document.removeEventListener('mousemove', onMove, true);
        document.removeEventListener('click', onClick, true);
        document.removeEventListener('keydown', onKey, true);
        overlay.remove();
        hud.remove();
        delete window.__xncZapperCleanup;
      }
      window.__xncZapperCleanup = cleanup;
      document.addEventListener('mousemove', onMove, true);
      document.addEventListener('click', onClick, true);
      document.addEventListener('keydown', onKey, true);
      return 'on';
    })();
    """#

    public static func defaultRulesJSON(excludingAllowlistedHosts allowlisted: [String] = []) -> String {
        var entries: [[String: Any]] = blockedDomains.map { domain in
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
        let cleaned = allowlisted.map { $0.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() }.filter { !$0.isEmpty }
        if !cleaned.isEmpty {
            entries.append([
                "trigger": [
                    "url-filter": ".*",
                    "if-top-url": cleaned.map { "^https?://([^/]+\\.)?\(NSRegularExpression.escapedPattern(for: $0))[:/]" }
                ],
                "action": [
                    "type": "ignore-previous-rules"
                ]
            ])
        }
        guard let data = try? JSONSerialization.data(withJSONObject: entries, options: [.prettyPrinted]),
              let json = String(data: data, encoding: .utf8) else {
            return "[]"
        }
        return json
    }

    public static func loadRulesJSON(from bundle: Bundle? = nil, excludingAllowlistedHosts allowlisted: [String] = []) -> String {
        if allowlisted.isEmpty,
           let bundleURL = bundle?.url(forResource: "blocklist", withExtension: "json"),
           let contents = try? String(contentsOf: bundleURL, encoding: .utf8),
           isValidRulesJSON(contents) {
            return contents
        }
        return defaultRulesJSON(excludingAllowlistedHosts: allowlisted)
    }

    public static func loadCosmeticCSS(from bundle: Bundle? = nil) -> String {
        if let bundleURL = bundle?.url(forResource: "cosmetic_rules", withExtension: "css"),
           let contents = try? String(contentsOf: bundleURL, encoding: .utf8),
           !contents.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return contents
        }
        return defaultCosmeticCSS
    }

    public static func loadZapperScript(from bundle: Bundle? = nil) -> String {
        if let bundleURL = bundle?.url(forResource: "zapper", withExtension: "js"),
           let contents = try? String(contentsOf: bundleURL, encoding: .utf8),
           !contents.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
            return contents
        }
        return defaultZapperScript
    }

    public static func cssForHost(_ host: String, store: BrowserStore, shieldEnabled: Bool, bundle: Bundle? = nil) -> String {
        let normalized = host.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        let active = shieldEnabled && !store.isHostAllowlisted(normalized)
        var parts: [String] = []
        if active {
            parts.append(loadCosmeticCSS(from: bundle))
        }
        for sel in store.customSelectors(for: normalized) where !sel.isEmpty {
            parts.append("\(sel) { display: none !important; }")
        }
        return parts.joined(separator: "\n")
    }

    public static func injectCSSScript(_ css: String, styleID: String = "__xnc-cosmetic") -> String {
        let escaped = css
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "`", with: "\\`")
            .replacingOccurrences(of: "$", with: "\\$")
        return """
        (() => {
          let s = document.getElementById('\(styleID)');
          if (!s) {
            s = document.createElement('style');
            s.id = '\(styleID)';
            (document.head || document.documentElement).appendChild(s);
          }
          s.textContent = `\(escaped)`;
        })();
        """
    }

    public static func transparentMicaScript(enabled: Bool) -> String {
        if !enabled {
            return "(() => { const s = document.getElementById('__xnc-transparent-mica'); if (s) s.remove(); })();"
        }
        return injectCSSScript(transparentMicaCSS, styleID: "__xnc-transparent-mica")
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
