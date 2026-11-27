#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace xnc {

struct TabInfo {
    std::uint64_t id = 0;
    std::string title = "New Tab";
    std::string url;
    bool is_private = false;
};

/// Platform-independent tab list state machine.
class TabModel {
public:
    std::uint64_t open_tab(std::string url = {}, bool is_private = false, std::string title = {});
    bool close_tab(std::uint64_t id);
    bool select_index(size_t index);
    bool select_id(std::uint64_t id);
    void select_next();
    void select_prev();

    bool update_tab(std::uint64_t id, std::string title, std::string url);

    const std::vector<TabInfo>& tabs() const { return tabs_; }
    std::optional<size_t> active_index() const { return active_index_; }
    const TabInfo* active_tab() const;
    std::optional<size_t> index_of(std::uint64_t id) const;

private:
    std::uint64_t next_id_ = 1;
    std::vector<TabInfo> tabs_;
    std::optional<size_t> active_index_;
};

} // namespace xnc
