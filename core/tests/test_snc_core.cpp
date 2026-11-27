#include "snc/blocker.hpp"
#include "snc/storage.hpp"
#include "snc/tab_model.hpp"
#include "snc/url_resolver.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#define SNC_CHECK(cond)                                                     \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__  \
                      << " -> " << #cond << "\n";                           \
            std::exit(1);                                                   \
        }                                                                   \
    } while (false)

namespace {

void test_url_resolver() {
    SNC_CHECK(snc::resolve_input("") == "https://duckduckgo.com/");
    SNC_CHECK(snc::resolve_input("   ") == "https://duckduckgo.com/");
    SNC_CHECK(snc::resolve_input("https://example.com/a") == "https://example.com/a");
    SNC_CHECK(snc::resolve_input("http://localhost:3000") == "http://localhost:3000");
    SNC_CHECK(snc::resolve_input("example.org") == "https://example.org");
    SNC_CHECK(snc::resolve_input("localhost:8080/test") == "http://localhost:8080/test");
    SNC_CHECK(snc::resolve_input("127.0.0.1:9000") == "http://127.0.0.1:9000");
    SNC_CHECK(snc::resolve_input("127.0.0.1.example.com") == "https://127.0.0.1.example.com");
    SNC_CHECK(snc::resolve_input("about:blank") == "about:blank");
    SNC_CHECK(snc::resolve_input("webkit browser") == "https://duckduckgo.com/?q=webkit+browser");
    SNC_CHECK(snc::resolve_input("c++20 & cmake") == "https://duckduckgo.com/?q=c%2B%2B20+%26+cmake");

    SNC_CHECK(snc::extract_host("https://Sub.Example.com:8443/path") == "sub.example.com");
    SNC_CHECK(snc::display_host("https://www.wikipedia.org/wiki/WebKit") == "wikipedia.org");
}

void test_blocker() {
    snc::Blocker blocker;
    SNC_CHECK(blocker.enabled());
    SNC_CHECK(blocker.should_block("https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js", "news.ycombinator.com"));
    SNC_CHECK(blocker.should_block("https://doubleclick.net/pixel", "example.com"));
    SNC_CHECK(!blocker.should_block("https://doubleclick.net/pixel", "doubleclick.net"));
    SNC_CHECK(!blocker.should_block("https://example.com/app.js", "example.com"));

    blocker.set_enabled(false);
    SNC_CHECK(!blocker.should_block("https://doubleclick.net/pixel", "example.com"));
}

void test_storage() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("snc_core_test_" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(tmp_dir);

    {
        snc::BrowserStore store(tmp_dir);
        SNC_CHECK(store.toggle_bookmark("DuckDuckGo\tHome", "https://duckduckgo.com/"));
        SNC_CHECK(store.is_bookmarked("https://duckduckgo.com/"));
        store.record_history("Private Page", "https://secret.example.com", true, 100);
        SNC_CHECK(store.history().empty());
        store.record_history("Example", "https://example.com", false, 200);
        store.record_history("Example Updated", "https://example.com", false, 205);
        SNC_CHECK(store.history().size() == 1);
        SNC_CHECK(store.history().front().title == "Example Updated");
    }

    {
        snc::BrowserStore reloaded(tmp_dir);
        SNC_CHECK(reloaded.is_bookmarked("https://duckduckgo.com/"));
        SNC_CHECK(reloaded.bookmarks().front().title == "DuckDuckGo\tHome");
        SNC_CHECK(reloaded.history().size() == 1);
        SNC_CHECK(reloaded.history().front().timestamp == 205);
        SNC_CHECK(!reloaded.toggle_bookmark("DuckDuckGo\tHome", "https://duckduckgo.com/"));
        SNC_CHECK(!reloaded.is_bookmarked("https://duckduckgo.com/"));
    }

    std::filesystem::remove_all(tmp_dir);
}

void test_tab_model() {
    snc::TabModel model;
    auto t1 = model.open_tab("https://example.com", false);
    auto t2 = model.open_tab("https://duckduckgo.com/", true);
    auto t3 = model.open_tab("https://webkit.org/", false);
    SNC_CHECK(model.tabs().size() == 3);
    SNC_CHECK(model.active_tab() && model.active_tab()->id == t3);

    model.select_next();
    SNC_CHECK(model.active_tab() && model.active_tab()->id == t1);
    model.select_prev();
    SNC_CHECK(model.active_tab() && model.active_tab()->id == t3);

    SNC_CHECK(model.select_id(t2));
    SNC_CHECK(model.active_tab() && model.active_tab()->is_private);
    SNC_CHECK(model.close_tab(t2));
    SNC_CHECK(model.tabs().size() == 2);
    SNC_CHECK(model.active_tab() && model.active_tab()->id == t3);
}

} // namespace

int main() {
    test_url_resolver();
    test_blocker();
    test_storage();
    test_tab_model();
    std::cout << "All snc_core tests passed.\n";
    return 0;
}
