#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace xnc {

/// Lightweight uBlock-style domain blocker, cosmetic CSS, zapper, and transparent Mica helpers.
class Blocker {
public:
    Blocker();

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    bool load_domains_file(const std::filesystem::path& path);
    void set_domains(std::vector<std::string> domains);
    const std::vector<std::string>& domains() const { return domains_; }

    bool should_block(std::string_view request_url, std::string_view page_host = {}) const;

    static std::string_view default_cosmetic_css();
    static std::string_view default_zapper_script();
    static std::string_view default_transparent_mica_css();

    static std::string build_css_for_host(
        bool shield_active,
        const std::vector<std::string>& custom_selectors,
        std::string_view cosmetic_css = default_cosmetic_css());

    static std::string build_inject_css_script(std::string_view css);
    static std::string build_transparent_mica_script(bool enabled);

private:
    bool enabled_ = true;
    std::vector<std::string> domains_;
};

} // namespace xnc
