#include "snc/storage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace snc {

namespace {

std::string escape_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

std::string unescape_field(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == '\\') { out.push_back('\\'); ++i; continue; }
            if (next == 't') { out.push_back('\t'); ++i; continue; }
            if (next == 'n') { out.push_back('\n'); ++i; continue; }
            if (next == 'r') { out.push_back('\r'); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = line.find('\t', start);
        if (pos == std::string::npos) {
            parts.push_back(unescape_field(std::string_view(line).substr(start)));
            break;
        }
        parts.push_back(unescape_field(std::string_view(line).substr(start, pos - start)));
        start = pos + 1;
    }
    return parts;
}

std::int64_t current_unix_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

BrowserStore::BrowserStore(std::filesystem::path data_dir)
    : data_dir_(std::move(data_dir)) {
    load();
}

std::filesystem::path BrowserStore::default_data_dir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return std::filesystem::path(appdata) / "SNC";
    }
    if (const char* home = std::getenv("USERPROFILE"); home && *home) {
        return std::filesystem::path(home) / "AppData" / "Roaming" / "SNC";
    }
    return std::filesystem::path("snc_data");
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "SNC";
    }
    return std::filesystem::path("snc_data");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "snc";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "snc";
    }
    return std::filesystem::path("snc_data");
#endif
}

bool BrowserStore::load() {
    bookmarks_.clear();
    history_.clear();
    std::filesystem::path file = data_dir_ / "store.tsv";
    std::ifstream in(file);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto cols = split_tabs(line);
        if (cols.empty()) continue;
        if (cols[0] == "B" && cols.size() >= 3) {
            bookmarks_.push_back(Bookmark{cols[1], cols[2]});
        } else if (cols[0] == "H" && cols.size() >= 4) {
            std::int64_t ts = 0;
            try { ts = std::stoll(cols[3]); } catch (...) { ts = 0; }
            history_.push_back(HistoryEntry{cols[1], cols[2], ts});
        }
    }
    return true;
}

bool BrowserStore::save() const {
    std::error_code ec;
    std::filesystem::create_directories(data_dir_, ec);
    if (ec) return false;

    std::filesystem::path file = data_dir_ / "store.tsv";
    std::ofstream out(file, std::ios::trunc);
    if (!out.is_open()) return false;

    for (const auto& b : bookmarks_) {
        out << "B\t" << escape_field(b.title) << "\t" << escape_field(b.url) << "\n";
    }
    for (const auto& h : history_) {
        out << "H\t" << escape_field(h.title) << "\t" << escape_field(h.url) << "\t" << h.timestamp << "\n";
    }
    return out.good();
}

bool BrowserStore::is_bookmarked(std::string_view url) const {
    return std::any_of(bookmarks_.begin(), bookmarks_.end(), [&](const Bookmark& b) {
        return b.url == url;
    });
}

void BrowserStore::add_bookmark(std::string title, std::string url) {
    if (url.empty()) return;
    if (title.empty()) title = url;
    for (auto& b : bookmarks_) {
        if (b.url == url) {
            b.title = std::move(title);
            save();
            return;
        }
    }
    bookmarks_.push_back(Bookmark{std::move(title), std::move(url)});
    save();
}

void BrowserStore::remove_bookmark(std::string_view url) {
    auto it = std::remove_if(bookmarks_.begin(), bookmarks_.end(), [&](const Bookmark& b) {
        return b.url == url;
    });
    if (it != bookmarks_.end()) {
        bookmarks_.erase(it, bookmarks_.end());
        save();
    }
}

bool BrowserStore::toggle_bookmark(std::string title, std::string url) {
    if (url.empty()) return false;
    if (is_bookmarked(url)) {
        remove_bookmark(url);
        return false;
    }
    add_bookmark(std::move(title), std::move(url));
    return true;
}

void BrowserStore::record_history(std::string title, std::string url, bool is_private, std::int64_t timestamp) {
    if (is_private || url.empty() || url.rfind("about:", 0) == 0) return;
    if (title.empty()) title = url;
    std::int64_t ts = (timestamp > 0) ? timestamp : current_unix_seconds();

    if (!history_.empty() && history_.front().url == url) {
        history_.front().title = std::move(title);
        history_.front().timestamp = ts;
        save();
        return;
    }

    history_.insert(history_.begin(), HistoryEntry{std::move(title), std::move(url), ts});
    if (history_.size() > kMaxHistoryEntries) {
        history_.resize(kMaxHistoryEntries);
    }
    save();
}

void BrowserStore::clear_history() {
    history_.clear();
    save();
}

} // namespace snc
