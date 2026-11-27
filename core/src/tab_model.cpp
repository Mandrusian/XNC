#include "xnc/tab_model.hpp"

namespace xnc {

std::uint64_t TabModel::open_tab(std::string url, bool is_private, std::string title) {
    if (title.empty()) {
        title = is_private ? "Private Tab" : "New Tab";
    }
    std::uint64_t id = next_id_++;
    tabs_.push_back(TabInfo{id, std::move(title), std::move(url), is_private});
    active_index_ = tabs_.size() - 1;
    return id;
}

bool TabModel::close_tab(std::uint64_t id) {
    auto idx_opt = index_of(id);
    if (!idx_opt) return false;
    size_t idx = *idx_opt;
    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(idx));

    if (tabs_.empty()) {
        active_index_.reset();
    } else if (active_index_) {
        if (*active_index_ >= tabs_.size()) {
            active_index_ = tabs_.size() - 1;
        } else if (idx < *active_index_) {
            --(*active_index_);
        }
    }
    return true;
}

bool TabModel::select_index(size_t index) {
    if (index >= tabs_.size()) return false;
    active_index_ = index;
    return true;
}

bool TabModel::select_id(std::uint64_t id) {
    auto idx = index_of(id);
    if (!idx) return false;
    active_index_ = *idx;
    return true;
}

void TabModel::select_next() {
    if (tabs_.empty() || !active_index_) return;
    active_index_ = (*active_index_ + 1) % tabs_.size();
}

void TabModel::select_prev() {
    if (tabs_.empty() || !active_index_) return;
    active_index_ = (*active_index_ == 0) ? (tabs_.size() - 1) : (*active_index_ - 1);
}

bool TabModel::update_tab(std::uint64_t id, std::string title, std::string url) {
    auto idx = index_of(id);
    if (!idx) return false;
    if (!title.empty()) tabs_[*idx].title = std::move(title);
    if (!url.empty()) tabs_[*idx].url = std::move(url);
    return true;
}

const TabInfo* TabModel::active_tab() const {
    if (!active_index_ || *active_index_ >= tabs_.size()) return nullptr;
    return &tabs_[*active_index_];
}

std::optional<size_t> TabModel::index_of(std::uint64_t id) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i].id == id) return i;
    }
    return std::nullopt;
}

} // namespace xnc
