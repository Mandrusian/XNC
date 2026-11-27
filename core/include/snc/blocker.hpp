#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace snc {

/// Lightweight uBlock-style domain blocker, cosmetic CSS, and zapper payload helpers.
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

    /// Built-in cosmetic CSS rules hiding common ad slots and consent banners.
    static std::string_view default_cosmetic_css();

    /// Built-in interactive element zapper & picker script.
    static std::string_view default_zapper_script();

    /// Builds combined CSS string for a host given global cosmetic rules + zapped selectors.
    static std::string build_css_for_host(
        bool shield_active,
        const std::vector<std::string>& custom_selectors,
        std::string_view cosmetic_css = default_cosmetic_css());

    /// Builds a tiny JS snippet that injects a `<style>` element with `css`.
    static std::string build_inject_css_script(std::string_view css);

private:
    bool enabled_ = true;
    std::vector<std::string> domains_;
};

} // namespace snc
