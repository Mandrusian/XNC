#include "snc/blocker.hpp"
#include "snc/url_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

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
        "openx.net",
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
        return false; // Allow first-party requests
    }

    for (const auto& domain : domains_) {
        if (host_matches_domain(req_host, domain)) {
            return true;
        }
    }
    return false;
}

} // namespace snc
