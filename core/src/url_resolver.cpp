#include "xnc/url_resolver.hpp"

#include <algorithm>
#include <cctype>

namespace xnc {

namespace {

std::string_view trim_ascii(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool is_ipv4_host(std::string_view host) {
    if (host.empty()) return false;
    int dots = 0;
    int digits = 0;
    int octet = 0;
    for (char c : host) {
        if (c == '.') {
            if (digits == 0 || octet > 255) return false;
            ++dots;
            digits = 0;
            octet = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            octet = octet * 10 + (c - '0');
            ++digits;
            if (digits > 3 || octet > 255) return false;
        } else {
            return false;
        }
    }
    return dots == 3 && digits > 0 && octet <= 255;
}

std::string inferred_scheme_if_host_url(std::string_view input) {
    if (input.empty()) return {};
    for (char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) return {};
    }

    size_t slash = input.find_first_of("/?#");
    std::string_view authority = (slash == std::string_view::npos) ? input : input.substr(0, slash);
    if (authority.empty()) return {};

    std::string_view host = authority;
    if (auto colon = authority.rfind(':'); colon != std::string_view::npos && authority.front() != '[') {
        std::string_view port = authority.substr(colon + 1);
        if (!port.empty() && std::all_of(port.begin(), port.end(), [](unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            host = authority.substr(0, colon);
        } else {
            return {};
        }
    }

    std::string lower_host = to_lower_ascii(host);
    if (lower_host == "localhost" || lower_host == "127.0.0.1") {
        return "http://";
    }
    if (is_ipv4_host(lower_host)) {
        return "https://";
    }

    auto dot = lower_host.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= lower_host.size()) {
        return {};
    }
    if (lower_host.find("..") != std::string::npos) return {};

    std::string_view tld = std::string_view(lower_host).substr(dot + 1);
    if (tld.size() >= 2 && std::all_of(tld.begin(), tld.end(), [](unsigned char ch) {
            return std::isalnum(ch) != 0;
        })) {
        return "https://";
    }
    return {};
}

} // namespace

std::string percent_encode(std::string_view input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(kHex[(c >> 4) & 0x0F]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string normalize_searxng_base(std::string_view raw) {
    std::string_view trimmed = trim_ascii(raw);
    if (trimmed.empty()) return {};
    std::string lower = to_lower_ascii(trimmed);
    if (lower == "duckduckgo" || lower == "ddg") {
        return "duckduckgo";
    }

    std::string url;
    if (starts_with_ci(trimmed, "http://") || starts_with_ci(trimmed, "https://")) {
        url = std::string(trimmed);
    } else if (std::string scheme = inferred_scheme_if_host_url(trimmed); !scheme.empty()) {
        url = scheme + std::string(trimmed);
    } else {
        url = "https://" + std::string(trimmed);
    }

    if (url.find("{query}") != std::string::npos) {
        return url;
    }

    while (url.size() > 8 && url.back() == '/') {
        url.pop_back();
    }
    constexpr std::string_view kSearchSuffix = "/search";
    if (url.size() > kSearchSuffix.size() &&
        url.compare(url.size() - kSearchSuffix.size(), kSearchSuffix.size(), kSearchSuffix) == 0) {
        url.erase(url.size() - kSearchSuffix.size());
    }
    return url;
}

std::string home_url_for_search_engine(std::string_view search_base_url) {
    std::string norm = normalize_searxng_base(search_base_url);
    if (norm.empty() || norm == "duckduckgo") {
        return std::string(kDefaultHomeUrl);
    }
    if (norm.find("{query}") != std::string::npos) {
        auto scheme_end = norm.find("://");
        if (scheme_end != std::string::npos) {
            auto path_start = norm.find('/', scheme_end + 3);
            return (path_start != std::string::npos) ? (norm.substr(0, path_start) + "/") : (norm + "/");
        }
    }
    return norm + "/";
}

std::string resolve_input(std::string_view raw, std::string_view search_base_url) {
    std::string_view trimmed = trim_ascii(raw);
    if (trimmed.empty()) {
        return home_url_for_search_engine(search_base_url);
    }

    if (starts_with_ci(trimmed, "http://") ||
        starts_with_ci(trimmed, "https://") ||
        starts_with_ci(trimmed, "file://") ||
        starts_with_ci(trimmed, "about:")) {
        return std::string(trimmed);
    }

    if (std::string scheme = inferred_scheme_if_host_url(trimmed); !scheme.empty()) {
        return scheme + std::string(trimmed);
    }

    std::string encoded = percent_encode(trimmed);
    std::string searx = normalize_searxng_base(search_base_url);
    if (!searx.empty() && searx != "duckduckgo") {
        auto pos = searx.find("{query}");
        if (pos != std::string::npos) {
            searx.replace(pos, 7, encoded);
            return searx;
        }
        return searx + "/search?q=" + encoded;
    }

    return "https://duckduckgo.com/?q=" + encoded;
}

std::string extract_host(std::string_view url) {
    std::string_view s = trim_ascii(url);
    auto scheme_pos = s.find("://");
    if (scheme_pos != std::string_view::npos) {
        s.remove_prefix(scheme_pos + 3);
    } else if (starts_with_ci(s, "about:")) {
        return {};
    }

    auto end = s.find_first_of("/?#");
    if (end != std::string_view::npos) {
        s = s.substr(0, end);
    }

    if (auto at = s.rfind('@'); at != std::string::npos) {
        s.remove_prefix(at + 1);
    }

    if (!s.empty() && s.front() == '[') {
        auto close = s.find(']');
        if (close != std::string_view::npos) {
            return to_lower_ascii(s.substr(1, close - 1));
        }
    }

    if (auto colon = s.find(':'); colon != std::string_view::npos) {
        s = s.substr(0, colon);
    }
    return to_lower_ascii(s);
}

std::string display_host(std::string_view url) {
    if (starts_with_ci(trim_ascii(url), "about:")) {
        return std::string(trim_ascii(url));
    }
    std::string host = extract_host(url);
    if (host.rfind("www.", 0) == 0 && host.size() > 4) {
        host.erase(0, 4);
    }
    return host.empty() ? std::string(url) : host;
}

} // namespace xnc
