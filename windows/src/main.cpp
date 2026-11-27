#include "snc/blocker.hpp"
#include "snc/storage.hpp"
#include "snc/url_resolver.hpp"

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kIdTabCtrl = 101;
constexpr int kIdBack = 102;
constexpr int kIdForward = 103;
constexpr int kIdReload = 104;
constexpr int kIdShield = 105;
constexpr int kIdAddress = 106;
constexpr int kIdBookmark = 107;
constexpr int kIdNewTab = 108;
constexpr int kIdPrivateTab = 109;
constexpr int kIdMenu = 110;

constexpr int kTabStripHeight = 30;
constexpr int kToolbarHeight = 38;
constexpr UINT_PTR kCmdBookmarkBase = 2000;
constexpr UINT_PTR kCmdHistoryBase = 3000;
constexpr UINT_PTR kCmdClearHistory = 4000;

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

struct Tab {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::string title = "New Tab";
    std::string url;
    bool is_private = false;
    bool is_loading = false;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool closed = false;
};

struct AppState {
    HWND hwnd = nullptr;
    HWND tab_ctrl = nullptr;
    HWND back_btn = nullptr;
    HWND forward_btn = nullptr;
    HWND reload_btn = nullptr;
    HWND shield_btn = nullptr;
    HWND address_edit = nullptr;
    HWND bookmark_btn = nullptr;
    HWND new_tab_btn = nullptr;
    HWND private_btn = nullptr;
    HWND menu_btn = nullptr;
    HFONT ui_font = nullptr;

    ComPtr<ICoreWebView2Environment> env;
    snc::BrowserStore store;
    snc::Blocker blocker;
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

void sync_toolbar() {
    Tab* tab = current_tab();
    if (!tab) return;

    std::string title = tab->is_private ? ("SNC (Private) — " + tab->title) : ("SNC — " + tab->title);
    SetWindowTextW(g_app.hwnd, to_wide(title).c_str());

    EnableWindow(g_app.back_btn, tab->can_go_back ? TRUE : FALSE);
    EnableWindow(g_app.forward_btn, tab->can_go_forward ? TRUE : FALSE);
    SetWindowTextW(g_app.reload_btn, tab->is_loading ? L"Stop" : L"Reload");
    SetWindowTextW(g_app.shield_btn, g_app.blocker.enabled() ? L"Shield: ON" : L"Shield: OFF");

    bool starred = !tab->url.empty() && g_app.store.is_bookmarked(tab->url);
    SetWindowTextW(g_app.bookmark_btn, starred ? L"*" : L"+Bm");

    if (GetFocus() != g_app.address_edit) {
        SetWindowTextW(g_app.address_edit, to_wide(tab->url).c_str());
    }
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

    place(g_app.back_btn, 36);
    place(g_app.forward_btn, 36);
    place(g_app.reload_btn, 62);
    place(g_app.shield_btn, 86);

    int right_buttons = 48 + 42 + 64 + 44 + 24;
    int edit_w = (std::max)(120, w - x - right_buttons);
    place(g_app.address_edit, edit_w);

    place(g_app.bookmark_btn, 44);
    place(g_app.new_tab_btn, 40);
    place(g_app.private_btn, 62);
    place(g_app.menu_btn, 42);

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

void attach_webview_to_tab(const std::shared_ptr<Tab>& tab, ICoreWebView2Controller* controller) {
    if (tab->closed) {
        controller->Close();
        return;
    }
    tab->controller = controller;
    controller->get_CoreWebView2(&tab->webview);
    controller->put_Bounds(webview_bounds());
    controller->put_IsVisible(tab.get() == current_tab() ? TRUE : FALSE);

    if (!tab->webview) return;

    tab->webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
    tab->webview->add_WebResourceRequested(
        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                if (tab->closed || !g_app.blocker.enabled() || !g_app.env) return S_OK;
                COREWEBVIEW2_WEB_RESOURCE_CONTEXT ctx = COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL;
                args->get_ResourceContext(&ctx);
                if (ctx == COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT) return S_OK;

                ComPtr<ICoreWebView2WebResourceRequest> req;
                if (FAILED(args->get_Request(&req)) || !req) return S_OK;
                LPWSTR raw_uri = nullptr;
                if (FAILED(req->get_Uri(&raw_uri)) || !raw_uri) return S_OK;
                std::string req_url = to_utf8(raw_uri);
                CoTaskMemFree(raw_uri);

                std::string page_host = snc::extract_host(tab->url);
                if (g_app.blocker.should_block(req_url, page_host)) {
                    ComPtr<ICoreWebView2WebResourceResponse> resp;
                    if (SUCCEEDED(g_app.env->CreateWebResourceResponse(nullptr, 204, L"Blocked", L"", &resp))) {
                        args->put_Response(resp.Get());
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
    tab->url = initial_url.empty() ? std::string(snc::kDefaultHomeUrl) : initial_url;

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
    std::string target = snc::resolve_input(to_utf8(buf));
    tab->url = target;
    if (tab->webview) {
        tab->webview->Navigate(to_wide(target).c_str());
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

    if (cmd == kCmdClearHistory) {
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

        g_app.tab_ctrl = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_TABS,
                                         0, 0, 100, kTabStripHeight, hwnd, ctrl_id(kIdTabCtrl), nullptr, nullptr);
        g_app.back_btn = CreateWindowW(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE, 0, 0, 36, 28, hwnd, ctrl_id(kIdBack), nullptr, nullptr);
        g_app.forward_btn = CreateWindowW(L"BUTTON", L">", WS_CHILD | WS_VISIBLE, 0, 0, 36, 28, hwnd, ctrl_id(kIdForward), nullptr, nullptr);
        g_app.reload_btn = CreateWindowW(L"BUTTON", L"Reload", WS_CHILD | WS_VISIBLE, 0, 0, 62, 28, hwnd, ctrl_id(kIdReload), nullptr, nullptr);
        g_app.shield_btn = CreateWindowW(L"BUTTON", L"Shield: ON", WS_CHILD | WS_VISIBLE, 0, 0, 86, 28, hwnd, ctrl_id(kIdShield), nullptr, nullptr);
        g_app.address_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                             0, 0, 300, 28, hwnd, ctrl_id(kIdAddress), nullptr, nullptr);
        g_app.bookmark_btn = CreateWindowW(L"BUTTON", L"+Bm", WS_CHILD | WS_VISIBLE, 0, 0, 44, 28, hwnd, ctrl_id(kIdBookmark), nullptr, nullptr);
        g_app.new_tab_btn = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE, 0, 0, 40, 28, hwnd, ctrl_id(kIdNewTab), nullptr, nullptr);
        g_app.private_btn = CreateWindowW(L"BUTTON", L"Private", WS_CHILD | WS_VISIBLE, 0, 0, 62, 28, hwnd, ctrl_id(kIdPrivateTab), nullptr, nullptr);
        g_app.menu_btn = CreateWindowW(L"BUTTON", L"Menu", WS_CHILD | WS_VISIBLE, 0, 0, 42, 28, hwnd, ctrl_id(kIdMenu), nullptr, nullptr);

        for (HWND c : {g_app.tab_ctrl, g_app.back_btn, g_app.forward_btn, g_app.reload_btn, g_app.shield_btn,
                       g_app.address_edit, g_app.bookmark_btn, g_app.new_tab_btn, g_app.private_btn, g_app.menu_btn}) {
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
                        add_tab(std::string(snc::kDefaultHomeUrl), false);
                    }
                    return S_OK;
                }).Get());
        layout_controls();
        return 0;
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
        case kIdShield:
            g_app.blocker.set_enabled(!g_app.blocker.enabled());
            sync_toolbar();
            break;
        case kIdBookmark:
            if (tab) {
                g_app.store.toggle_bookmark(tab->title, tab->url);
                sync_toolbar();
            }
            break;
        case kIdNewTab: add_tab(std::string(snc::kDefaultHomeUrl), false); break;
        case kIdPrivateTab: add_tab(std::string(snc::kDefaultHomeUrl), true); break;
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
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool handle_accelerator(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

    if (ctrl && !shift && !alt) {
        if (msg.wParam == 'T') { add_tab(std::string(snc::kDefaultHomeUrl), false); return true; }
        if (msg.wParam == 'W') { close_current_tab(); return true; }
        if (msg.wParam == 'L') {
            SetFocus(g_app.address_edit);
            SendMessageW(g_app.address_edit, EM_SETSEL, 0, -1);
            return true;
        }
        if (msg.wParam == 'R') { SendMessageW(g_app.hwnd, WM_COMMAND, kIdReload, 0); return true; }
        if (msg.wParam == 'D') { SendMessageW(g_app.hwnd, WM_COMMAND, kIdBookmark, 0); return true; }
    } else if (ctrl && shift && !alt) {
        if (msg.wParam == 'N') { add_tab(std::string(snc::kDefaultHomeUrl), true); return true; }
        if (msg.wParam == 'B') { SendMessageW(g_app.hwnd, WM_COMMAND, kIdShield, 0); return true; }
    } else if (alt && !ctrl) {
        if (msg.wParam == VK_LEFT) { SendMessageW(g_app.hwnd, WM_COMMAND, kIdBack, 0); return true; }
        if (msg.wParam == VK_RIGHT) { SendMessageW(g_app.hwnd, WM_COMMAND, kIdForward, 0); return true; }
    }
    return false;
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
    wc.lpszClassName = L"SNCBrowserWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"SNC",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 740,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (handle_accelerator(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
