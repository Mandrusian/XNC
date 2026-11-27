#include "xnc/blocker.hpp"
#include "xnc/storage.hpp"
#include "xnc/tab_model.hpp"
#include "xnc/url_resolver.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#define XNC_CHECK(cond)                                                     \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::cerr << "CHECK FAILED at " << __FILE__ << ":" << __LINE__  \
                      << " -> " << #cond << "\n";                           \
            std::exit(1);                                                   \
        }                                                                   \
    } while (false)

namespace {

void test_url_resolver() {
    XNC_CHECK(xnc::resolve_input("") == "https://duckduckgo.com/");
    XNC_CHECK(xnc::resolve_input("   ") == "https://duckduckgo.com/");
    XNC_CHECK(xnc::resolve_input("https://example.com/a") == "https://example.com/a");
    XNC_CHECK(xnc::resolve_input("http://localhost:3000") == "http://localhost:3000");
    XNC_CHECK(xnc::resolve_input("example.org") == "https://example.org");
    XNC_CHECK(xnc::resolve_input("localhost:8080/test") == "http://localhost:8080/test");
    XNC_CHECK(xnc::resolve_input("127.0.0.1:9000") == "http://127.0.0.1:9000");
    XNC_CHECK(xnc::resolve_input("127.0.0.1.example.com") == "https://127.0.0.1.example.com");
    XNC_CHECK(xnc::resolve_input("about:blank") == "about:blank");
    XNC_CHECK(xnc::resolve_input("webkit browser") == "https://duckduckgo.com/?q=webkit+browser");
    XNC_CHECK(xnc::resolve_input("c++20 & cmake") == "https://duckduckgo.com/?q=c%2B%2B20+%26+cmake");

    // Custom SearXNG support
    XNC_CHECK(xnc::normalize_searxng_base("searx.example.com/") == "https://searx.example.com");
    XNC_CHECK(xnc::normalize_searxng_base("http://localhost:8080/search") == "http://localhost:8080");
    XNC_CHECK(xnc::resolve_input("", "searx.example.com") == "https://searx.example.com/");
    XNC_CHECK(xnc::resolve_input("webkit mica", "https://searx.example.com") ==
              "https://searx.example.com/search?q=webkit+mica");
    XNC_CHECK(xnc::resolve_input("open source", "http://localhost:8888/?q={query}") ==
              "http://localhost:8888/?q=open+source");

    XNC_CHECK(xnc::extract_host("https://Sub.Example.com:8443/path") == "sub.example.com");
    XNC_CHECK(xnc::display_host("https://www.wikipedia.org/wiki/WebKit") == "wikipedia.org");
}

void test_blocker() {
    xnc::Blocker blocker;
    XNC_CHECK(blocker.enabled());
    XNC_CHECK(blocker.should_block("https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js", "news.ycombinator.com"));
    XNC_CHECK(blocker.should_block("https://doubleclick.net/pixel", "example.com"));
    XNC_CHECK(!blocker.should_block("https://doubleclick.net/pixel", "doubleclick.net"));
    XNC_CHECK(!blocker.should_block("https://example.com/app.js", "example.com"));

    blocker.set_enabled(false);
    XNC_CHECK(!blocker.should_block("https://doubleclick.net/pixel", "example.com"));

    XNC_CHECK(xnc::Blocker::build_transparent_mica_script(true).find("__xnc-transparent-mica") != std::string::npos);
    XNC_CHECK(xnc::Blocker::build_transparent_mica_script(false).find("s.remove()") != std::string::npos);
}

void test_storage() {
    auto tmp_dir = std::filesystem::temp_directory_path() /
                   ("xnc_core_test_" + std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(tmp_dir);

    {
        xnc::BrowserStore store(tmp_dir);
        XNC_CHECK(store.toggle_bookmark("SearXNG\tHome", "https://searx.example.com/"));
        XNC_CHECK(store.is_bookmarked("https://searx.example.com/"));
        store.record_history("Private Page", "https://secret.example.com", true, 100);
        XNC_CHECK(store.history().empty());
        store.record_history("Example", "https://example.com", false, 200);
        store.record_history("Example Updated", "https://example.com", false, 205);
        XNC_CHECK(store.history().size() == 1);
        XNC_CHECK(store.history().front().title == "Example Updated");

        XNC_CHECK(store.toggle_host_allowlist("Example.com"));
        XNC_CHECK(store.is_host_allowlisted("example.com"));
        store.add_custom_selector("news.ycombinator.com", ".ad-banner");
        store.add_custom_selector("news.ycombinator.com", ".ad-banner");
        XNC_CHECK(store.custom_selectors("news.ycombinator.com").size() == 1);

        store.set_search_engine_url("https://searx.example.com/");
        store.set_transparent_websites(true);
    }

    {
        xnc::BrowserStore reloaded(tmp_dir);
        XNC_CHECK(reloaded.is_bookmarked("https://searx.example.com/"));
        XNC_CHECK(reloaded.bookmarks().front().title == "SearXNG\tHome");
        XNC_CHECK(reloaded.history().size() == 1);
        XNC_CHECK(reloaded.history().front().timestamp == 205);
        XNC_CHECK(reloaded.is_host_allowlisted("example.com"));
        XNC_CHECK(reloaded.search_engine_url() == "https://searx.example.com");
        XNC_CHECK(reloaded.transparent_websites());
        XNC_CHECK(!reloaded.toggle_host_allowlist("example.com"));
        XNC_CHECK(!reloaded.is_host_allowlisted("example.com"));
        XNC_CHECK(reloaded.custom_selectors("news.ycombinator.com").size() == 1);
        reloaded.clear_custom_selectors("news.ycombinator.com");
        XNC_CHECK(reloaded.custom_selectors("news.ycombinator.com").empty());
        XNC_CHECK(!reloaded.toggle_bookmark("SearXNG\tHome", "https://searx.example.com/"));
        XNC_CHECK(!reloaded.is_bookmarked("https://searx.example.com/"));
    }

    auto css = xnc::Blocker::build_css_for_host(true, {"#annoying"});
    XNC_CHECK(css.find("adsbygoogle") != std::string::npos);
    XNC_CHECK(css.find("#annoying { display: none !important; }") != std::string::npos);
    XNC_CHECK(!xnc::Blocker::default_zapper_script().empty());

    std::filesystem::remove_all(tmp_dir);
}

void test_tab_model() {
    xnc::TabModel model;
    auto t1 = model.open_tab("https://example.com", false);
    auto t2 = model.open_tab("https://duckduckgo.com/", true);
    auto t3 = model.open_tab("https://webkit.org/", false);
    XNC_CHECK(model.tabs().size() == 3);
    XNC_CHECK(model.active_tab() && model.active_tab()->id == t3);

    model.select_next();
    XNC_CHECK(model.active_tab() && model.active_tab()->id == t1);
    model.select_prev();
    XNC_CHECK(model.active_tab() && model.active_tab()->id == t3);

    XNC_CHECK(model.select_id(t2));
    XNC_CHECK(model.active_tab() && model.active_tab()->is_private);
    XNC_CHECK(model.close_tab(t2));
    XNC_CHECK(model.tabs().size() == 2);
    XNC_CHECK(model.active_tab() && model.active_tab()->id == t3);
}

} // namespace

int main() {
    test_url_resolver();
    test_blocker();
    test_storage();
    test_tab_model();
    std::cout << "All xnc_core tests passed.\n";
    return 0;
}
