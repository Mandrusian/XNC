#include "xnc/blocker.hpp"
#include "xnc/storage.hpp"
#include "xnc/url_resolver.hpp"

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kIdTabCtrl = 101;
constexpr int kIdBack = 102;
constexpr int kIdForward = 103;
constexpr int kIdReload = 104;
constexpr int kIdShield = 105;
constexpr int kIdZapper = 106;
constexpr int kIdMica = 107;
constexpr int kIdAddress = 108;
constexpr int kIdBookmark = 109;
constexpr int kIdNewTab = 110;
constexpr int kIdPrivateTab = 111;
constexpr int kIdDev = 112;
constexpr int kIdMenu = 113;

constexpr int kTabStripHeight = 30;
constexpr int kToolbarHeight = 38;
constexpr UINT_PTR kCmdBookmarkBase = 2000;
constexpr UINT_PTR kCmdHistoryBase = 3000;
constexpr UINT_PTR kCmdClearHistory = 4000;

constexpr UINT kCmdShieldToggleSite = 5001;
constexpr UINT kCmdShieldToggleGlobal = 5002;
constexpr UINT kCmdShieldZapper = 5003;
constexpr UINT kCmdShieldClearSite = 5004;

constexpr UINT kCmdDevInspector = 6001;
constexpr UINT kCmdDevZapper = 6002;
constexpr UINT kCmdDevToggleMica = 6003;
constexpr UINT kCmdDevToggleJS = 6004;

constexpr UINT kCmdSearxLocal = 7001;
constexpr UINT kCmdSearxClipboard = 7002;
constexpr UINT kCmdSearxDDG = 7003;

HMENU ctrl_id(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

std::string to_utf8(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::string read_clipboard_utf8(HWND hwnd) {
    if (!OpenClipboard(hwnd)) return {};
    std::string result;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            result = to_utf8(w);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return result;
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

std::string load_shared_text(const std::string& filename, const std::string& fallback) {
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

void apply_mica_backdrop(HWND hwnd, bool full_window) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins = full_window ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, kTabStripHeight + kToolbarHeight, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

struct Tab {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::string title = "New Tab";
    std::string url;
    bool is_private = false;
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool js_enabled = true;
    bool closed = false;
};

struct AppState {
    HWND hwnd = nullptr;
    HWND tab_ctrl = nullptr;
    HWND back_btn = nullptr;
    HWND forward_btn = nullptr;
    HWND reload_btn = nullptr;
    HWND shield_btn = nullptr;
    HWND zapper_btn = nullptr;
    HWND mica_btn = nullptr;
    HWND address_edit = nullptr;
    HWND bookmark_btn = nullptr;
    HWND new_tab_btn = nullptr;
    HWND private_btn = nullptr;
    HWND dev_btn = nullptr;
    HWND menu_btn = nullptr;
    HFONT ui_font = nullptr;

    ComPtr<ICoreWebView2Environment> env;
    xnc::BrowserStore store;
    xnc::Blocker blocker;
    std::vector<std::shared_ptr<Tab>> tabs;
    int selected_index = -1;
};

AppState g_app;

RECT webview_bounds() {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    rc.top = kTabStripHeight + kToolbarHeight;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

Tab* current_tab() {
    if (g_app.selected_index < 0 || static_cast<size_t>(g_app.selected_index) >= g_app.tabs.size()) {
        return nullptr;
    }
    return g_app.tabs[static_cast<size_t>(g_app.selected_index)].get();
}

void inject_cosmetic_and_mica(Tab* tab) {
    if (!tab || tab->closed) return;
    bool mica = g_app.store.transparent_websites();

    if (tab->controller) {
        ComPtr<ICoreWebView2Controller2> ctrl2;
        if (SUCCEEDED(tab->controller.As(&ctrl2)) && ctrl2) {
            COREWEBVIEW2_COLOR bg = mica ? COREWEBVIEW2_COLOR{0, 0, 0, 0} : COREWEBVIEW2_COLOR{255, 255, 255, 255};
            ctrl2->put_DefaultBackgroundColor(bg);
        }
    }
    if (!tab->webview) return;

    std::string host = xnc::extract_host(tab->url);
    bool shield_active = g_app.blocker.enabled() && !g_app.store.is_host_allowlisted(host);
    std::string cosmetic_css = load_shared_text("cosmetic_rules.css", std::string(xnc::Blocker::default_cosmetic_css()));
    std::string css = xnc::Blocker::build_css_for_host(
        shield_active, g_app.store.custom_selectors(host), cosmetic_css);
    tab->webview->ExecuteScript(to_wide(xnc::Blocker::build_inject_css_script(css)).c_str(), nullptr);
    tab->webview->ExecuteScript(to_wide(xnc::Blocker::build_transparent_mica_script(mica)).c_str(), nullptr);
}

void sync_toolbar() {
    Tab* tab = current_tab();
    if (!tab) return;

    std::string title = tab->is_private ? ("XNC (Private) — " + tab->title) : ("XNC — " + tab->title);
    SetWindowTextW(g_app.hwnd, to_wide(title).c_str());

    EnableWindow(g_app.back_btn, tab->can_go_back ? TRUE : FALSE);
    EnableWindow(g_app.forward_btn, tab->can_go_forward ? TRUE : FALSE);
    SetWindowTextW(g_app.reload_btn, tab->is_loading ? L"Stop" : L"Reload");

    std::string host = xnc::extract_host(tab->url);
    bool paused = !host.empty() && g_app.store.is_host_allowlisted(host);
    SetWindowTextW(g_app.shield_btn,
                   !g_app.blocker.enabled() ? L"uB: OFF" : (paused ? L"uB: Paused" : L"uB: ON"));
    SetWindowTextW(g_app.mica_btn, g_app.store.transparent_websites() ? L"Glass: ON" : L"Glass");

    bool starred = !tab->url.empty() && g_app.store.is_bookmarked(tab->url);
    SetWindowTextW(g_app.bookmark_btn, starred ? L"*" : L"+Bm");

    if (GetFocus() != g_app.address_edit) {
        SetWindowTextW(g_app.address_edit, to_wide(tab->url).c_str());
    }
}

void toggle_transparent_mica() {
    bool next = !g_app.store.transparent_websites();
    g_app.store.set_transparent_websites(next);
    apply_mica_backdrop(g_app.hwnd, next);
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
    for (auto& tab : g_app.tabs) {
        inject_cosmetic_and_mica(tab.get());
    }
    sync_toolbar();
}

void update_tab_caption(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_app.tabs.size()) return;
    Tab* tab = g_app.tabs[static_cast<size_t>(index)].get();
    std::string prefix = tab->is_private ? "[Private] " : "";
    std::string label = prefix + (tab->title.size() > 20 ? tab->title.substr(0, 19) + "..." : tab->title);
    std::wstring wlabel = to_wide(label);

    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = wlabel.data();
    TabCtrl_SetItem(g_app.tab_ctrl, index, &item);
}

void update_caption_for_tab(const Tab* target) {
    for (size_t i = 0; i < g_app.tabs.size(); ++i) {
        if (g_app.tabs[i].get() == target) {
            update_tab_caption(static_cast<int>(i));
            return;
        }
    }
}

void layout_controls() {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    int w = rc.right - rc.left;

    MoveWindow(g_app.tab_ctrl, 0, 0, w, kTabStripHeight, TRUE);

    int y = kTabStripHeight + 4;
    int h = kToolbarHeight - 8;
    int x = 8;
    auto place = [&](HWND ctrl, int width) {
        MoveWindow(ctrl, x, y, width, h, TRUE);
        x += width + 4;
    };

    place(g_app.back_btn, 30);
    place(g_app.forward_btn, 30);
    place(g_app.reload_btn, 54);
    place(g_app.shield_btn, 72);
    place(g_app.zapper_btn, 38);
    place(g_app.mica_btn, 66);

    int right_buttons = 40 + 36 + 52 + 40 + 40 + 28;
    int edit_w = (std::max)(120, w - x - right_buttons);
    place(g_app.address_edit, edit_w);

    place(g_app.bookmark_btn, 40);
    place(g_app.new_tab_btn, 36);
    place(g_app.private_btn, 52);
    place(g_app.dev_btn, 40);
    place(g_app.menu_btn, 40);

    RECT wv_rc = webview_bounds();
    for (size_t i = 0; i < g_app.tabs.size(); ++i) {
        if (g_app.tabs[i]->controller) {
            g_app.tabs[i]->controller->put_Bounds(wv_rc);
            g_app.tabs[i]->controller->put_IsVisible(
                static_cast<int>(i) == g_app.selected_index ? TRUE : FALSE);
        }
    }
}

void select_tab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_app.tabs.size()) return;
    g_app.selected_index = index;
    TabCtrl_SetCurSel(g_app.tab_ctrl, index);
    RECT wv_rc = webview_bounds();
    for (size_t i = 0; i < g_app.tabs.size(); ++i) {
        if (g_app.tabs[i]->controller) {
            g_app.tabs[i]->controller->put_Bounds(wv_rc);
            g_app.tabs[i]->controller->put_IsVisible(static_cast<int>(i) == index ? TRUE : FALSE);
        }
    }
    sync_toolbar();
}

void add_tab(const std::string& initial_url, bool is_private);
bool process_shortcut(UINT vkey, bool ctrl, bool shift, bool alt);

void attach_webview_to_tab(const std::shared_ptr<Tab>& tab, ICoreWebView2Controller* controller) {
    if (tab->closed) {
        controller->Close();
        return;
    }
    tab->controller = controller;
    controller->get_CoreWebView2(&tab->webview);
    controller->put_Bounds(webview_bounds());
    controller->put_IsVisible(tab.get() == current_tab() ? TRUE : FALSE);

    controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [tab](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                if (tab->closed) return S_OK;
                COREWEBVIEW2_KEY_EVENT_KIND kind{};
                args->get_KeyEventKind(&kind);
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                    kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                    return S_OK;
                }
                UINT vkey = 0;
                args->get_VirtualKey(&vkey);
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                if (process_shortcut(vkey, ctrl, shift, alt)) {
                    args->put_Handled(TRUE);
                }
                return S_OK;
            }).Get(),
        nullptr);

    if (!tab->webview) return;
    inject_cosmetic_and_mica(tab.get());

    tab->webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    tab->webview->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                if (tab->closed || !g_app.blocker.enabled() || !g_app.env) return S_OK;
                std::string page_host = xnc::extract_host(tab->url);
                if (!page_host.empty() && g_app.store.is_host_allowlisted(page_host)) return S_OK;

                COREWEBVIEW2_WEB_RESOURCE_CONTEXT ctx = COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL;
                args->get_ResourceContext(&ctx);
                if (ctx == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT) return S_OK;

                ComPtr<ICoreWebView2WebResourceRequest> req;
                if (FAILED(args->get_Request(&req)) || !req) return S_OK;
                LPWSTR raw_uri = nullptr;
                if (FAILED(req->get_Uri(&raw_uri)) || !raw_uri) return S_OK;
                std::string req_url = to_utf8(raw_uri);
                CoTaskMemFree(raw_uri);

                if (g_app.blocker.should_block(req_url, page_host)) {
                    ComPtr<ICoreWebView2WebResourceResponse> resp;
                    if (SUCCEEDED(g_app.env->CreateWebResourceResponse(nullptr, 204, L"Blocked", L"", &resp))) {
                        args->put_Response(resp.Get());
                    }
                }
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR raw = nullptr;
                if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
                std::string msg = to_utf8(raw);
                CoTaskMemFree(raw);
                if (extract_json_string_field(msg, "type") == "zapper") {
                    std::string host = extract_json_string_field(msg, "host");
                    std::string sel = extract_json_string_field(msg, "selector");
                    if (!host.empty() && !sel.empty()) {
                        g_app.store.add_custom_selector(host, sel);
                        inject_cosmetic_and_mica(tab.get());
                    }
                }
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                tab->is_loading = true;
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [tab](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                tab->is_loading = false;
                BOOL back = FALSE, fwd = FALSE;
                sender->get_CanGoBack(&back);
                sender->get_CanGoForward(&fwd);
                tab->can_go_back = (back == TRUE);
                tab->can_go_forward = (fwd == TRUE);

                LPWSTR uri = nullptr;
                if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                    tab->url = to_utf8(uri);
                    CoTaskMemFree(uri);
                }
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    std::string t = to_utf8(title);
                    if (!t.empty()) tab->title = t;
                    CoTaskMemFree(title);
                }
                inject_cosmetic_and_mica(tab.get());
                g_app.store.record_history(tab->title, tab->url, tab->is_private);
                update_caption_for_tab(tab.get());
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [tab](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    std::string t = to_utf8(title);
                    if (!t.empty()) tab->title = t;
                    CoTaskMemFree(title);
                }
                update_caption_for_tab(tab.get());
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [tab](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR uri = nullptr;
                if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                    tab->url = to_utf8(uri);
                    CoTaskMemFree(uri);
                }
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR uri = nullptr;
                if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
                    std::string next_url = to_utf8(uri);
                    CoTaskMemFree(uri);
                    args->put_Handled(TRUE);
                    add_tab(next_url, tab->is_private);
                }
                return S_OK;
            }).Get(),
        nullptr);

    tab->webview->Navigate(to_wide(tab->url).c_str());
}

void create_controller_for_tab(const std::shared_ptr<Tab>& tab) {
    if (!g_app.env || tab->closed) return;

    auto completion = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [tab](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
            if (SUCCEEDED(hr) && controller) {
                attach_webview_to_tab(tab, controller);
            }
            return S_OK;
        });

    ComPtr<ICoreWebView2Environment10> env10;
    if (tab->is_private && SUCCEEDED(g_app.env.As(&env10))) {
        ComPtr<ICoreWebView2ControllerOptions> opts;
        if (SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&opts))) {
            opts->put_IsInPrivateModeEnabled(TRUE);
            env10->CreateCoreWebView2ControllerWithOptions(g_app.hwnd, opts.Get(), completion.Get());
            return;
        }
    }
    g_app.env->CreateCoreWebView2Controller(g_app.hwnd, completion.Get());
}

void add_tab(const std::string& initial_url, bool is_private) {
    auto tab = std::make_shared<Tab>();
    tab->is_private = is_private;
    tab->title = is_private ? "Private Tab" : "New Tab";
    std::string home = xnc::home_url_for_search_engine(g_app.store.search_engine_url());
    tab->url = initial_url.empty() ? home : initial_url;

    int new_idx = static_cast<int>(g_app.tabs.size());
    std::wstring label = to_wide((is_private ? "[Private] " : "") + tab->title);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = label.data();
    TabCtrl_InsertItem(g_app.tab_ctrl, new_idx, &item);

    g_app.tabs.push_back(tab);
    select_tab(new_idx);
    create_controller_for_tab(tab);
}

void close_current_tab() {
    if (g_app.tabs.empty()) return;
    int idx = g_app.selected_index;
    if (idx < 0 || static_cast<size_t>(idx) >= g_app.tabs.size()) return;

    auto tab = g_app.tabs[static_cast<size_t>(idx)];
    tab->closed = true;
    if (tab->controller) {
        tab->controller->Close();
        tab->controller.Reset();
    }
    tab->webview.Reset();

    TabCtrl_DeleteItem(g_app.tab_ctrl, idx);
    g_app.tabs.erase(g_app.tabs.begin() + idx);

    if (g_app.tabs.empty()) {
        DestroyWindow(g_app.hwnd);
        return;
    }
    int next_idx = (idx >= static_cast<int>(g_app.tabs.size())) ? static_cast<int>(g_app.tabs.size()) - 1 : idx;
    select_tab(next_idx);
}

void navigate_from_address_bar() {
    Tab* tab = current_tab();
    if (!tab) return;
    wchar_t buf[4096]{};
    GetWindowTextW(g_app.address_edit, buf, 4096);
    std::string target = xnc::resolve_input(to_utf8(buf), g_app.store.search_engine_url());
    tab->url = target;
    if (tab->webview) {
        tab->webview->Navigate(to_wide(target).c_str());
    }
}

void trigger_zapper() {
    Tab* tab = current_tab();
    if (!tab || !tab->webview) return;
    std::string js = load_shared_text("zapper.js", std::string(xnc::Blocker::default_zapper_script()));
    tab->webview->ExecuteScript(to_wide(js).c_str(), nullptr);
}

void show_shield_menu() {
    Tab* tab = current_tab();
    std::string host = tab ? xnc::extract_host(tab->url) : "";
    bool paused = !host.empty() && g_app.store.is_host_allowlisted(host);

    HMENU menu = CreatePopupMenu();
    if (!host.empty()) {
        std::string label = paused ? ("Resume uBlock on " + host) : ("Pause uBlock on " + host);
        AppendMenuW(menu, MF_STRING, kCmdShieldToggleSite, to_wide(label).c_str());
    }
    AppendMenuW(menu, MF_STRING, kCmdShieldToggleGlobal,
                g_app.blocker.enabled() ? L"Disable Global Shield (Ctrl+Shift+B)" : L"Enable Global Shield (Ctrl+Shift+B)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdShieldZapper, L"Element Zapper (Ctrl+Shift+Z)");
    if (!host.empty() && !g_app.store.custom_selectors(host).empty()) {
        AppendMenuW(menu, MF_STRING, kCmdShieldClearSite, to_wide("Clear Zapped Rules on " + host).c_str());
    }

    RECT rc{};
    GetWindowRect(g_app.shield_btn, &rc);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              rc.left, rc.bottom, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == kCmdShieldToggleSite && !host.empty()) {
        g_app.store.toggle_host_allowlist(host);
        inject_cosmetic_and_mica(tab);
        sync_toolbar();
    } else if (cmd == kCmdShieldToggleGlobal) {
        g_app.blocker.set_enabled(!g_app.blocker.enabled());
        inject_cosmetic_and_mica(tab);
        sync_toolbar();
    } else if (cmd == kCmdShieldZapper) {
        trigger_zapper();
    } else if (cmd == kCmdShieldClearSite && !host.empty() && tab && tab->webview) {
        g_app.store.clear_custom_selectors(host);
        tab->webview->Reload();
    }
}

void show_dev_menu() {
    Tab* tab = current_tab();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdDevInspector, L"Open DevTools (F12 / Ctrl+Shift+I)");
    AppendMenuW(menu, MF_STRING, kCmdDevZapper, L"Element Zapper (Ctrl+Shift+Z)");
    AppendMenuW(menu, MF_STRING, kCmdDevToggleMica,
                g_app.store.transparent_websites() ? L"Disable Transparent Mica Websites (Ctrl+Shift+M)" : L"Enable Transparent Mica Websites (Ctrl+Shift+M)");
    AppendMenuW(menu, MF_STRING, kCmdDevToggleJS,
                (tab && !tab->js_enabled) ? L"Enable JavaScript on Tab" : L"Disable JavaScript on Tab");

    RECT rc{};
    GetWindowRect(g_app.dev_btn, &rc);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                              rc.right, rc.bottom, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == kCmdDevInspector && tab && tab->webview) {
        tab->webview->OpenDevToolsWindow();
    } else if (cmd == kCmdDevZapper) {
        trigger_zapper();
    } else if (cmd == kCmdDevToggleMica) {
        toggle_transparent_mica();
    } else if (cmd == kCmdDevToggleJS && tab && tab->webview) {
        tab->js_enabled = !tab->js_enabled;
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(tab->webview->get_Settings(&settings)) && settings) {
            settings->put_IsScriptEnabled(tab->js_enabled ? TRUE : FALSE);
        }
        tab->webview->Reload();
    }
}

LRESULT CALLBACK AddressSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        navigate_from_address_bar();
        return 0;
    }
    if (msg == WM_CHAR && wParam == L'\r') {
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void show_bookmarks_history_menu() {
    HMENU menu = CreatePopupMenu();

    HMENU searx_sub = CreatePopupMenu();
    AppendMenuW(searx_sub, MF_STRING, kCmdSearxClipboard, L"Use SearXNG URL from Clipboard");
    AppendMenuW(searx_sub, MF_STRING, kCmdSearxLocal, L"Use Local SearXNG (http://localhost:8080)");
    AppendMenuW(searx_sub, MF_STRING, kCmdSearxDDG, L"Reset to DuckDuckGo");
    std::string searx_hdr = g_app.store.search_engine_url().empty()
        ? "Search Engine: DuckDuckGo"
        : ("Search Engine: " + g_app.store.search_engine_url());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(searx_sub), to_wide(searx_hdr).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Bookmarks");
    for (size_t i = 0; i < g_app.store.bookmarks().size() && i < 20; ++i) {
        const auto& b = g_app.store.bookmarks()[i];
        AppendMenuW(menu, MF_STRING, kCmdBookmarkBase + i, to_wide("  " + b.title).c_str());
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent History");
    for (size_t i = 0; i < g_app.store.history().size() && i < 15; ++i) {
        const auto& h = g_app.store.history()[i];
        AppendMenuW(menu, MF_STRING, kCmdHistoryBase + i, to_wide("  " + h.title).c_str());
    }
    if (!g_app.store.history().empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCmdClearHistory, L"Clear History");
    }

    RECT rc{};
    GetWindowRect(g_app.menu_btn, &rc);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                              rc.right, rc.bottom, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    auto navigate_home = [&]() {
        std::string home = xnc::home_url_for_search_engine(g_app.store.search_engine_url());
        if (auto* tab = current_tab(); tab && tab->webview) {
            tab->webview->Navigate(to_wide(home).c_str());
        }
    };

    if (cmd == kCmdSearxLocal) {
        g_app.store.set_search_engine_url("http://localhost:8080");
        navigate_home();
    } else if (cmd == kCmdSearxClipboard) {
        std::string clip = read_clipboard_utf8(g_app.hwnd);
        if (!clip.empty()) {
            g_app.store.set_search_engine_url(clip);
            navigate_home();
        }
    } else if (cmd == kCmdSearxDDG) {
        g_app.store.set_search_engine_url("");
        navigate_home();
    } else if (cmd == kCmdClearHistory) {
        g_app.store.clear_history();
    } else if (cmd >= kCmdBookmarkBase && cmd < kCmdHistoryBase) {
        size_t idx = cmd - kCmdBookmarkBase;
        if (idx < g_app.store.bookmarks().size()) {
            if (auto* tab = current_tab(); tab && tab->webview) {
                tab->webview->Navigate(to_wide(g_app.store.bookmarks()[idx].url).c_str());
            }
        }
    } else if (cmd >= kCmdHistoryBase && cmd < kCmdClearHistory) {
        size_t idx = cmd - kCmdHistoryBase;
        if (idx < g_app.store.history().size()) {
            if (auto* tab = current_tab(); tab && tab->webview) {
                tab->webview->Navigate(to_wide(g_app.store.history()[idx].url).c_str());
            }
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hwnd = hwnd;
        g_app.ui_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        apply_mica_backdrop(hwnd, g_app.store.transparent_websites());

        g_app.tab_ctrl = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_TABS,
                                         0, 0, 100, kTabStripHeight, hwnd, ctrl_id(kIdTabCtrl), nullptr, nullptr);
        g_app.back_btn = CreateWindowW(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE, 0, 0, 30, 28, hwnd, ctrl_id(kIdBack), nullptr, nullptr);
        g_app.forward_btn = CreateWindowW(L"BUTTON", L">", WS_CHILD | WS_VISIBLE, 0, 0, 30, 28, hwnd, ctrl_id(kIdForward), nullptr, nullptr);
        g_app.reload_btn = CreateWindowW(L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE, 0, 0, 54, 28, hwnd, ctrl_id(kIdReload), nullptr, nullptr);
        g_app.shield_btn = CreateWindowW(L"BUTTON", L"uB: ON", WS_CHILD | WS_VISIBLE, 0, 0, 72, 28, hwnd, ctrl_id(kIdShield), nullptr, nullptr);
        g_app.zapper_btn = CreateWindowW(L"BUTTON", L"Zap", WS_CHILD | WS_VISIBLE, 0, 0, 38, 28, hwnd, ctrl_id(kIdZapper), nullptr, nullptr);
        g_app.mica_btn = CreateWindowW(L"BUTTON", L"Glass", WS_CHILD | WS_VISIBLE, 0, 0, 66, 28, hwnd, ctrl_id(kIdMica), nullptr, nullptr);
        g_app.address_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                             0, 0, 300, 28, hwnd, ctrl_id(kIdAddress), nullptr, nullptr);
        g_app.bookmark_btn = CreateWindowW(L"BUTTON", L"+Bm", WS_CHILD | WS_VISIBLE, 0, 0, 40, 28, hwnd, ctrl_id(kIdBookmark), nullptr, nullptr);
        g_app.new_tab_btn = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE, 0, 0, 36, 28, hwnd, ctrl_id(kIdNewTab), nullptr, nullptr);
        g_app.private_btn = CreateWindowW(L"BUTTON", L"Priv", WS_CHILD | WS_VISIBLE, 0, 0, 52, 28, hwnd, ctrl_id(kIdPrivateTab), nullptr, nullptr);
        g_app.dev_btn = CreateWindowW(L"BUTTON", L"Dev", WS_CHILD | WS_VISIBLE, 0, 0, 40, 28, hwnd, ctrl_id(kIdDev), nullptr, nullptr);
        g_app.menu_btn = CreateWindowW(L"BUTTON", L"Menu", WS_CHILD | WS_VISIBLE, 0, 0, 40, 28, hwnd, ctrl_id(kIdMenu), nullptr, nullptr);

        for (HWND c : {g_app.tab_ctrl, g_app.back_btn, g_app.forward_btn, g_app.reload_btn, g_app.shield_btn,
                       g_app.zapper_btn, g_app.mica_btn, g_app.address_edit, g_app.bookmark_btn,
                       g_app.new_tab_btn, g_app.private_btn, g_app.dev_btn, g_app.menu_btn}) {
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
        }
        SetWindowSubclass(g_app.address_edit, AddressSubclassProc, 1, 0);

        auto domains_path = std::filesystem::current_path() / "shared" / "blocklist_domains.txt";
        g_app.blocker.load_domains_file(domains_path);

        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    if (SUCCEEDED(hr) && env && IsWindow(g_app.hwnd)) {
                        g_app.env = env;
                        add_tab(xnc::home_url_for_search_engine(g_app.store.search_engine_url()), false);
                    }
                    return S_OK;
                }).Get());
        layout_controls();
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH br = g_app.store.transparent_websites()
            ? static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))
            : GetSysColorBrush(COLOR_WINDOW);
        FillRect(hdc, &rc, br);
        return 1;
    }
    case WM_SIZE:
        layout_controls();
        return 0;
    case WM_NOTIFY: {
        auto* nmhdr = reinterpret_cast<NMHDR*>(lParam);
        if (nmhdr && nmhdr->idFrom == kIdTabCtrl && nmhdr->code == TCN_SELCHANGE) {
            select_tab(TabCtrl_GetCurSel(g_app.tab_ctrl));
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        Tab* tab = current_tab();
        switch (id) {
        case kIdBack: if (tab && tab->webview) tab->webview->GoBack(); break;
        case kIdForward: if (tab && tab->webview) tab->webview->GoForward(); break;
        case kIdReload:
            if (tab && tab->webview) {
                if (tab->is_loading) tab->webview->Stop();
                else tab->webview->Reload();
            }
            break;
        case kIdShield: show_shield_menu(); break;
        case kIdZapper: trigger_zapper(); break;
        case kIdMica: toggle_transparent_mica(); break;
        case kIdBookmark:
            if (tab) {
                g_app.store.toggle_bookmark(tab->title, tab->url);
                sync_toolbar();
            }
            break;
        case kIdNewTab: add_tab(xnc::home_url_for_search_engine(g_app.store.search_engine_url()), false); break;
        case kIdPrivateTab: add_tab(xnc::home_url_for_search_engine(g_app.store.search_engine_url()), true); break;
        case kIdDev: show_dev_menu(); break;
        case kIdMenu: show_bookmarks_history_menu(); break;
        default: break;
        }
        return 0;
    }
    case WM_DESTROY:
        for (auto& tab : g_app.tabs) {
            tab->closed = true;
            if (tab->controller) tab->controller->Close();
            tab->controller.Reset();
            tab->webview.Reset();
        }
        g_app.tabs.clear();
        g_app.env.Reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool process_shortcut(UINT vkey, bool ctrl, bool shift, bool alt) {
    if (vkey == VK_F12) {
        if (auto* tab = current_tab(); tab && tab->webview) tab->webview->OpenDevToolsWindow();
        return true;
    }
    if (ctrl && !shift && !alt) {
        if (vkey == 'T') { add_tab(xnc::home_url_for_search_engine(g_app.store.search_engine_url()), false); return true; }
        if (vkey == 'W') { close_current_tab(); return true; }
        if (vkey == 'L') {
            SetFocus(g_app.address_edit);
            SendMessageW(g_app.address_edit, EM_SETSEL, 0, -1);
            return true;
        }
        if (vkey == 'R') { SendMessageW(g_app.hwnd, WM_COMMAND, kIdReload, 0); return true; }
        if (vkey == 'D') { SendMessageW(g_app.hwnd, WM_COMMAND, kIdBookmark, 0); return true; }
    } else if (ctrl && shift && !alt) {
        if (vkey == 'N') { add_tab(xnc::home_url_for_search_engine(g_app.store.search_engine_url()), true); return true; }
        if (vkey == 'B') {
            g_app.blocker.set_enabled(!g_app.blocker.enabled());
            inject_cosmetic_and_mica(current_tab());
            sync_toolbar();
            return true;
        }
        if (vkey == 'Z') { trigger_zapper(); return true; }
        if (vkey == 'M') { toggle_transparent_mica(); return true; }
        if (vkey == 'I') {
            if (auto* tab = current_tab(); tab && tab->webview) tab->webview->OpenDevToolsWindow();
            return true;
        }
    } else if (alt && !ctrl) {
        if (vkey == VK_LEFT) { SendMessageW(g_app.hwnd, WM_COMMAND, kIdBack, 0); return true; }
        if (vkey == VK_RIGHT) { SendMessageW(g_app.hwnd, WM_COMMAND, kIdForward, 0); return true; }
    }
    return false;
}

bool handle_accelerator(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return process_shortcut(static_cast<UINT>(msg.wParam), ctrl, shift, alt);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"XNCBrowserWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"XNC",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1160, 760,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (handle_accelerator(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (auto& tab : g_app.tabs) {
        tab->closed = true;
        if (tab->controller) tab->controller->Close();
        tab->controller.Reset();
        tab->webview.Reset();
    }
    g_app.tabs.clear();
    g_app.env.Reset();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
