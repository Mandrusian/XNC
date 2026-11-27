#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace snc {

struct Bookmark {
    std::string title;
    std::string url;
};

struct HistoryEntry {
    std::string title;
    std::string url;
    std::int64_t timestamp = 0;
};

/// Minimal file-backed bookmark, history, allowlist, and element-zapper store.
class BrowserStore {
public:
    static constexpr size_t kMaxHistoryEntries = 500;

    explicit BrowserStore(std::filesystem::path data_dir = default_data_dir());

    static std::filesystem::path default_data_dir();

    bool load();
    bool save() const;

    const std::vector<Bookmark>& bookmarks() const { return bookmarks_; }
    const std::vector<HistoryEntry>& history() const { return history_; }
    const std::vector<std::string>& allowlisted_hosts() const { return allowlisted_hosts_; }

    bool is_bookmarked(std::string_view url) const;
    void add_bookmark(std::string title, std::string url);
    void remove_bookmark(std::string_view url);
    /// Toggles bookmark state; returns true if bookmarked after toggling.
    bool toggle_bookmark(std::string title, std::string url);

    /// Records a visit unless `is_private` is true or `url` is empty/internal.
    void record_history(std::string title, std::string url, bool is_private, std::int64_t timestamp = 0);
    void clear_history();

    bool is_host_allowlisted(std::string_view host) const;
    /// Toggles whether `host` bypasses uBlock-style filtering; returns true if allowlisted after toggling.
    bool toggle_host_allowlist(std::string_view host);

    std::vector<std::string> custom_selectors(std::string_view host) const;
    void add_custom_selector(std::string_view host, std::string selector);
    void clear_custom_selectors(std::string_view host);

    const std::filesystem::path& data_dir() const { return data_dir_; }

private:
    std::filesystem::path data_dir_;
    std::vector<Bookmark> bookmarks_;
    std::vector<HistoryEntry> history_;
    std::vector<std::string> allowlisted_hosts_;
    std::map<std::string, std::vector<std::string>> custom_selectors_;
};

} // namespace snc
