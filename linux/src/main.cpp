#include "snc/blocker.hpp"
#include "snc/storage.hpp"
#include "snc/url_resolver.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kFilterId = "snc-tracker-blocker";

struct BrowserApp;

struct Tab {
    BrowserApp* app = nullptr;
    GtkWidget* container = nullptr;
    WebKitWebView* webview = nullptr;
    GtkWidget* tab_box = nullptr;
    GtkWidget* tab_label = nullptr;
    GtkWidget* close_btn = nullptr;
    std::string title = "New Tab";
    std::string url;
    bool is_private = false;
    bool js_enabled = true;
    bool closed = false;
};

struct WeakTabRef {
    std::weak_ptr<Tab> weak;
};

template <typename Fn>
void connect_tab_signal(gpointer instance, const char* signal, Fn callback, const std::shared_ptr<Tab>& tab) {
    auto* ref = new WeakTabRef{tab};
    g_signal_connect_data(
        instance,
        signal,
        G_CALLBACK(callback),
        ref,
        [](gpointer p, GClosure*) { delete static_cast<WeakTabRef*>(p); },
        static_cast<GConnectFlags>(0));
}

std::shared_ptr<Tab> lock_tab(gpointer user_data) {
    if (!user_data) return nullptr;
    auto tab = static_cast<WeakTabRef*>(user_data)->weak.lock();
    if (!tab || tab->closed) return nullptr;
    return tab;
}

GtkWidget* create_managed_popup_menu() {
    GtkWidget* menu = gtk_menu_new();
    g_object_ref_sink(menu);
    g_signal_connect(menu, "selection-done", G_CALLBACK(+[](GtkMenuShell* shell, gpointer) {
        g_idle_add(+[](gpointer ptr) -> gboolean {
            GtkWidget* w = GTK_WIDGET(ptr);
            gtk_widget_destroy(w);
            g_object_unref(w);
            return G_SOURCE_REMOVE;
        }, shell);
    }), nullptr);
    return menu;
}

struct BrowserApp {
    GtkApplication* gtk_app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* header_bar = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* back_btn = nullptr;
    GtkWidget* forward_btn = nullptr;
    GtkWidget* reload_btn = nullptr;
    GtkWidget* shield_btn = nullptr;
    GtkWidget* zapper_btn = nullptr;
    GtkWidget* address_entry = nullptr;
    GtkWidget* bookmark_btn = nullptr;
    GtkWidget* dev_btn = nullptr;
    GtkWidget* menu_btn = nullptr;
    GtkWidget* progress_bar = nullptr;

    snc::BrowserStore store;
    WebKitUserContentFilterStore* filter_store = nullptr;
    WebKitUserContentFilter* compiled_filter = nullptr;
    bool blocker_enabled = true;
    std::vector<std::shared_ptr<Tab>> tabs;

    Tab* current_tab() {
        if (!notebook) return nullptr;
        int page = gtk_notebook_get_current_page(GTK_NOTEBOOK(notebook));
        if (page < 0) return nullptr;
        GtkWidget* widget = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page);
        if (!widget) return nullptr;
        auto* ref = static_cast<WeakTabRef*>(g_object_get_data(G_OBJECT(widget), "snc-tab"));
        if (!ref) return nullptr;
        auto tab = ref->weak.lock();
        return (tab && !tab->closed) ? tab.get() : nullptr;
    }
};

void run_js(WebKitWebView* view, const std::string& script) {
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(
        view, script.c_str(), static_cast<gssize>(script.size()),
        nullptr, nullptr, nullptr, nullptr, nullptr);
#else
    webkit_web_view_run_javascript(view, script.c_str(), nullptr, nullptr, nullptr);
#endif
}

std::string extract_json_string_field(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return {};
    std::string out;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            out.push_back(json[i + 1]);
            ++i;
            continue;
        }
        if (json[i] == '"') break;
        out.push_back(json[i]);
    }
    return out;
}

std::string build_fallback_blocklist_json() {
    snc::Blocker blocker;
    std::ostringstream out;
    out << "[\n";
    const auto& domains = blocker.domains();
    for (size_t i = 0; i < domains.size(); ++i) {
        out << "  {\"trigger\":{\"url-filter\":\"^https?://([^/]+\\\\.)?";
        for (char c : domains[i]) {
            if (c == '.') out << "\\\\.";
            else out << c;
        }
        out << "[:/]\",\"load-type\":[\"third-party\"]},\"action\":{\"type\":\"block\"}}";
        if (i + 1 < domains.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
    return out.str();
}

std::string load_text_asset(const std::string& filename, const std::string& fallback) {
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "shared" / filename,
        std::filesystem::current_path() / ".." / "shared" / filename,
    };
    for (const auto& p : candidates) {
        std::ifstream in(p);
        if (in.is_open()) {
            std::ostringstream buf;
            buf << in.rdbuf();
            std::string s = buf.str();
            if (!s.empty()) return s;
        }
    }
    return fallback;
}

void inject_cosmetic_rules(BrowserApp* app, Tab* tab) {
    if (!tab || tab->closed || !tab->webview) return;
    std::string host = snc::extract_host(tab->url);
    bool shield_active = app->blocker_enabled && !app->store.is_host_allowlisted(host);
    std::string cosmetic_css = load_text_asset("cosmetic_rules.css", std::string(snc::Blocker::default_cosmetic_css()));
    std::string combined = snc::Blocker::build_css_for_host(
        shield_active, app->store.custom_selectors(host), cosmetic_css);
    run_js(tab->webview, snc::Blocker::build_inject_css_script(combined));
}

void apply_filter_to_tab(BrowserApp* app, Tab* tab) {
    if (!tab || tab->closed || !tab->webview) return;
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(tab->webview);
    if (!ucm) return;
    webkit_user_content_manager_remove_filter_by_id(ucm, kFilterId);
    std::string host = snc::extract_host(tab->url);
    bool shield_active = app->blocker_enabled && !app->store.is_host_allowlisted(host);
    if (shield_active && app->compiled_filter) {
        webkit_user_content_manager_add_filter(ucm, app->compiled_filter);
    }
    inject_cosmetic_rules(app, tab);
}

void update_tab_label(Tab* tab) {
    if (!tab || tab->closed || !tab->tab_label) return;
    std::string prefix = tab->is_private ? "[Private] " : "";
    std::string short_title = tab->title.empty() ? (tab->is_private ? "Private Tab" : "New Tab") : tab->title;
    if (short_title.size() > 22) {
        short_title = short_title.substr(0, 21) + "...";
    }
    std::string label = prefix + short_title;
    gtk_label_set_text(GTK_LABEL(tab->tab_label), label.c_str());
}

void sync_toolbar(BrowserApp* app) {
    if (!app->window) return;
    Tab* tab = app->current_tab();
    if (!tab) return;

    std::string win_title = tab->is_private ? ("SNC (Private) — " + tab->title) : ("SNC — " + tab->title);
    gtk_window_set_title(GTK_WINDOW(app->window), win_title.c_str());
    if (app->header_bar) {
        gtk_header_bar_set_title(GTK_HEADER_BAR(app->header_bar), win_title.c_str());
    }

    gtk_widget_set_sensitive(app->back_btn, webkit_web_view_can_go_back(tab->webview));
    gtk_widget_set_sensitive(app->forward_btn, webkit_web_view_can_go_forward(tab->webview));

    bool loading = webkit_web_view_is_loading(tab->webview);
    gtk_button_set_label(GTK_BUTTON(app->reload_btn), loading ? "Stop" : "↻");
    if (loading) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar),
                                      webkit_web_view_get_estimated_load_progress(tab->webview));
        gtk_widget_show(app->progress_bar);
    } else {
        gtk_widget_hide(app->progress_bar);
    }

    std::string host = snc::extract_host(tab->url);
    bool paused = !host.empty() && app->store.is_host_allowlisted(host);
    gtk_button_set_label(GTK_BUTTON(app->shield_btn),
                         !app->blocker_enabled ? "uB: OFF" : (paused ? "uB: Paused" : "uB: ON"));

    bool starred = !tab->url.empty() && app->store.is_bookmarked(tab->url);
    gtk_button_set_label(GTK_BUTTON(app->bookmark_btn), starred ? "★" : "☆");

    if (!gtk_widget_has_focus(app->address_entry)) {
        gtk_entry_set_text(GTK_ENTRY(app->address_entry), tab->url.c_str());
    }
}

void add_tab(BrowserApp* app, const std::string& initial_url, bool is_private);

void close_tab(BrowserApp* app, Tab* tab) {
    if (!tab || tab->closed || !app->notebook) return;
    tab->closed = true;

    int page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->container);
    if (page >= 0) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    }

    app->tabs.erase(
        std::remove_if(app->tabs.begin(), app->tabs.end(),
                       [tab](const std::shared_ptr<Tab>& ptr) { return ptr.get() == tab; }),
        app->tabs.end());

    if (app->tabs.empty() && app->window) {
        gtk_window_close(GTK_WINDOW(app->window));
    } else {
        sync_toolbar(app);
    }
}

void on_close_tab_clicked(GtkButton*, gpointer user_data) {
    if (auto tab = lock_tab(user_data)) {
        close_tab(tab->app, tab.get());
    }
}

void on_title_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return;
    const char* t = webkit_web_view_get_title(tab->webview);
    if (t && *t) tab->title = t;
    update_tab_label(tab.get());
    if (tab.get() == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_uri_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return;
    const char* u = webkit_web_view_get_uri(tab->webview);
    if (u && *u) tab->url = u;
    apply_filter_to_tab(tab->app, tab.get());
    if (tab.get() == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_progress_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return;
    if (tab.get() == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return;
    if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED) {
        inject_cosmetic_rules(tab->app, tab.get());
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        const char* u = webkit_web_view_get_uri(tab->webview);
        const char* t = webkit_web_view_get_title(tab->webview);
        if (u && *u) tab->url = u;
        if (t && *t) tab->title = t;
        tab->app->store.record_history(tab->title, tab->url, tab->is_private);
        update_tab_label(tab.get());
    }
    if (tab.get() == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_zapper_message(WebKitUserContentManager*, WebKitJavascriptResult* js_res, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return;
    JSCValue* val = webkit_javascript_result_get_js_value(js_res);
    if (!val || !jsc_value_is_string(val)) return;
    char* raw = jsc_value_to_string(val);
    if (!raw) return;
    std::string json(raw);
    g_free(raw);

    std::string host = extract_json_string_field(json, "host");
    std::string selector = extract_json_string_field(json, "selector");
    if (!host.empty() && !selector.empty()) {
        tab->app->store.add_custom_selector(host, selector);
        inject_cosmetic_rules(tab->app, tab.get());
    }
}

GtkWidget* on_create_web_view(WebKitWebView*, WebKitNavigationAction* action, gpointer user_data) {
    auto tab = lock_tab(user_data);
    if (!tab) return nullptr;
    WebKitURIRequest* req = webkit_navigation_action_get_request(action);
    const char* uri = req ? webkit_uri_request_get_uri(req) : nullptr;
    add_tab(tab->app, uri ? uri : std::string(snc::kDefaultHomeUrl), tab->is_private);
    return nullptr;
}

void add_tab(BrowserApp* app, const std::string& initial_url, bool is_private) {
    auto tab = std::make_shared<Tab>();
    tab->app = app;
    tab->is_private = is_private;
    tab->title = is_private ? "Private Tab" : "New Tab";
    tab->url = initial_url.empty() ? std::string(snc::kDefaultHomeUrl) : initial_url;

    WebKitWebContext* ctx = is_private ? webkit_web_context_new_ephemeral() : webkit_web_context_get_default();
    GtkWidget* view_widget = webkit_web_view_new_with_context(ctx);
    if (is_private) {
        g_object_unref(ctx);
    }

    tab->webview = WEBKIT_WEB_VIEW(view_widget);
    WebKitSettings* settings = webkit_web_view_get_settings(tab->webview);
    webkit_settings_set_enable_developer_extras(settings, TRUE);

    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(tab->webview);
    webkit_user_content_manager_register_script_message_handler(ucm, "sncZapper");

    tab->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data_full(G_OBJECT(tab->container), "snc-tab", new WeakTabRef{tab},
                           [](gpointer p) { delete static_cast<WeakTabRef*>(p); });
    gtk_box_pack_start(GTK_BOX(tab->container), view_widget, TRUE, TRUE, 0);

    tab->tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    tab->tab_label = gtk_label_new(tab->title.c_str());
    tab->close_btn = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(tab->close_btn), GTK_RELIEF_NONE);
    gtk_box_pack_start(GTK_BOX(tab->tab_box), tab->tab_label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(tab->tab_box), tab->close_btn, FALSE, FALSE, 0);
    gtk_widget_show_all(tab->tab_box);
    gtk_widget_show_all(tab->container);

    Tab* raw_tab = tab.get();
    apply_filter_to_tab(app, raw_tab);

    connect_tab_signal(tab->close_btn, "clicked", on_close_tab_clicked, tab);
    connect_tab_signal(ucm, "script-message-received::sncZapper", on_zapper_message, tab);
    connect_tab_signal(tab->webview, "notify::title", on_title_changed, tab);
    connect_tab_signal(tab->webview, "notify::uri", on_uri_changed, tab);
    connect_tab_signal(tab->webview, "notify::estimated-load-progress", on_progress_changed, tab);
    connect_tab_signal(tab->webview, "load-changed", on_load_changed, tab);
    connect_tab_signal(tab->webview, "create", on_create_web_view, tab);

    app->tabs.push_back(std::move(tab));
    int page_idx = gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), raw_tab->container, raw_tab->tab_box);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), raw_tab->container, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_idx);

    webkit_web_view_load_uri(raw_tab->webview, raw_tab->url.c_str());
    sync_toolbar(app);
}

void on_back_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) webkit_web_view_go_back(tab->webview);
}

void on_forward_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) webkit_web_view_go_forward(tab->webview);
}

void on_reload_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) {
        if (webkit_web_view_is_loading(tab->webview)) webkit_web_view_stop_loading(tab->webview);
        else webkit_web_view_reload(tab->webview);
    }
}

void on_toggle_global_shield(GtkMenuItem*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    app->blocker_enabled = !app->blocker_enabled;
    for (auto& tab : app->tabs) {
        apply_filter_to_tab(app, tab.get());
    }
    sync_toolbar(app);
}

void on_toggle_site_allowlist(GtkMenuItem*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) {
        std::string host = snc::extract_host(tab->url);
        if (!host.empty()) {
            app->store.toggle_host_allowlist(host);
            apply_filter_to_tab(app, tab);
            sync_toolbar(app);
        }
    }
}

void on_clear_zapped_site(GtkMenuItem*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) {
        std::string host = snc::extract_host(tab->url);
        if (!host.empty()) {
            app->store.clear_custom_selectors(host);
            webkit_web_view_reload(tab->webview);
        }
    }
}

void trigger_zapper(BrowserApp* app) {
    if (auto* tab = app->current_tab()) {
        std::string script = load_text_asset("zapper.js", std::string(snc::Blocker::default_zapper_script()));
        run_js(tab->webview, script);
    }
}

void on_zapper_clicked(GtkButton*, gpointer user_data) {
    trigger_zapper(static_cast<BrowserApp*>(user_data));
}

void open_inspector(BrowserApp* app) {
    if (auto* tab = app->current_tab()) {
        WebKitWebInspector* inspector = webkit_web_view_get_inspector(tab->webview);
        if (inspector) webkit_web_inspector_show(inspector);
    }
}

void toggle_js_on_tab(BrowserApp* app) {
    if (auto* tab = app->current_tab()) {
        tab->js_enabled = !tab->js_enabled;
        WebKitSettings* settings = webkit_web_view_get_settings(tab->webview);
        webkit_settings_set_enable_javascript(settings, tab->js_enabled ? TRUE : FALSE);
        webkit_web_view_reload(tab->webview);
    }
}

void on_shield_btn_clicked(GtkButton* btn, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    GtkWidget* menu = create_managed_popup_menu();

    Tab* tab = app->current_tab();
    std::string host = tab ? snc::extract_host(tab->url) : "";
    bool paused = !host.empty() && app->store.is_host_allowlisted(host);

    if (!host.empty()) {
        std::string site_label = paused ? ("Resume uBlock on " + host) : ("Pause uBlock on " + host);
        GtkWidget* site_item = gtk_menu_item_new_with_label(site_label.c_str());
        g_signal_connect(site_item, "activate", G_CALLBACK(on_toggle_site_allowlist), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), site_item);
    }

    GtkWidget* global_item = gtk_menu_item_new_with_label(
        app->blocker_enabled ? "Disable Global Shield (Ctrl+Shift+B)" : "Enable Global Shield (Ctrl+Shift+B)");
    g_signal_connect(global_item, "activate", G_CALLBACK(on_toggle_global_shield), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), global_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget* zap_item = gtk_menu_item_new_with_label("Element Zapper (Ctrl+Shift+Z)");
    g_signal_connect_swapped(zap_item, "activate", G_CALLBACK(trigger_zapper), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), zap_item);

    if (!host.empty() && !app->store.custom_selectors(host).empty()) {
        std::string clr_label = "Clear Zapped Rules on " + host;
        GtkWidget* clr_item = gtk_menu_item_new_with_label(clr_label.c_str());
        g_signal_connect(clr_item, "activate", G_CALLBACK(on_clear_zapped_site), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), clr_item);
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(btn), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
}

void on_dev_btn_clicked(GtkButton* btn, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    GtkWidget* menu = create_managed_popup_menu();

    GtkWidget* insp = gtk_menu_item_new_with_label("Web Inspector (Ctrl+Shift+I / F12)");
    g_signal_connect_swapped(insp, "activate", G_CALLBACK(open_inspector), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), insp);

    GtkWidget* zap = gtk_menu_item_new_with_label("Element Zapper (Ctrl+Shift+Z)");
    g_signal_connect_swapped(zap, "activate", G_CALLBACK(trigger_zapper), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), zap);

    Tab* tab = app->current_tab();
    GtkWidget* js_item = gtk_menu_item_new_with_label(
        (tab && !tab->js_enabled) ? "Enable JavaScript on Tab" : "Disable JavaScript on Tab");
    g_signal_connect_swapped(js_item, "activate", G_CALLBACK(toggle_js_on_tab), app);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), js_item);

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(btn), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
}

void on_address_activate(GtkEntry* entry, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    Tab* tab = app->current_tab();
    if (!tab) return;
    std::string target = snc::resolve_input(gtk_entry_get_text(entry));
    tab->url = target;
    webkit_web_view_load_uri(tab->webview, target.c_str());
    gtk_widget_grab_focus(GTK_WIDGET(tab->webview));
}

void on_bookmark_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (auto* tab = app->current_tab()) {
        app->store.toggle_bookmark(tab->title, tab->url);
        sync_toolbar(app);
    }
}

void on_new_tab_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    add_tab(app, std::string(snc::kDefaultHomeUrl), false);
}

void on_new_private_tab_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    add_tab(app, std::string(snc::kDefaultHomeUrl), true);
}

struct MenuTarget {
    BrowserApp* app;
    std::string url;
};

void on_menu_url_activate(GtkMenuItem*, gpointer user_data) {
    auto* target = static_cast<MenuTarget*>(user_data);
    if (auto* tab = target->app->current_tab()) {
        tab->url = target->url;
        webkit_web_view_load_uri(tab->webview, target->url.c_str());
    }
}

void on_clear_history_activate(GtkMenuItem*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    app->store.clear_history();
}

void on_menu_btn_clicked(GtkButton* btn, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    GtkWidget* menu = create_managed_popup_menu();

    GtkWidget* b_hdr = gtk_menu_item_new_with_label("Bookmarks");
    gtk_widget_set_sensitive(b_hdr, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), b_hdr);

    for (const auto& b : app->store.bookmarks()) {
        std::string text = "  " + b.title;
        GtkWidget* item = gtk_menu_item_new_with_label(text.c_str());
        auto* target = new MenuTarget{app, b.url};
        g_signal_connect_data(item, "activate", G_CALLBACK(on_menu_url_activate), target,
                              [](gpointer p, GClosure*) { delete static_cast<MenuTarget*>(p); },
                              static_cast<GConnectFlags>(0));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget* h_hdr = gtk_menu_item_new_with_label("Recent History");
    gtk_widget_set_sensitive(h_hdr, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), h_hdr);

    size_t count = 0;
    for (const auto& h : app->store.history()) {
        if (count++ >= 15) break;
        std::string text = "  " + h.title;
        GtkWidget* item = gtk_menu_item_new_with_label(text.c_str());
        auto* target = new MenuTarget{app, h.url};
        g_signal_connect_data(item, "activate", G_CALLBACK(on_menu_url_activate), target,
                              [](gpointer p, GClosure*) { delete static_cast<MenuTarget*>(p); },
                              static_cast<GConnectFlags>(0));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    if (!app->store.history().empty()) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        GtkWidget* clear_item = gtk_menu_item_new_with_label("Clear History");
        g_signal_connect(clear_item, "activate", G_CALLBACK(on_clear_history_activate), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), clear_item);
    }

    gtk_widget_show_all(menu);
    gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(btn), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, nullptr);
}

void on_switch_page(GtkNotebook*, GtkWidget*, guint, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    sync_toolbar(app);
}

gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    guint mods = event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK);
    guint key = gdk_keyval_to_lower(event->keyval);

    if (event->keyval == GDK_KEY_F12) {
        open_inspector(app);
        return TRUE;
    }
    if (mods == GDK_CONTROL_MASK) {
        if (key == GDK_KEY_t) { add_tab(app, std::string(snc::kDefaultHomeUrl), false); return TRUE; }
        if (key == GDK_KEY_w) { close_tab(app, app->current_tab()); return TRUE; }
        if (key == GDK_KEY_l) {
            gtk_widget_grab_focus(app->address_entry);
            gtk_editable_select_region(GTK_EDITABLE(app->address_entry), 0, -1);
            return TRUE;
        }
        if (key == GDK_KEY_r) { on_reload_clicked(nullptr, app); return TRUE; }
        if (key == GDK_KEY_d) { on_bookmark_clicked(nullptr, app); return TRUE; }
    } else if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (key == GDK_KEY_n) { add_tab(app, std::string(snc::kDefaultHomeUrl), true); return TRUE; }
        if (key == GDK_KEY_b) { on_toggle_global_shield(nullptr, app); return TRUE; }
        if (key == GDK_KEY_z) { trigger_zapper(app); return TRUE; }
        if (key == GDK_KEY_i) { open_inspector(app); return TRUE; }
    } else if (mods == GDK_MOD1_MASK) {
        if (event->keyval == GDK_KEY_Left) { on_back_clicked(nullptr, app); return TRUE; }
        if (event->keyval == GDK_KEY_Right) { on_forward_clicked(nullptr, app); return TRUE; }
    }
    return FALSE;
}

void on_window_destroy(GtkWidget*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    for (auto& tab : app->tabs) {
        tab->closed = true;
    }
    app->tabs.clear();
    app->notebook = nullptr;
    app->window = nullptr;
}

void on_filter_saved(GObject* source, GAsyncResult* res, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    GError* err = nullptr;
    WebKitUserContentFilter* filter =
        webkit_user_content_filter_store_save_finish(WEBKIT_USER_CONTENT_FILTER_STORE(source), res, &err);
    if (err) {
        g_error_free(err);
        return;
    }
    if (app->compiled_filter) {
        webkit_user_content_filter_unref(app->compiled_filter);
    }
    app->compiled_filter = filter;
    if (!app->window) return;
    for (auto& tab : app->tabs) {
        apply_filter_to_tab(app, tab.get());
    }
}

void activate(GtkApplication* gtk_app, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    if (app->window) {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }
    app->gtk_app = gtk_app;

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "headerbar { min-height: 40px; padding: 2px 6px; }"
        "entry#snc-address { border-radius: 8px; min-height: 28px; }"
        "button.flat-tool { border-radius: 6px; padding: 2px 7px; }",
        -1, nullptr);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1160, 760);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);

    app->header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(app->header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(app->header_bar), "SNC");

    app->back_btn = gtk_button_new_with_label("◀");
    app->forward_btn = gtk_button_new_with_label("▶");
    app->reload_btn = gtk_button_new_with_label("↻");
    app->shield_btn = gtk_button_new_with_label("uB: ON");
    app->zapper_btn = gtk_button_new_with_label("⚡");
    app->address_entry = gtk_entry_new();
    gtk_widget_set_name(app->address_entry, "snc-address");
    gtk_entry_set_width_chars(GTK_ENTRY(app->address_entry), 44);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->address_entry), "Search DuckDuckGo or enter URL");
    app->bookmark_btn = gtk_button_new_with_label("☆");
    GtkWidget* new_tab_btn = gtk_button_new_with_label("+");
    GtkWidget* private_btn = gtk_button_new_with_label("Priv");
    app->dev_btn = gtk_button_new_with_label("Dev");
    app->menu_btn = gtk_button_new_with_label("☰");

    gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header_bar), app->back_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header_bar), app->forward_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header_bar), app->reload_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header_bar), app->shield_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(app->header_bar), app->zapper_btn);
    gtk_header_bar_set_custom_title(GTK_HEADER_BAR(app->header_bar), app->address_entry);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header_bar), app->menu_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header_bar), app->dev_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header_bar), private_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header_bar), new_tab_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(app->header_bar), app->bookmark_btn);

    gtk_window_set_titlebar(GTK_WINDOW(app->window), app->header_bar);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    app->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(app->progress_bar, TRUE);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);

    gtk_box_pack_start(GTK_BOX(root), app->progress_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app->notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    g_signal_connect(app->back_btn, "clicked", G_CALLBACK(on_back_clicked), app);
    g_signal_connect(app->forward_btn, "clicked", G_CALLBACK(on_forward_clicked), app);
    g_signal_connect(app->reload_btn, "clicked", G_CALLBACK(on_reload_clicked), app);
    g_signal_connect(app->shield_btn, "clicked", G_CALLBACK(on_shield_btn_clicked), app);
    g_signal_connect(app->zapper_btn, "clicked", G_CALLBACK(on_zapper_clicked), app);
    g_signal_connect(app->address_entry, "activate", G_CALLBACK(on_address_activate), app);
    g_signal_connect(app->bookmark_btn, "clicked", G_CALLBACK(on_bookmark_clicked), app);
    g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), app);
    g_signal_connect(private_btn, "clicked", G_CALLBACK(on_new_private_tab_clicked), app);
    g_signal_connect(app->dev_btn, "clicked", G_CALLBACK(on_dev_btn_clicked), app);
    g_signal_connect(app->menu_btn, "clicked", G_CALLBACK(on_menu_btn_clicked), app);
    g_signal_connect_after(app->notebook, "switch-page", G_CALLBACK(on_switch_page), app);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);

    if (!app->filter_store) {
        auto filter_dir = app->store.data_dir() / "filters";
        std::error_code ec;
        std::filesystem::create_directories(filter_dir, ec);
        app->filter_store = webkit_user_content_filter_store_new(filter_dir.c_str());
    }

    std::string json = load_text_asset("blocklist.json", build_fallback_blocklist_json());
    GBytes* bytes = g_bytes_new(json.data(), json.size());
    webkit_user_content_filter_store_save(app->filter_store, kFilterId, bytes, nullptr, on_filter_saved, app);
    g_bytes_unref(bytes);

    add_tab(app, std::string(snc::kDefaultHomeUrl), false);
    gtk_widget_show_all(app->window);
}

} // namespace

int main(int argc, char** argv) {
    BrowserApp app;
#if GLIB_CHECK_VERSION(2, 74, 0)
    GtkApplication* gtk_app = gtk_application_new("com.mandrusian.snc", G_APPLICATION_DEFAULT_FLAGS);
#else
    GtkApplication* gtk_app = gtk_application_new("com.mandrusian.snc", G_APPLICATION_FLAGS_NONE);
#endif
    g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    if (app.compiled_filter) webkit_user_content_filter_unref(app.compiled_filter);
    if (app.filter_store) g_object_unref(app.filter_store);
    g_object_unref(gtk_app);
    return status;
}
