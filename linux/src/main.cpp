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
    bool closed = false;
};

struct BrowserApp {
    GtkApplication* gtk_app = nullptr;
    GtkWidget* window = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* back_btn = nullptr;
    GtkWidget* forward_btn = nullptr;
    GtkWidget* reload_btn = nullptr;
    GtkWidget* shield_btn = nullptr;
    GtkWidget* address_entry = nullptr;
    GtkWidget* bookmark_btn = nullptr;
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
        auto* tab = static_cast<Tab*>(g_object_get_data(G_OBJECT(widget), "snc-tab"));
        return (tab && !tab->closed) ? tab : nullptr;
    }
};

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

std::string load_blocklist_json() {
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "shared" / "blocklist.json",
        std::filesystem::current_path() / ".." / "shared" / "blocklist.json",
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
    return build_fallback_blocklist_json();
}

void apply_filter_to_tab(BrowserApp* app, Tab* tab) {
    if (!tab || tab->closed || !tab->webview) return;
    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(tab->webview);
    if (!ucm) return;
    webkit_user_content_manager_remove_filter_by_id(ucm, kFilterId);
    if (app->blocker_enabled && app->compiled_filter) {
        webkit_user_content_manager_add_filter(ucm, app->compiled_filter);
    }
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

    gtk_widget_set_sensitive(app->back_btn, webkit_web_view_can_go_back(tab->webview));
    gtk_widget_set_sensitive(app->forward_btn, webkit_web_view_can_go_forward(tab->webview));

    bool loading = webkit_web_view_is_loading(tab->webview);
    gtk_button_set_label(GTK_BUTTON(app->reload_btn), loading ? "Stop" : "Reload");
    if (loading) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress_bar),
                                      webkit_web_view_get_estimated_load_progress(tab->webview));
        gtk_widget_show(app->progress_bar);
    } else {
        gtk_widget_hide(app->progress_bar);
    }

    gtk_button_set_label(GTK_BUTTON(app->shield_btn), app->blocker_enabled ? "Shield: ON" : "Shield: OFF");

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
    auto* tab = static_cast<Tab*>(user_data);
    close_tab(tab->app, tab);
}

void on_title_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto* tab = static_cast<Tab*>(user_data);
    if (tab->closed) return;
    const char* t = webkit_web_view_get_title(tab->webview);
    if (t && *t) tab->title = t;
    update_tab_label(tab);
    if (tab == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_uri_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto* tab = static_cast<Tab*>(user_data);
    if (tab->closed) return;
    const char* u = webkit_web_view_get_uri(tab->webview);
    if (u && *u) tab->url = u;
    if (tab == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_progress_changed(GObject*, GParamSpec*, gpointer user_data) {
    auto* tab = static_cast<Tab*>(user_data);
    if (tab->closed) return;
    if (tab == tab->app->current_tab()) sync_toolbar(tab->app);
}

void on_load_changed(WebKitWebView*, WebKitLoadEvent event, gpointer user_data) {
    auto* tab = static_cast<Tab*>(user_data);
    if (tab->closed) return;
    if (event == WEBKIT_LOAD_FINISHED) {
        const char* u = webkit_web_view_get_uri(tab->webview);
        const char* t = webkit_web_view_get_title(tab->webview);
        if (u && *u) tab->url = u;
        if (t && *t) tab->title = t;
        tab->app->store.record_history(tab->title, tab->url, tab->is_private);
        update_tab_label(tab);
    }
    if (tab == tab->app->current_tab()) sync_toolbar(tab->app);
}

GtkWidget* on_create_web_view(WebKitWebView*, WebKitNavigationAction* action, gpointer user_data) {
    auto* tab = static_cast<Tab*>(user_data);
    if (tab->closed) return nullptr;
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
    tab->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    g_object_set_data(G_OBJECT(tab->container), "snc-tab", tab.get());
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

    g_signal_connect(tab->close_btn, "clicked", G_CALLBACK(on_close_tab_clicked), raw_tab);
    g_signal_connect(tab->webview, "notify::title", G_CALLBACK(on_title_changed), raw_tab);
    g_signal_connect(tab->webview, "notify::uri", G_CALLBACK(on_uri_changed), raw_tab);
    g_signal_connect(tab->webview, "notify::estimated-load-progress", G_CALLBACK(on_progress_changed), raw_tab);
    g_signal_connect(tab->webview, "load-changed", G_CALLBACK(on_load_changed), raw_tab);
    g_signal_connect(tab->webview, "create", G_CALLBACK(on_create_web_view), raw_tab);

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

void on_shield_clicked(GtkButton*, gpointer user_data) {
    auto* app = static_cast<BrowserApp*>(user_data);
    app->blocker_enabled = !app->blocker_enabled;
    for (auto& tab : app->tabs) {
        apply_filter_to_tab(app, tab.get());
    }
    sync_toolbar(app);
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
    GtkWidget* menu = gtk_menu_new();
    g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), nullptr);

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
        if (key == GDK_KEY_b) { on_shield_clicked(nullptr, app); return TRUE; }
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

    app->window = gtk_application_window_new(gtk_app);
    gtk_window_set_title(GTK_WINDOW(app->window), "SNC");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1120, 740);
    g_signal_connect(app->window, "destroy", G_CALLBACK(on_window_destroy), app);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 6);

    app->back_btn = gtk_button_new_with_label("◀");
    app->forward_btn = gtk_button_new_with_label("▶");
    app->reload_btn = gtk_button_new_with_label("Reload");
    app->shield_btn = gtk_button_new_with_label("Shield: ON");
    app->address_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->address_entry), "Search DuckDuckGo or enter URL");
    app->bookmark_btn = gtk_button_new_with_label("☆");
    GtkWidget* new_tab_btn = gtk_button_new_with_label("+");
    GtkWidget* private_btn = gtk_button_new_with_label("Private");
    app->menu_btn = gtk_button_new_with_label("☰");

    gtk_box_pack_start(GTK_BOX(toolbar), app->back_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app->forward_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app->reload_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app->shield_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app->address_entry, TRUE, TRUE, 4);
    gtk_box_pack_start(GTK_BOX(toolbar), app->bookmark_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), new_tab_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), private_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toolbar), app->menu_btn, FALSE, FALSE, 0);

    app->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(app->progress_bar, TRUE);

    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);

    gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app->progress_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), app->notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    g_signal_connect(app->back_btn, "clicked", G_CALLBACK(on_back_clicked), app);
    g_signal_connect(app->forward_btn, "clicked", G_CALLBACK(on_forward_clicked), app);
    g_signal_connect(app->reload_btn, "clicked", G_CALLBACK(on_reload_clicked), app);
    g_signal_connect(app->shield_btn, "clicked", G_CALLBACK(on_shield_clicked), app);
    g_signal_connect(app->address_entry, "activate", G_CALLBACK(on_address_activate), app);
    g_signal_connect(app->bookmark_btn, "clicked", G_CALLBACK(on_bookmark_clicked), app);
    g_signal_connect(new_tab_btn, "clicked", G_CALLBACK(on_new_tab_clicked), app);
    g_signal_connect(private_btn, "clicked", G_CALLBACK(on_new_private_tab_clicked), app);
    g_signal_connect(app->menu_btn, "clicked", G_CALLBACK(on_menu_btn_clicked), app);
    g_signal_connect_after(app->notebook, "switch-page", G_CALLBACK(on_switch_page), app);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(on_key_press), app);

    if (!app->filter_store) {
        auto filter_dir = app->store.data_dir() / "filters";
        std::error_code ec;
        std::filesystem::create_directories(filter_dir, ec);
        app->filter_store = webkit_user_content_filter_store_new(filter_dir.c_str());
    }

    std::string json = load_blocklist_json();
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
