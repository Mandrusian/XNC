#include "snc/blocker.hpp"
#include "snc/url_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace snc {

namespace {

std::string normalize_domain(std::string_view raw) {
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.front()))) raw.remove_prefix(1);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) raw.remove_suffix(1);
    std::string out(raw);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool host_matches_domain(std::string_view host, std::string_view domain) {
    if (host.empty() || domain.empty()) return false;
    if (host == domain) return true;
    if (host.size() <= domain.size()) return false;
    size_t prefix_len = host.size() - domain.size();
    return host[prefix_len - 1] == '.' && host.substr(prefix_len) == domain;
}

} // namespace

Blocker::Blocker() {
    domains_ = {
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
        "onetrust.com", "cookiebot.com", "consensu.org", "trustarc.com",
    };
}

bool Blocker::load_domains_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::vector<std::string> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (auto hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        std::string dom = normalize_domain(line);
        if (!dom.empty()) {
            loaded.push_back(std::move(dom));
        }
    }
    if (!loaded.empty()) {
        domains_ = std::move(loaded);
        return true;
    }
    return false;
}

void Blocker::set_domains(std::vector<std::string> domains) {
    domains_.clear();
    for (const auto& d : domains) {
        std::string norm = normalize_domain(d);
        if (!norm.empty()) domains_.push_back(std::move(norm));
    }
}

bool Blocker::should_block(std::string_view request_url, std::string_view page_host) const {
    if (!enabled_) return false;

    std::string req_host = extract_host(request_url);
    if (req_host.empty()) return false;

    std::string page = normalize_domain(page_host);
    if (!page.empty() && (req_host == page || host_matches_domain(req_host, page) || host_matches_domain(page, req_host))) {
        return false;
    }

    for (const auto& domain : domains_) {
        if (host_matches_domain(req_host, domain)) {
            return true;
        }
    }
    return false;
}

std::string_view Blocker::default_cosmetic_css() {
    return R"(
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
)";
}

std::string_view Blocker::default_zapper_script() {
    return R"JS((() => {
  if (typeof window.__sncZapperCleanup === 'function') {
    window.__sncZapperCleanup();
    return 'off';
  }
  const overlay = document.createElement('div');
  overlay.id = '__snc-zapper-overlay';
  Object.assign(overlay.style, {
    position: 'fixed', pointerEvents: 'none', border: '2px solid #ef4444',
    background: 'rgba(239, 68, 68, 0.14)', borderRadius: '4px',
    zIndex: '2147483646', display: 'none', boxSizing: 'border-box', transition: 'all 40ms ease-out'
  });
  const hud = document.createElement('div');
  hud.id = '__snc-zapper-hud';
  Object.assign(hud.style, {
    position: 'fixed', bottom: '14px', left: '50%', transform: 'translateX(-50%)',
    padding: '7px 14px', borderRadius: '999px', background: 'rgba(20, 20, 24, 0.88)',
    color: '#f8fafc', font: '600 12px/1.3 -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif',
    boxShadow: '0 8px 28px rgba(0,0,0,0.35)', border: '1px solid rgba(255,255,255,0.16)',
    zIndex: '2147483647', pointerEvents: 'none', whiteSpace: 'nowrap'
  });
  hud.textContent = 'Zapper · Click: Zap | Shift+Click: Hide Always | U: Undo | Esc: Done';
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
    style.setAttribute('data-snc-zapper', '1');
    style.textContent = selector + ' { display: none !important; }';
    (document.head || document.documentElement).appendChild(style);
    const payload = JSON.stringify({ type: 'zapper', host: host || '', selector });
    try {
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.sncZapper) {
        window.webkit.messageHandlers.sncZapper.postMessage(payload);
      } else if (window.chrome && window.chrome.webview) {
        window.chrome.webview.postMessage(payload);
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
    delete window.__sncZapperCleanup;
  }
  window.__sncZapperCleanup = cleanup;
  document.addEventListener('mousemove', onMove, true);
  document.addEventListener('click', onClick, true);
  document.addEventListener('keydown', onKey, true);
  return 'on';
})();)JS";
}

std::string Blocker::build_css_for_host(
    bool shield_active,
    const std::vector<std::string>& custom_selectors,
    std::string_view cosmetic_css) {
    std::ostringstream css;
    if (shield_active) {
        css << cosmetic_css << "\n";
    }
    for (const auto& sel : custom_selectors) {
        if (!sel.empty()) {
            css << sel << " { display: none !important; }\n";
        }
    }
    return css.str();
}

std::string Blocker::build_inject_css_script(std::string_view css) {
    std::string escaped;
    escaped.reserve(css.size() + 32);
    for (char c : css) {
        if (c == '\\') escaped += "\\\\";
        else if (c == '`') escaped += "\\`";
        else if (c == '$') escaped += "\\$";
        else escaped.push_back(c);
    }
    return "(() => { let s = document.getElementById('__snc-cosmetic'); "
           "if (!s) { s = document.createElement('style'); s.id = '__snc-cosmetic'; "
           "(document.head || document.documentElement).appendChild(s); } "
           "s.textContent = `" + escaped + "`; })();";
}

} // namespace snc
