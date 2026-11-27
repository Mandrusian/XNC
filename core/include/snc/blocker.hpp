#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace snc {

/// Lightweight domain-based ad and tracker blocker.
class Blocker {
public:
    Blocker();

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    /// Loads newline-separated domains from a file; falls back to built-in rules if missing.
    bool load_domains_file(const std::filesystem::path& path);

    /// Replaces the active domain list with the provided domains.
    void set_domains(std::vector<std::string> domains);

    const std::vector<std::string>& domains() const { return domains_; }

    /// Returns true if `request_url` matches a blocked domain (and is third-party when `page_host` is non-empty).
    bool should_block(std::string_view request_url, std::string_view page_host = {}) const;

private:
    bool enabled_ = true;
    std::vector<std::string> domains_;
};

} // namespace snc
