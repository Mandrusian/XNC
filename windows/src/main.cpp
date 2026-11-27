#include "xnc/blocker.hpp"
#include "xnc/storage.hpp"
#include "xnc/url_resolver.hpp"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <objbase.h>
#include <objidl.h>
#include <uxtheme.h>
#include <wrl.h>
#include <WebView2.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#include <gdiplus.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kAppIconId = 101;
constexpr int kIdAddressEdit = 108;

constexpr UINT_PTR kCmdBookmarkBase = 2000;
constexpr UINT_PTR kCmdHistoryBase = 3000;
constexpr UINT_PTR kCmdClearHistory = 4000;
constexpr UINT kCmdSearxConfigure = 4500;
constexpr UINT kCmdToggleMicaMenu = 4501;

constexpr UINT kCmdShieldToggleSite = 5001;
constexpr UINT kCmdShieldToggleGlobal = 5002;
constexpr UINT kCmdShieldZapper = 5003;
constexpr UINT kCmdShieldClearSite = 5004;

constexpr UINT kCmdDevInspector = 6001;
constexpr UINT kCmdDevConsole = 6002;
constexpr UINT kCmdDevSource = 6003;
constexpr UINT kCmdDevZapper = 6004;
constexpr UINT kCmdDevToggleMica = 6005;
constexpr UINT kCmdDevToggleJS = 6006;
constexpr UINT kCmdDevUADefault = 6010;
constexpr UINT kCmdDevUASafari = 6011;
constexpr UINT kCmdDevUAFirefox = 6012;
constexpr UINT kCmdDevUAMobile = 6013;
constexpr UINT kCmdDevClearData = 6020;

enum class HitKind {
    None,
    TabSelect,
    TabClose,
    NewTab,
    MinBtn,
    MaxBtn,
    CloseBtn,
    Back,
    Forward,
    Reload,
    Shield,
    Zapper,
    Mica,
    Bookmark,
    PrivateTab,
    Dev,
    Menu
};

struct HitBox {
    HitKind kind = HitKind::None;
    int index = -1;
    RECT rect{};
    bool enabled = true;
};

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

std::string escape_html(std::string_view raw) {
    std::string out;
    out.reserve(raw.size() + 64);
    for (char c : raw) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out.push_back(c);
    }
    return out;
}

std::string unescape_json_result(std::string_view raw) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        std::string out;
        out.reserve(raw.size());
        for (size_t i = 1; i + 1 < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 2 <= raw.size() - 1) {
                char n = raw[i + 1];
                if (n == 'n') { out.push_back('\n'); ++i; continue; }
                if (n == 'r') { out.push_back('\r'); ++i; continue; }
                if (n == 't') { out.push_back('\t'); ++i; continue; }
                if (n == '"' || n == '\\' || n == '/') { out.push_back(n); ++i; continue; }
                if (n == 'u' && i + 5 < raw.size() - 1) {
                    unsigned int code = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = raw[i + 2 + k];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(10 + h - 'a');
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(10 + h - 'A');
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else {
                        wchar_t wc = static_cast<wchar_t>(code);
                        char buf[4]{};
                        int bytes = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, buf, 4, nullptr, nullptr);
                        if (bytes > 0) out.append(buf, static_cast<size_t>(bytes));
                    }
                    i += 5;
                    continue;
                }
            }
            out.push_back(raw[i]);
        }
        return out;
    }
    return std::string(raw);
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

std::filesystem::path find_shared_file(const std::string& filename) {
    std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "shared" / filename,
        std::filesystem::current_path() / ".." / "shared" / filename,
    };
    wchar_t exe_buf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH) > 0) {
        std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
        candidates.push_back(exe_dir / "shared" / filename);
        candidates.push_back(exe_dir / ".." / "shared" / filename);
        candidates.push_back(exe_dir / ".." / ".." / "shared" / filename);
    }
    for (const auto& p : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) return p;
    }
    return {};
}

std::string load_shared_text(const std::string& filename, const std::string& fallback) {
    std::filesystem::path p = find_shared_file(filename);
    if (!p.empty()) {
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

void enable_dark_uxtheme_menus() {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto set_mode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flush_menus = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (set_mode) set_mode(2); // ForceDark / AllowDark
    if (flush_menus) flush_menus();
}

void apply_mica_backdrop(HWND hwnd, bool full_window, int top_chrome_px) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    int backdrop = DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    MARGINS margins = full_window ? MARGINS{-1, -1, -1, -1} : MARGINS{0, 0, top_chrome_px, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

struct Tab {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::string title = "New Tab";
    std::string url;
    std::string pending_html;
    std::string custom_ua;
    bool is_private = false;
    bool is_loading = false;
    double progress = 0.0;
    bool can_go_back = false;
    bool can_go_forward = false;
    bool js_enabled = true;
    bool zapper_active = false;
    bool closed = false;
};

struct AppState {
    HINSTANCE hinstance = nullptr;
    HWND hwnd = nullptr;
    HWND address_edit = nullptr;
    HFONT ui_font = nullptr;
    HBRUSH edit_bg_brush = nullptr;
    float dpi_scale = 1.0f;

    ComPtr<ICoreWebView2Environment> env;
    xnc::BrowserStore store;
    xnc::Blocker blocker;
    std::vector<std::shared_ptr<Tab>> tabs;
    int selected_index = -1;

    std::vector<HitBox> hitboxes;
    RECT address_pill_rc{};
    HitBox hovered{};
    bool tracking_mouse = false;
    bool is_fullscreen = false;
    WINDOWPLACEMENT prev_placement{sizeof(WINDOWPLACEMENT)};
};

AppState g_app;

int px(int dp) {
    return static_cast<int>(dp * g_app.dpi_scale + 0.5f);
}

int tab_strip_height() { return g_app.is_fullscreen ? 0 : px(38); }
int toolbar_height() { return g_app.is_fullscreen ? 0 : px(38); }
int chrome_height() { return g_app.is_fullscreen ? 0 : (tab_strip_height() + toolbar_height() + px(2)); }

bool is_newtab_url(const std::string& url) {
    return url.find("xnc.local/newtab.html") != std::string::npos || url == "about:newtab";
}

std::string default_newtab_url() {
    if (!find_shared_file("newtab.html").empty()) {
        return "https://xnc.local/newtab.html";
    }
    return xnc::home_url_for_search_engine(g_app.store.search_engine_url());
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

RECT webview_bounds() {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    rc.top = chrome_height();
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    return rc;
}

Tab* current_tab() {
    if (g_app.selected_index < 0 || static_cast<size_t>(g_app.selected_index) >= g_app.tabs.size()) {
        return nullptr;
    }
    return g_app.tabs[static_cast<size_t>(g_app.selected_index)].get();
}

void invalidate_chrome() {
    if (!g_app.hwnd) return;
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    rc.bottom = chrome_height() + px(2);
    InvalidateRect(g_app.hwnd, &rc, FALSE);
}

void sync_search_placeholder() {
    if (!g_app.address_edit) return;
    std::wstring ph;
    if (g_app.store.search_engine_url().empty()) {
        ph = L"Search DuckDuckGo or enter URL";
    } else {
        std::string host = xnc::display_host(xnc::home_url_for_search_engine(g_app.store.search_engine_url()));
        ph = to_wide("Search SearXNG (" + host + ") or enter URL");
    }
    SendMessageW(g_app.address_edit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(ph.c_str()));
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

    if (is_newtab_url(tab->url)) {
        std::string js = "if (typeof window.__xncApplyState === 'function') window.__xncApplyState({searxngUrl:\""
            + json_escape(g_app.store.search_engine_url()) + "\",micaEnabled:" + (mica ? "true" : "false") + "});";
        tab->webview->ExecuteScript(to_wide(js).c_str(), nullptr);
        return;
    }

    std::string host = xnc::extract_host(tab->url);
    bool shield_active = g_app.blocker.enabled() && !g_app.store.is_host_allowlisted(host);
    std::string cosmetic_css = load_shared_text("cosmetic_rules.css", std::string(xnc::Blocker::default_cosmetic_css()));
    std::string css = xnc::Blocker::build_css_for_host(
        shield_active, g_app.store.custom_selectors(host), cosmetic_css);
    if (!g_app.store.search_engine_url().empty()) {
        std::string searx_host = xnc::extract_host(xnc::home_url_for_search_engine(g_app.store.search_engine_url()));
        if (!searx_host.empty() && host == searx_host) {
            std::string searx_css = load_shared_text("searxng_theme.css", "");
            if (!searx_css.empty()) {
                css += "\n" + searx_css;
            }
        }
    }
    tab->webview->ExecuteScript(to_wide(xnc::Blocker::build_inject_css_script(css)).c_str(), nullptr);
    tab->webview->ExecuteScript(to_wide(xnc::Blocker::build_transparent_mica_script(mica)).c_str(), nullptr);
}

void sync_toolbar() {
    Tab* tab = current_tab();
    if (!tab) return;

    std::string title = tab->is_private ? ("XNC (Private) — " + tab->title) : ("XNC — " + tab->title);
    SetWindowTextW(g_app.hwnd, to_wide(title).c_str());

    if (GetFocus() != g_app.address_edit) {
        std::string display_url = is_newtab_url(tab->url) ? "" : tab->url;
        SetWindowTextW(g_app.address_edit, to_wide(display_url).c_str());
    }
    sync_search_placeholder();
    invalidate_chrome();
}

void compute_layout() {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    int w = rc.right - rc.left;
    g_app.hitboxes.clear();

    // Top Titlebar Tab Strip (y = 0 .. tab_strip_height)
    int cap_w = px(44);
    int cap_h = px(32);
    int close_x = w - cap_w;
    int max_x = close_x - cap_w;
    int min_x = max_x - cap_w;

    g_app.hitboxes.push_back({HitKind::MinBtn, -1, {min_x, 0, min_x + cap_w, cap_h}, true});
    g_app.hitboxes.push_back({HitKind::MaxBtn, -1, {max_x, 0, max_x + cap_w, cap_h}, true});
    g_app.hitboxes.push_back({HitKind::CloseBtn, -1, {close_x, 0, close_x + cap_w, cap_h}, true});

    int new_btn_sz = px(26);
    int tx = px(10);
    int ty = px(6);
    int th = px(28);
    int avail_tabs = (std::max)(px(120), min_x - px(16) - new_btn_sz - px(12));
    int count = static_cast<int>(g_app.tabs.size());
    int tab_w = count > 0 ? (std::min)(px(196), (std::max)(px(92), (avail_tabs - (count - 1) * px(5)) / count)) : px(160);

    for (int i = 0; i < count; ++i) {
        int x0 = tx;
        int x1 = x0 + tab_w;
        int c_sz = px(18);
        RECT tab_rc{x0, ty, x1, ty + th};
        RECT x_rc{x1 - px(6) - c_sz, ty + (th - c_sz) / 2, x1 - px(6), ty + (th + c_sz) / 2};
        g_app.hitboxes.push_back({HitKind::TabClose, i, x_rc, true});
        g_app.hitboxes.push_back({HitKind::TabSelect, i, tab_rc, true});
        tx = x1 + px(5);
    }

    int plus_x = (std::min)(tx + px(2), min_x - new_btn_sz - px(10));
    g_app.hitboxes.push_back({HitKind::NewTab, -1, {plus_x, ty + px(1), plus_x + new_btn_sz, ty + th - px(1)}, true});

    // Second Toolbar Row (y = tab_strip_height .. chrome_height)
    int bar_y = tab_strip_height() + px(5);
    int btn_sz = px(28);
    int gap = px(4);
    Tab* tab = current_tab();

    int lx = px(10);
    auto push_left = [&](HitKind k, bool enabled) {
        g_app.hitboxes.push_back({k, -1, {lx, bar_y, lx + btn_sz, bar_y + btn_sz}, enabled});
        lx += btn_sz + gap;
    };
    push_left(HitKind::Back, tab ? tab->can_go_back : false);
    push_left(HitKind::Forward, tab ? tab->can_go_forward : false);
    push_left(HitKind::Reload, tab != nullptr);
    push_left(HitKind::Shield, true);
    push_left(HitKind::Zapper, tab != nullptr);
    push_left(HitKind::Mica, true);

    int rx = w - px(10);
    auto push_right = [&](HitKind k) {
        rx -= btn_sz;
        g_app.hitboxes.push_back({k, -1, {rx, bar_y, rx + btn_sz, bar_y + btn_sz}, true});
        rx -= gap;
    };
    push_right(HitKind::Menu);
    push_right(HitKind::Dev);
    push_right(HitKind::PrivateTab);
    push_right(HitKind::Bookmark);

    int pill_left = lx + px(4);
    int pill_right = (std::max)(pill_left + px(100), rx);
    g_app.address_pill_rc = {pill_left, bar_y, pill_right, bar_y + btn_sz};

    if (g_app.address_edit) {
        ShowWindow(g_app.address_edit, g_app.is_fullscreen ? SW_HIDE : SW_SHOW);
        if (!g_app.is_fullscreen) {
            int pad_x = px(11);
            int edit_h = px(18);
            int edit_y = bar_y + (btn_sz - edit_h) / 2;
            MoveWindow(g_app.address_edit,
                       pill_left + pad_x,
                       edit_y,
                       (std::max)(40, (pill_right - pill_left) - pad_x * 2),
                       edit_h,
                       TRUE);
        }
    }

    RECT wv_rc = webview_bounds();
    for (size_t i = 0; i < g_app.tabs.size(); ++i) {
        if (g_app.tabs[i]->controller) {
            g_app.tabs[i]->controller->put_Bounds(wv_rc);
            g_app.tabs[i]->controller->put_IsVisible(
                static_cast<int>(i) == g_app.selected_index ? TRUE : FALSE);
        }
    }
}

HitBox hit_test_chrome(POINT pt) {
    for (const auto& hb : g_app.hitboxes) {
        if (PtInRect(&hb.rect, pt)) return hb;
    }
    return {};
}

// MARK: - GDI+ Drawing

Gdiplus::RectF to_rectf(const RECT& r) {
    return Gdiplus::RectF(
        static_cast<Gdiplus::REAL>(r.left),
        static_cast<Gdiplus::REAL>(r.top),
        static_cast<Gdiplus::REAL>(r.right - r.left),
        static_cast<Gdiplus::REAL>(r.bottom - r.top));
}

void add_rounded_rect(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, Gdiplus::REAL radius) {
    Gdiplus::REAL d = radius * 2.0f;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void fill_rounded_rect(Gdiplus::Graphics& g, RECT r, float radius, Gdiplus::Color fill, Gdiplus::Color border = Gdiplus::Color(0, 0, 0, 0)) {
    Gdiplus::GraphicsPath path;
    add_rounded_rect(path, to_rectf(r), radius);
    Gdiplus::SolidBrush brush(fill);
    g.FillPath(&brush, &path);
    if (border.GetA() > 0) {
        Gdiplus::Pen pen(border, 1.0f);
        g.DrawPath(&pen, &path);
    }
}

void draw_glyph_icon(Gdiplus::Graphics& g, HitKind kind, RECT rc, bool hovered, bool active, bool enabled, bool loading, bool starred, bool host_paused, bool blocker_on) {
    if (hovered && enabled) {
        fill_rounded_rect(g, rc, static_cast<float>(px(6)), Gdiplus::Color(32, 255, 255, 255));
    }
    if (active && (kind == HitKind::Mica || kind == HitKind::Zapper)) {
        Gdiplus::Color bg = (kind == HitKind::Zapper)
            ? Gdiplus::Color(46, 239, 68, 68)
            : Gdiplus::Color(55, 59, 130, 246);
        fill_rounded_rect(g, rc, static_cast<float>(px(6)), bg);
    }

    float cx = (rc.left + rc.right) * 0.5f;
    float cy = (rc.top + rc.bottom) * 0.5f;
    float s = g_app.dpi_scale;

    Gdiplus::Color fg = enabled ? Gdiplus::Color(235, 238, 244, 250) : Gdiplus::Color(90, 180, 188, 200);
    if (kind == HitKind::Shield) {
        fg = !blocker_on ? Gdiplus::Color(150, 148, 163, 184)
            : (host_paused ? Gdiplus::Color(255, 249, 115, 22) : Gdiplus::Color(255, 52, 211, 153));
    } else if (kind == HitKind::Zapper && active) {
        fg = Gdiplus::Color(255, 248, 113, 113);
    } else if (kind == HitKind::Mica && active) {
        fg = Gdiplus::Color(255, 96, 165, 250);
    } else if (kind == HitKind::Bookmark && starred) {
        fg = Gdiplus::Color(255, 250, 204, 21);
    }

    Gdiplus::Pen pen(fg, 1.5f * s);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush brush(fg);

    switch (kind) {
    case HitKind::Back: {
        Gdiplus::PointF pts[3] = {{cx + 2.5f * s, cy - 4.5f * s}, {cx - 2.5f * s, cy}, {cx + 2.5f * s, cy + 4.5f * s}};
        g.DrawLines(&pen, pts, 3);
        break;
    }
    case HitKind::Forward: {
        Gdiplus::PointF pts[3] = {{cx - 2.5f * s, cy - 4.5f * s}, {cx + 2.5f * s, cy}, {cx - 2.5f * s, cy + 4.5f * s}};
        g.DrawLines(&pen, pts, 3);
        break;
    }
    case HitKind::Reload: {
        if (loading) {
            g.DrawLine(&pen, cx - 4 * s, cy - 4 * s, cx + 4 * s, cy + 4 * s);
            g.DrawLine(&pen, cx + 4 * s, cy - 4 * s, cx - 4 * s, cy + 4 * s);
        } else {
            g.DrawArc(&pen, cx - 5 * s, cy - 5 * s, 10 * s, 10 * s, -45.0f, 290.0f);
            Gdiplus::PointF pts[3] = {{cx + 2.0f * s, cy - 6.0f * s}, {cx + 5.0f * s, cy - 3.0f * s}, {cx + 1.5f * s, cy - 1.5f * s}};
            g.DrawLines(&pen, pts, 3);
        }
        break;
    }
    case HitKind::Shield: {
        Gdiplus::PointF pts[5] = {
            {cx, cy - 6.0f * s},
            {cx + 5.2f * s, cy - 3.6f * s},
            {cx + 4.2f * s, cy + 2.4f * s},
            {cx, cy + 6.2f * s},
            {cx - 4.2f * s, cy + 2.4f * s}
        };
        Gdiplus::PointF poly[6] = {pts[0], pts[1], pts[2], pts[3], pts[4], {cx - 5.2f * s, cy - 3.6f * s}};
        if (blocker_on && !host_paused) g.FillPolygon(&brush, poly, 6);
        else g.DrawPolygon(&pen, poly, 6);
        break;
    }
    case HitKind::Zapper: {
        Gdiplus::PointF bolt[6] = {
            {cx + 1.2f * s, cy - 6.2f * s},
            {cx - 4.2f * s, cy + 0.5f * s},
            {cx + 0.2f * s, cy + 0.5f * s},
            {cx - 1.2f * s, cy + 6.2f * s},
            {cx + 4.2f * s, cy - 0.5f * s},
            {cx - 0.2f * s, cy - 0.5f * s}
        };
        g.FillPolygon(&brush, bolt, 6);
        break;
    }
    case HitKind::Mica: {
        Gdiplus::PointF star[8] = {
            {cx, cy - 6.2f * s}, {cx + 1.6f * s, cy - 1.6f * s},
            {cx + 6.2f * s, cy}, {cx + 1.6f * s, cy + 1.6f * s},
            {cx, cy + 6.2f * s}, {cx - 1.6f * s, cy + 1.6f * s},
            {cx - 6.2f * s, cy}, {cx - 1.6f * s, cy - 1.6f * s}
        };
        g.FillPolygon(&brush, star, 8);
        break;
    }
    case HitKind::Bookmark: {
        Gdiplus::PointF pts[10];
        for (int i = 0; i < 10; ++i) {
            double ang = (-90.0 + i * 36.0) * 3.14159265358979323846 / 180.0;
            float r = (i % 2 == 0) ? 6.2f * s : 2.7f * s;
            pts[i] = {cx + static_cast<float>(std::cos(ang)) * r,
                      cy + static_cast<float>(std::sin(ang)) * r};
        }
        if (starred) g.FillPolygon(&brush, pts, 10);
        else g.DrawPolygon(&pen, pts, 10);
        break;
    }
    case HitKind::PrivateTab: {
        g.DrawEllipse(&pen, cx - 5.5f * s, cy - 3.2f * s, 11.0f * s, 6.4f * s);
        g.FillEllipse(&brush, cx - 1.6f * s, cy - 1.6f * s, 3.2f * s, 3.2f * s);
        g.DrawLine(&pen, cx - 5.2f * s, cy + 4.5f * s, cx + 5.2f * s, cy - 4.5f * s);
        break;
    }
    case HitKind::Dev: {
        Gdiplus::PointF l[3] = {{cx - 2.2f * s, cy - 3.5f * s}, {cx - 5.8f * s, cy}, {cx - 2.2f * s, cy + 3.5f * s}};
        Gdiplus::PointF r[3] = {{cx + 2.2f * s, cy - 3.5f * s}, {cx + 5.8f * s, cy}, {cx + 2.2f * s, cy + 3.5f * s}};
        g.DrawLines(&pen, l, 3);
        g.DrawLines(&pen, r, 3);
        g.DrawLine(&pen, cx + 1.0f * s, cy - 4.8f * s, cx - 1.0f * s, cy + 4.8f * s);
        break;
    }
    case HitKind::Menu: {
        g.DrawLine(&pen, cx - 5.5f * s, cy - 3.8f * s, cx + 5.5f * s, cy - 3.8f * s);
        g.DrawLine(&pen, cx - 5.5f * s, cy, cx + 5.5f * s, cy);
        g.DrawLine(&pen, cx - 5.5f * s, cy + 3.8f * s, cx + 5.5f * s, cy + 3.8f * s);
        break;
    }
    case HitKind::NewTab: {
        g.DrawLine(&pen, cx - 4.5f * s, cy, cx + 4.5f * s, cy);
        g.DrawLine(&pen, cx, cy - 4.5f * s, cx, cy + 4.5f * s);
        break;
    }
    default: break;
    }
}

void paint_chrome(HDC hdc) {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    if (chrome_height() <= 0) return;
    RECT chrome_rc{0, 0, rc.right, chrome_height()};

    HDC buf_hdc = nullptr;
    HPAINTBUFFER pbp = BeginBufferedPaint(hdc, &chrome_rc, BPBF_TOPDOWNDIB, nullptr, &buf_hdc);
    if (!pbp || !buf_hdc) return;

    BufferedPaintClear(pbp, nullptr);

    {
        Gdiplus::Graphics g(buf_hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        float s = g_app.dpi_scale;
        Tab* tab = current_tab();
        std::string host = tab ? xnc::extract_host(tab->url) : "";
        bool paused = !host.empty() && g_app.store.is_host_allowlisted(host);

        Gdiplus::FontFamily font_family(L"Segoe UI");
        Gdiplus::Font tab_font(&font_family, 11.5f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font tab_font_bold(&font_family, 11.5f * s, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentNear);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        // Subtle glass wash across chrome header
        Gdiplus::SolidBrush wash(Gdiplus::Color(48, 12, 14, 20));
        g.FillRectangle(&wash, 0, 0, static_cast<int>(chrome_rc.right), static_cast<int>(chrome_rc.bottom));

        // Draw tabs + caption buttons + toolbar icons
        for (const auto& hb : g_app.hitboxes) {
            bool is_hov = (hb.kind == g_app.hovered.kind && hb.index == g_app.hovered.index);
            if (hb.kind == HitKind::TabSelect) {
                if (hb.index < 0 || static_cast<size_t>(hb.index) >= g_app.tabs.size()) continue;
                bool selected = (hb.index == g_app.selected_index);
                Gdiplus::Color pill_bg = selected
                    ? Gdiplus::Color(46, 255, 255, 255)
                    : (is_hov ? Gdiplus::Color(24, 255, 255, 255) : Gdiplus::Color(12, 255, 255, 255));
                fill_rounded_rect(g, hb.rect, 7.0f * s, pill_bg);

                const auto& t = g_app.tabs[static_cast<size_t>(hb.index)];
                std::wstring label = to_wide((t->is_private ? "Private · " : "") + t->title);
                Gdiplus::RectF text_rc(
                    static_cast<float>(hb.rect.left) + 10.0f * s,
                    static_cast<float>(hb.rect.top),
                    static_cast<float>(hb.rect.right - hb.rect.left) - 28.0f * s,
                    static_cast<float>(hb.rect.bottom - hb.rect.top));
                Gdiplus::SolidBrush text_br(selected ? Gdiplus::Color(255, 248, 250, 252) : Gdiplus::Color(210, 203, 213, 225));
                g.DrawString(label.c_str(), -1, selected ? &tab_font_bold : &tab_font, text_rc, &sf, &text_br);
            }
        }

        // Draw Tab Close '×' on top of tab pills
        for (const auto& hb : g_app.hitboxes) {
            if (hb.kind == HitKind::TabClose) {
                bool is_hov = (hb.kind == g_app.hovered.kind && hb.index == g_app.hovered.index);
                if (is_hov) {
                    fill_rounded_rect(g, hb.rect, 4.0f * s, Gdiplus::Color(40, 255, 255, 255));
                }
                float cx = (hb.rect.left + hb.rect.right) * 0.5f;
                float cy = (hb.rect.top + hb.rect.bottom) * 0.5f;
                Gdiplus::Pen xp(is_hov ? Gdiplus::Color(255, 255, 255, 255) : Gdiplus::Color(180, 203, 213, 225), 1.3f * s);
                xp.SetStartCap(Gdiplus::LineCapRound);
                xp.SetEndCap(Gdiplus::LineCapRound);
                g.DrawLine(&xp, cx - 3.2f * s, cy - 3.2f * s, cx + 3.2f * s, cy + 3.2f * s);
                g.DrawLine(&xp, cx + 3.2f * s, cy - 3.2f * s, cx - 3.2f * s, cy + 3.2f * s);
            } else if (hb.kind == HitKind::MinBtn || hb.kind == HitKind::MaxBtn || hb.kind == HitKind::CloseBtn) {
                bool is_hov = (hb.kind == g_app.hovered.kind);
                if (is_hov) {
                    Gdiplus::Color bg = (hb.kind == HitKind::CloseBtn)
                        ? Gdiplus::Color(235, 196, 43, 28)
                        : Gdiplus::Color(30, 255, 255, 255);
                    Gdiplus::SolidBrush br(bg);
                    g.FillRectangle(
                        &br,
                        static_cast<int>(hb.rect.left),
                        static_cast<int>(hb.rect.top),
                        static_cast<int>(hb.rect.right - hb.rect.left),
                        static_cast<int>(hb.rect.bottom - hb.rect.top));
                }
                float cx = (hb.rect.left + hb.rect.right) * 0.5f;
                float cy = (hb.rect.top + hb.rect.bottom) * 0.5f;
                Gdiplus::Pen cp(Gdiplus::Color(240, 241, 245, 249), 1.1f * s);
                if (hb.kind == HitKind::MinBtn) {
                    g.DrawLine(&cp, cx - 5.0f * s, cy, cx + 5.0f * s, cy);
                } else if (hb.kind == HitKind::MaxBtn) {
                    g.DrawRectangle(&cp, cx - 4.5f * s, cy - 4.5f * s, 9.0f * s, 9.0f * s);
                } else {
                    g.DrawLine(&cp, cx - 4.5f * s, cy - 4.5f * s, cx + 4.5f * s, cy + 4.5f * s);
                    g.DrawLine(&cp, cx + 4.5f * s, cy - 4.5f * s, cx - 4.5f * s, cy + 4.5f * s);
                }
            } else if (hb.kind != HitKind::TabSelect) {
                bool is_hov = (hb.kind == g_app.hovered.kind);
                bool active = (hb.kind == HitKind::Zapper && tab && tab->zapper_active) ||
                              (hb.kind == HitKind::Mica && g_app.store.transparent_websites());
                bool starred = (tab && !tab->url.empty() && g_app.store.is_bookmarked(tab->url));
                draw_glyph_icon(
                    g, hb.kind, hb.rect, is_hov, active, hb.enabled,
                    tab ? tab->is_loading : false, starred, paused, g_app.blocker.enabled());
            }
        }

        // Rounded translucent pill address bar
        fill_rounded_rect(
            g,
            g_app.address_pill_rc,
            8.0f * s,
            Gdiplus::Color(235, 30, 33, 43),
            Gdiplus::Color(70, 255, 255, 255));

        // 2px progress bar + hairline separator
        if (tab && tab->is_loading) {
            int pw = static_cast<int>(chrome_rc.right * std::clamp(tab->progress, 0.08, 1.0));
            Gdiplus::SolidBrush prog(Gdiplus::Color(255, 59, 130, 246));
            g.FillRectangle(&prog, 0, static_cast<int>(chrome_rc.bottom - px(3)), pw, px(2));
        }
        Gdiplus::SolidBrush div(Gdiplus::Color(55, 255, 255, 255));
        g.FillRectangle(&div, 0, static_cast<int>(chrome_rc.bottom - 1), static_cast<int>(chrome_rc.right), 1);
    }

    EndBufferedPaint(pbp, TRUE);
}

// MARK: - Actions & Modals

void add_tab(const std::string& initial_url, bool is_private);
bool process_shortcut(UINT vkey, bool ctrl, bool shift, bool alt);

void select_tab(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_app.tabs.size()) return;
    g_app.selected_index = index;
    compute_layout();
    sync_toolbar();
}

void select_next_tab(int delta) {
    if (g_app.tabs.size() <= 1) return;
    int n = static_cast<int>(g_app.tabs.size());
    select_tab((g_app.selected_index + delta + n) % n);
}

void close_tab_at(int index) {
    if (index < 0 || static_cast<size_t>(index) >= g_app.tabs.size()) return;
    auto tab = g_app.tabs[static_cast<size_t>(index)];
    tab->closed = true;
    if (tab->controller) {
        tab->controller->Close();
        tab->controller.Reset();
    }
    tab->webview.Reset();
    g_app.tabs.erase(g_app.tabs.begin() + index);

    if (g_app.tabs.empty()) {
        g_app.selected_index = -1;
        DestroyWindow(g_app.hwnd);
        return;
    }
    int next_idx = g_app.selected_index;
    if (index < g_app.selected_index) {
        next_idx = g_app.selected_index - 1;
    } else if (index == g_app.selected_index) {
        next_idx = std::min(index, static_cast<int>(g_app.tabs.size()) - 1);
    }
    select_tab(next_idx);
}

void toggle_transparent_mica() {
    bool next = !g_app.store.transparent_websites();
    g_app.store.set_transparent_websites(next);
    apply_mica_backdrop(g_app.hwnd, next, chrome_height());
    InvalidateRect(g_app.hwnd, nullptr, TRUE);
    for (auto& tab : g_app.tabs) {
        inject_cosmetic_and_mica(tab.get());
    }
    sync_toolbar();
}

void toggle_fullscreen() {
    if (!g_app.hwnd) return;
    DWORD style = static_cast<DWORD>(GetWindowLongPtrW(g_app.hwnd, GWL_STYLE));
    if (!g_app.is_fullscreen) {
        MONITORINFO mi{sizeof(mi)};
        if (GetWindowPlacement(g_app.hwnd, &g_app.prev_placement) &&
            GetMonitorInfoW(MonitorFromWindow(g_app.hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            g_app.is_fullscreen = true;
            SetWindowLongPtrW(g_app.hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(
                g_app.hwnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        g_app.is_fullscreen = false;
        SetWindowLongPtrW(g_app.hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_app.hwnd, &g_app.prev_placement);
        SetWindowPos(
            g_app.hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    apply_mica_backdrop(g_app.hwnd, g_app.store.transparent_websites(), chrome_height());
    compute_layout();
    invalidate_chrome();
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
    tab->webview->ExecuteScript(
        to_wide(js).c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [tab = g_app.tabs[static_cast<size_t>(g_app.selected_index)]](HRESULT, LPCWSTR json_res) -> HRESULT {
                if (tab->closed) return S_OK;
                std::string res = unescape_json_result(to_utf8(json_res));
                tab->zapper_active = (res == "on");
                if (tab.get() == current_tab()) invalidate_chrome();
                return S_OK;
            }).Get());
}

void set_user_agent(const std::string& ua) {
    Tab* tab = current_tab();
    if (!tab || !tab->webview) return;
    tab->custom_ua = ua;
    ComPtr<ICoreWebView2Settings> base_settings;
    if (SUCCEEDED(tab->webview->get_Settings(&base_settings)) && base_settings) {
        ComPtr<ICoreWebView2Settings2> s2;
        if (SUCCEEDED(base_settings.As(&s2)) && s2) {
            s2->put_UserAgent(to_wide(ua).c_str());
            tab->webview->Reload();
        }
    }
}

void clear_browsing_data() {
#if defined(__ICoreWebView2_13_INTERFACE_DEFINED__) && defined(__ICoreWebView2Profile2_INTERFACE_DEFINED__)
    Tab* tab = current_tab();
    if (!tab || !tab->webview) return;
    ComPtr<ICoreWebView2_13> w13;
    if (SUCCEEDED(tab->webview.As(&w13)) && w13) {
        ComPtr<ICoreWebView2Profile> prof;
        if (SUCCEEDED(w13->get_Profile(&prof)) && prof) {
            ComPtr<ICoreWebView2Profile2> p2;
            if (SUCCEEDED(prof.As(&p2)) && p2) {
                p2->ClearBrowsingDataAll(
                    Callback<ICoreWebView2ClearBrowsingDataCompletedHandler>(
                        [](HRESULT) -> HRESULT {
                            if (auto* t = current_tab(); t && t->webview) t->webview->Reload();
                            return S_OK;
                        }).Get());
                return;
            }
        }
    }
#endif
    if (auto* t = current_tab(); t && t->webview) t->webview->Reload();
}

void view_page_source() {
    if (g_app.selected_index < 0 || static_cast<size_t>(g_app.selected_index) >= g_app.tabs.size()) return;
    auto tab = g_app.tabs[static_cast<size_t>(g_app.selected_index)];
    if (!tab || !tab->webview) return;
    std::string source_url = tab->url;
    bool priv = tab->is_private;
    tab->webview->ExecuteScript(
        L"document.documentElement.outerHTML",
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [tab, source_url, priv](HRESULT hr, LPCWSTR json_res) -> HRESULT {
                if (FAILED(hr) || !json_res || tab->closed || !IsWindow(g_app.hwnd)) return S_OK;
                std::string html = unescape_json_result(to_utf8(json_res));
                std::string doc =
                    "<!doctype html><html><head><meta charset='utf-8'>"
                    "<title>Source: " + escape_html(source_url) + "</title>"
                    "<style>body{margin:0;padding:20px;background:#111318;color:#e2e8f0;"
                    "font:12.5px/1.55 Consolas,'Cascadia Mono',monospace;}"
                    "header{margin-bottom:12px;color:#94a3b8;font-size:12px;}"
                    "pre{white-space:pre-wrap;word-break:break-word;}</style></head><body>"
                    "<header>Source of " + escape_html(source_url) + "</header>"
                    "<pre>" + escape_html(html) + "</pre></body></html>";
                add_tab("about:blank", priv);
                if (!g_app.tabs.empty()) {
                    auto new_tab = g_app.tabs.back();
                    new_tab->title = "Source — " + xnc::display_host(source_url);
                    new_tab->url = "view-source:" + source_url;
                    new_tab->pending_html = doc;
                    if (new_tab->webview) {
                        new_tab->pending_html.clear();
                        new_tab->webview->NavigateToString(to_wide(doc).c_str());
                    }
                    sync_toolbar();
                }
                return S_OK;
            }).Get());
}

// Reusable dark-themed Win32 modal prompt window
struct ModalPromptResult {
    int button = 0; // 1=Primary, 2=Secondary, 3=Tertiary, 0=Cancel
    std::string text;
};

struct ModalState {
    bool done = false;
    int clicked = 0;
    HWND input = nullptr;
    HWND output = nullptr;
    bool is_js_console = false;
};

constexpr int kModalBtn1 = 101;
constexpr int kModalBtn2 = 102;
constexpr int kModalBtn3 = 103;

std::string normalize_crlf(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\n' && (i == 0 || in[i - 1] != '\r')) out.push_back('\r');
        out.push_back(in[i]);
    }
    return out;
}

LRESULT CALLBACK ModalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<ModalState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(241, 245, 249));
        SetBkColor(hdc, RGB(30, 33, 43));
        if (!g_app.edit_bg_brush) g_app.edit_bg_brush = CreateSolidBrush(RGB(30, 33, 43));
        return reinterpret_cast<LRESULT>(g_app.edit_bg_brush);
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (st && id == kModalBtn1 && st->is_js_console) {
            wchar_t buf[4096]{};
            GetWindowTextW(st->input, buf, 4096);
            if (auto* t = current_tab(); t && t->webview) {
                HWND out_hwnd = st->output;
                t->webview->ExecuteScript(
                    buf,
                    Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                        [out_hwnd](HRESULT hr, LPCWSTR res) -> HRESULT {
                            if (IsWindow(out_hwnd)) {
                                std::string s = FAILED(hr)
                                    ? ("Error HRESULT: " + std::to_string(hr))
                                    : unescape_json_result(to_utf8(res));
                                SetWindowTextW(out_hwnd, to_wide(normalize_crlf(s)).c_str());
                            }
                            return S_OK;
                        }).Get());
            }
            return 0;
        }
        if (st && (id == kModalBtn1 || id == kModalBtn2 || id == kModalBtn3 || id == IDCANCEL)) {
            if (id == IDCANCEL) st->clicked = 0;
            else if (id == kModalBtn1) st->clicked = 1;
            else if (id == kModalBtn2) st->clicked = 2;
            else st->clicked = 3;
            st->done = true;
        }
        return 0;
    }
    case WM_CLOSE:
        if (st) {
            st->clicked = 0;
            st->done = true;
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

ModalPromptResult run_dark_modal(
    const wchar_t* title,
    const wchar_t* label_text,
    const std::string& initial_input,
    const wchar_t* btn1,
    const wchar_t* btn2,
    const wchar_t* btn3,
    bool js_console) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW mwc{};
        mwc.cbSize = sizeof(mwc);
        mwc.lpfnWndProc = ModalWndProc;
        mwc.hInstance = g_app.hinstance;
        mwc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mwc.hbrBackground = CreateSolidBrush(RGB(22, 24, 31));
        mwc.lpszClassName = L"XNCModalWindow";
        RegisterClassExW(&mwc);
        registered = true;
    }

    ModalState st{};
    st.is_js_console = js_console;
    int mw = px(480);
    int mh = js_console ? px(310) : px(175);
    RECT parent_rc{};
    GetWindowRect(g_app.hwnd, &parent_rc);
    int mx = parent_rc.left + ((parent_rc.right - parent_rc.left) - mw) / 2;
    int my = parent_rc.top + ((parent_rc.bottom - parent_rc.top) - mh) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"XNCModalWindow", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        mx, my, mw, mh, g_app.hwnd, nullptr, g_app.hinstance, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowLongPtrW(dlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&st));

    HWND lbl = CreateWindowW(L"STATIC", label_text, WS_CHILD | WS_VISIBLE,
                             px(16), px(14), mw - px(32), px(22), dlg, nullptr, nullptr, nullptr);
    st.input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", to_wide(initial_input).c_str(),
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               px(16), px(40), mw - px(32), px(26), dlg, nullptr, nullptr, nullptr);

    int btn_y = mh - px(74);
    if (js_console) {
        st.output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"// Output appears here",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                                    px(16), px(74), mw - px(32), px(145), dlg, nullptr, nullptr, nullptr);
        SendMessageW(st.output, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
    }

    int bx = mw - px(16);
    auto add_btn = [&](int id, const wchar_t* text, int w) {
        if (!text) return;
        bx -= w;
        HWND b = CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE,
                               bx, btn_y, w, px(28), dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
        bx -= px(8);
    };
    add_btn(IDCANCEL, L"Close", px(74));
    if (btn3) add_btn(kModalBtn3, btn3, px(120));
    if (btn2) add_btn(kModalBtn2, btn2, px(106));
    add_btn(kModalBtn1, btn1, px(84));

    SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
    SendMessageW(st.input, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);

    EnableWindow(g_app.hwnd, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.input);

    MSG msg{};
    while (!st.done) {
        BOOL gm = GetMessageW(&msg, nullptr, 0, 0);
        if (gm <= 0) {
            if (gm == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.clicked = 0;
            st.done = true;
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetFocus() == st.input) {
            SendMessageW(dlg, WM_COMMAND, kModalBtn1, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    wchar_t out_buf[4096]{};
    GetWindowTextW(st.input, out_buf, 4096);
    ModalPromptResult res{st.clicked, to_utf8(out_buf)};

    EnableWindow(g_app.hwnd, TRUE);
    DestroyWindow(dlg);
    SetForegroundWindow(g_app.hwnd);
    return res;
}

void open_searxng_dialog() {
    auto res = run_dark_modal(
        L"Custom SearXNG Search Engine",
        L"Enter your SearXNG base URL (e.g. https://searx.example.com or http://localhost:8080):",
        g_app.store.search_engine_url(),
        L"Save", L"Local :8080", L"DuckDuckGo", false);
    if (res.button == 1) g_app.store.set_search_engine_url(res.text);
    else if (res.button == 2) g_app.store.set_search_engine_url("http://localhost:8080");
    else if (res.button == 3) g_app.store.set_search_engine_url("");
    else return;

    sync_search_placeholder();
    std::string home = xnc::home_url_for_search_engine(g_app.store.search_engine_url());
    if (auto* t = current_tab(); t && t->webview) {
        t->webview->Navigate(to_wide(home).c_str());
    }
    invalidate_chrome();
}

void open_js_console() {
    run_dark_modal(
        L"XNC JavaScript Console",
        L"Evaluate JavaScript in the active tab:",
        "document.title",
        L"Run", nullptr, nullptr, true);
}

void show_shield_menu(POINT screen_pt) {
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

    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                              screen_pt.x, screen_pt.y, 0, g_app.hwnd, nullptr);
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

void show_dev_menu(POINT screen_pt) {
    Tab* tab = current_tab();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdDevInspector, L"Open DevTools (Ctrl+Shift+I / F12)");
    AppendMenuW(menu, MF_STRING, kCmdDevConsole, L"JavaScript Console... (Ctrl+Shift+J)");
    AppendMenuW(menu, MF_STRING, kCmdDevSource, L"View Page Source (Ctrl+U)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdDevZapper, L"Element Zapper (Ctrl+Shift+Z)");
    AppendMenuW(menu, MF_STRING, kCmdDevToggleMica,
                g_app.store.transparent_websites() ? L"Disable Transparent Mica Websites (Ctrl+Shift+M)" : L"Enable Transparent Mica Websites (Ctrl+Shift+M)");
    AppendMenuW(menu, MF_STRING, kCmdDevToggleJS,
                (tab && !tab->js_enabled) ? L"Enable JavaScript on Tab" : L"Disable JavaScript on Tab");

    HMENU ua_sub = CreatePopupMenu();
    AppendMenuW(ua_sub, MF_STRING, kCmdDevUADefault, L"Default (WebView2)");
    AppendMenuW(ua_sub, MF_STRING, kCmdDevUASafari, L"Safari (macOS)");
    AppendMenuW(ua_sub, MF_STRING, kCmdDevUAFirefox, L"Firefox (Desktop)");
    AppendMenuW(ua_sub, MF_STRING, kCmdDevUAMobile, L"Mobile Safari (iPhone)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(ua_sub), L"User-Agent");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdDevClearData, L"Clear Cookies & Cache");

    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                              screen_pt.x, screen_pt.y, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == kCmdDevInspector && tab && tab->webview) tab->webview->OpenDevToolsWindow();
    else if (cmd == kCmdDevConsole) open_js_console();
    else if (cmd == kCmdDevSource) view_page_source();
    else if (cmd == kCmdDevZapper) trigger_zapper();
    else if (cmd == kCmdDevToggleMica) toggle_transparent_mica();
    else if (cmd == kCmdDevToggleJS && tab && tab->webview) {
        tab->js_enabled = !tab->js_enabled;
        ComPtr<ICoreWebView2Settings> settings;
        if (SUCCEEDED(tab->webview->get_Settings(&settings)) && settings) {
            settings->put_IsScriptEnabled(tab->js_enabled ? TRUE : FALSE);
        }
        tab->webview->Reload();
    } else if (cmd == kCmdDevUADefault) set_user_agent("");
    else if (cmd == kCmdDevUASafari) set_user_agent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15");
    else if (cmd == kCmdDevUAFirefox) set_user_agent("Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:132.0) Gecko/20100101 Firefox/132.0");
    else if (cmd == kCmdDevUAMobile) set_user_agent("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1");
    else if (cmd == kCmdDevClearData) clear_browsing_data();
}

void show_main_menu(POINT screen_pt) {
    HMENU menu = CreatePopupMenu();

    std::string searx_label = g_app.store.search_engine_url().empty()
        ? "Search Engine: DuckDuckGo (Set SearXNG...)"
        : ("Search Engine: " + g_app.store.search_engine_url() + "...");
    AppendMenuW(menu, MF_STRING, kCmdSearxConfigure, to_wide(searx_label).c_str());
    AppendMenuW(menu, MF_STRING, kCmdToggleMicaMenu,
                g_app.store.transparent_websites() ? L"Transparent Mica Websites: ON (Ctrl+Shift+M)" : L"Transparent Mica Websites: OFF (Ctrl+Shift+M)");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Bookmarks");
    for (size_t i = 0; i < g_app.store.bookmarks().size() && i < 20; ++i) {
        AppendMenuW(menu, MF_STRING, kCmdBookmarkBase + i, to_wide("  " + g_app.store.bookmarks()[i].title).c_str());
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent History");
    for (size_t i = 0; i < g_app.store.history().size() && i < 15; ++i) {
        AppendMenuW(menu, MF_STRING, kCmdHistoryBase + i, to_wide("  " + g_app.store.history()[i].title).c_str());
    }
    if (!g_app.store.history().empty()) {
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCmdClearHistory, L"Clear History");
    }

    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN,
                              screen_pt.x, screen_pt.y, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == kCmdSearxConfigure) open_searxng_dialog();
    else if (cmd == kCmdToggleMicaMenu) toggle_transparent_mica();
    else if (cmd == kCmdClearHistory) g_app.store.clear_history();
    else if (cmd >= kCmdBookmarkBase && cmd < kCmdHistoryBase) {
        size_t idx = cmd - kCmdBookmarkBase;
        if (idx < g_app.store.bookmarks().size()) {
            if (auto* t = current_tab(); t && t->webview) {
                t->webview->Navigate(to_wide(g_app.store.bookmarks()[idx].url).c_str());
            }
        }
    } else if (cmd >= kCmdHistoryBase && cmd < kCmdClearHistory) {
        size_t idx = cmd - kCmdHistoryBase;
        if (idx < g_app.store.history().size()) {
            if (auto* t = current_tab(); t && t->webview) {
                t->webview->Navigate(to_wide(g_app.store.history()[idx].url).c_str());
            }
        }
    }
}

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

#ifdef __ICoreWebView2_3_INTERFACE_DEFINED__
    if (auto newtab_path = find_shared_file("newtab.html"); !newtab_path.empty()) {
        ComPtr<ICoreWebView2_3> w3;
        if (SUCCEEDED(tab->webview.As(&w3)) && w3) {
            std::wstring shared_dir = newtab_path.parent_path().wstring();
            w3->SetVirtualHostNameToFolderMapping(
                L"xnc.local", shared_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        }
    }
#endif

    inject_cosmetic_and_mica(tab.get());

    EventRegistrationToken token{};
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
        &token);

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
        &token);

    tab->webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR raw = nullptr;
                if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
                std::string msg = to_utf8(raw);
                CoTaskMemFree(raw);
                std::string type = extract_json_string_field(msg, "type");
                if (type == "zapper") {
                    std::string host = extract_json_string_field(msg, "host");
                    std::string sel = extract_json_string_field(msg, "selector");
                    if (!host.empty() && !sel.empty()) {
                        g_app.store.add_custom_selector(host, sel);
                        inject_cosmetic_and_mica(tab.get());
                    }
                } else if (type == "navigate") {
                    std::string target = xnc::resolve_input(
                        extract_json_string_field(msg, "input"), g_app.store.search_engine_url());
                    tab->url = target;
                    if (tab->webview) tab->webview->Navigate(to_wide(target).c_str());
                } else if (type == "setSearxng") {
                    g_app.store.set_search_engine_url(extract_json_string_field(msg, "url"));
                    sync_search_placeholder();
                    for (auto& t : g_app.tabs) {
                        if (is_newtab_url(t->url)) inject_cosmetic_and_mica(t.get());
                    }
                    invalidate_chrome();
                } else if (type == "setMica") {
                    bool wants_mica = msg.find("\"enabled\":true") != std::string::npos ||
                                      msg.find("\"enabled\": true") != std::string::npos;
                    if (wants_mica != g_app.store.transparent_websites()) {
                        toggle_transparent_mica();
                    }
                } else if (type == "action") {
                    std::string act = extract_json_string_field(msg, "name");
                    if (act == "newPrivateTab") add_tab(default_newtab_url(), true);
                    else if (act == "zapper") trigger_zapper();
                    else if (act == "fullscreen") toggle_fullscreen();
                    else if (act == "clearData") clear_browsing_data();
                }
                return S_OK;
            }).Get(),
        &token);

    tab->webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [tab](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                tab->is_loading = true;
                tab->progress = 0.25;
                tab->zapper_active = false;
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        &token);

    tab->webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [tab](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                tab->is_loading = false;
                tab->progress = 1.0;
                BOOL back = FALSE, fwd = FALSE;
                sender->get_CanGoBack(&back);
                sender->get_CanGoForward(&fwd);
                tab->can_go_back = (back == TRUE);
                tab->can_go_forward = (fwd == TRUE);

                LPWSTR uri = nullptr;
                if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                    std::string u = to_utf8(uri);
                    if (u != "about:blank" || tab->url.empty()) tab->url = u;
                    CoTaskMemFree(uri);
                }
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    std::string t = to_utf8(title);
                    if (!t.empty()) tab->title = t;
                    CoTaskMemFree(title);
                }
                if (is_newtab_url(tab->url)) {
                    tab->title = tab->is_private ? "Private Tab" : "New Tab";
                } else {
                    g_app.store.record_history(tab->title, tab->url, tab->is_private);
                }
                inject_cosmetic_and_mica(tab.get());
                if (tab.get() == current_tab()) sync_toolbar();
                else invalidate_chrome();
                return S_OK;
            }).Get(),
        &token);

    tab->webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [tab](ICoreWebView2* sender, IUnknown*) -> HRESULT {
                if (tab->closed) return S_OK;
                if (is_newtab_url(tab->url)) {
                    tab->title = tab->is_private ? "Private Tab" : "New Tab";
                    return S_OK;
                }
                LPWSTR title = nullptr;
                if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                    std::string t = to_utf8(title);
                    if (!t.empty()) tab->title = t;
                    CoTaskMemFree(title);
                }
                if (tab.get() == current_tab()) sync_toolbar();
                else invalidate_chrome();
                return S_OK;
            }).Get(),
        &token);

    tab->webview->add_SourceChanged(
        Callback<ICoreWebView2SourceChangedEventHandler>(
            [tab](ICoreWebView2* sender, ICoreWebView2SourceChangedEventArgs*) -> HRESULT {
                if (tab->closed) return S_OK;
                LPWSTR uri = nullptr;
                if (SUCCEEDED(sender->get_Source(&uri)) && uri) {
                    std::string u = to_utf8(uri);
                    if (u != "about:blank" || tab->url.empty()) tab->url = u;
                    CoTaskMemFree(uri);
                }
                if (is_newtab_url(tab->url)) tab->title = tab->is_private ? "Private Tab" : "New Tab";
                if (tab.get() == current_tab()) sync_toolbar();
                return S_OK;
            }).Get(),
        &token);

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
        &token);

    if (!tab->pending_html.empty()) {
        std::string doc = std::move(tab->pending_html);
        tab->pending_html.clear();
        tab->webview->NavigateToString(to_wide(doc).c_str());
    } else if (!tab->url.empty() && tab->url != "about:blank") {
        tab->webview->Navigate(to_wide(tab->url).c_str());
    }
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
    tab->url = initial_url.empty() ? default_newtab_url() : initial_url;

    g_app.tabs.push_back(tab);
    select_tab(static_cast<int>(g_app.tabs.size()) - 1);
    create_controller_for_tab(tab);
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

void handle_hitbox_click(const HitBox& hb) {
    if (!hb.enabled) return;
    Tab* tab = current_tab();
    auto anchor_pt = [&](const RECT& r, bool right_align) {
        POINT pt{right_align ? r.right : r.left, r.bottom + px(4)};
        ClientToScreen(g_app.hwnd, &pt);
        return pt;
    };

    switch (hb.kind) {
    case HitKind::TabSelect: select_tab(hb.index); break;
    case HitKind::TabClose: close_tab_at(hb.index); break;
    case HitKind::NewTab: add_tab(default_newtab_url(), false); break;
    case HitKind::MinBtn: ShowWindow(g_app.hwnd, SW_MINIMIZE); break;
    case HitKind::MaxBtn:
        ShowWindow(g_app.hwnd, IsZoomed(g_app.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        break;
    case HitKind::CloseBtn: DestroyWindow(g_app.hwnd); break;
    case HitKind::Back: if (tab && tab->webview) tab->webview->GoBack(); break;
    case HitKind::Forward: if (tab && tab->webview) tab->webview->GoForward(); break;
    case HitKind::Reload:
        if (tab && tab->webview) {
            if (tab->is_loading) tab->webview->Stop();
            else tab->webview->Reload();
        }
        break;
    case HitKind::Shield: show_shield_menu(anchor_pt(hb.rect, false)); break;
    case HitKind::Zapper: trigger_zapper(); break;
    case HitKind::Mica: toggle_transparent_mica(); break;
    case HitKind::Bookmark:
        if (tab) {
            g_app.store.toggle_bookmark(tab->title, tab->url);
            sync_toolbar();
        }
        break;
    case HitKind::PrivateTab: add_tab(default_newtab_url(), true); break;
    case HitKind::Dev: show_dev_menu(anchor_pt(hb.rect, true)); break;
    case HitKind::Menu: show_main_menu(anchor_pt(hb.rect, true)); break;
    default: break;
    }
}

bool process_shortcut(UINT vkey, bool ctrl, bool shift, bool alt) {
    if (vkey == VK_F11 && !ctrl && !alt) {
        toggle_fullscreen();
        return true;
    }
    if (vkey == VK_F12) {
        if (auto* t = current_tab(); t && t->webview) t->webview->OpenDevToolsWindow();
        return true;
    }
    if (ctrl && vkey == VK_TAB) {
        select_next_tab(shift ? -1 : 1);
        return true;
    }
    if (ctrl && !shift && !alt) {
        if (vkey == 'T') { add_tab(default_newtab_url(), false); return true; }
        if (vkey == 'W') { close_tab_at(g_app.selected_index); return true; }
        if (vkey == 'L') {
            SetFocus(g_app.address_edit);
            SendMessageW(g_app.address_edit, EM_SETSEL, 0, -1);
            return true;
        }
        if (vkey == 'R') {
            if (auto* t = current_tab(); t && t->webview) {
                if (t->is_loading) t->webview->Stop();
                else t->webview->Reload();
            }
            return true;
        }
        if (vkey == 'D') {
            if (auto* t = current_tab()) {
                g_app.store.toggle_bookmark(t->title, t->url);
                sync_toolbar();
            }
            return true;
        }
        if (vkey == 'U') { view_page_source(); return true; }
        if (vkey == VK_OEM_COMMA) { open_searxng_dialog(); return true; }
    } else if (ctrl && shift && !alt) {
        if (vkey == 'N') { add_tab(default_newtab_url(), true); return true; }
        if (vkey == 'B') {
            g_app.blocker.set_enabled(!g_app.blocker.enabled());
            inject_cosmetic_and_mica(current_tab());
            sync_toolbar();
            return true;
        }
        if (vkey == 'Z') { trigger_zapper(); return true; }
        if (vkey == 'M') { toggle_transparent_mica(); return true; }
        if (vkey == 'I') {
            if (auto* t = current_tab(); t && t->webview) t->webview->OpenDevToolsWindow();
            return true;
        }
        if (vkey == 'J') { open_js_console(); return true; }
    } else if (alt && !ctrl) {
        if (vkey == VK_LEFT) { if (auto* t = current_tab(); t && t->webview) t->webview->GoBack(); return true; }
        if (vkey == VK_RIGHT) { if (auto* t = current_tab(); t && t->webview) t->webview->GoForward(); return true; }
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

void recreate_ui_font() {
    if (g_app.ui_font) DeleteObject(g_app.ui_font);
    g_app.ui_font = CreateFontW(
        -px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    if (g_app.address_edit) {
        SendMessageW(g_app.address_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_app.hwnd = hwnd;
        UINT dpi = GetDpiForWindow(hwnd);
        g_app.dpi_scale = dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
        g_app.edit_bg_brush = CreateSolidBrush(RGB(30, 33, 43));
        recreate_ui_font();

        apply_mica_backdrop(hwnd, g_app.store.transparent_websites(), chrome_height());

        g_app.address_edit = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            0, 0, 300, px(18), hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAddressEdit)), nullptr, nullptr);
        SendMessageW(g_app.address_edit, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.ui_font), TRUE);
        SetWindowSubclass(g_app.address_edit, AddressSubclassProc, 1, 0);
        sync_search_placeholder();

        if (auto blocklist_path = find_shared_file("blocklist_domains.txt"); !blocklist_path.empty()) {
            g_app.blocker.load_domains_file(blocklist_path);
        }

        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    if (SUCCEEDED(hr) && env && IsWindow(g_app.hwnd)) {
                        g_app.env = env;
                        add_tab(default_newtab_url(), false);
                    }
                    return S_OK;
                }).Get());

        compute_layout();
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        return 0;
    }
    case WM_NCCALCSIZE: {
        if (wParam == TRUE) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            if (IsZoomed(hwnd)) {
                UINT dpi = GetDpiForWindow(hwnd);
                int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                params->rgrc[0].left += fx;
                params->rgrc[0].right -= fx;
                params->rgrc[0].top += fy;
                params->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    }
    case WM_NCHITTEST: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        POINT client_pt = pt;
        ScreenToClient(hwnd, &client_pt);
        if (client_pt.y >= 0 && client_pt.y < chrome_height()) {
            HitBox hb = hit_test_chrome(client_pt);
            if (hb.kind != HitKind::None) return HTCLIENT;
        }

        RECT win_rc{};
        GetWindowRect(hwnd, &win_rc);
        if (!IsZoomed(hwnd)) {
            int b = px(6);
            bool l = pt.x < win_rc.left + b;
            bool r = pt.x >= win_rc.right - b;
            bool t = pt.y < win_rc.top + b;
            bool bot = pt.y >= win_rc.bottom - b;
            if (t && l) return HTTOPLEFT;
            if (t && r) return HTTOPRIGHT;
            if (bot && l) return HTBOTTOMLEFT;
            if (bot && r) return HTBOTTOMRIGHT;
            if (t) return HTTOP;
            if (bot) return HTBOTTOM;
            if (l) return HTLEFT;
            if (r) return HTRIGHT;
        }
        if (client_pt.y < tab_strip_height()) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wParam);
        g_app.dpi_scale = dpi > 0 ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
        recreate_ui_font();
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        apply_mica_backdrop(hwnd, g_app.store.transparent_websites(), chrome_height());
        compute_layout();
        invalidate_chrome();
        return 0;
    }
    case WM_SIZE:
        compute_layout();
        invalidate_chrome();
        return 0;
    case WM_MOUSEMOVE: {
        if (!g_app.tracking_mouse) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_app.tracking_mouse = true;
        }
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        HitBox hb = hit_test_chrome(pt);
        if (hb.kind != g_app.hovered.kind || hb.index != g_app.hovered.index) {
            g_app.hovered = hb;
            invalidate_chrome();
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_app.tracking_mouse = false;
        if (g_app.hovered.kind != HitKind::None) {
            g_app.hovered = {};
            invalidate_chrome();
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        HitBox hb = hit_test_chrome(pt);
        if (hb.kind != HitKind::None) {
            handle_hitbox_click(hb);
        }
        return 0;
    }
    case WM_MBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        HitBox hb = hit_test_chrome(pt);
        if (hb.kind == HitKind::TabSelect) {
            close_tab_at(hb.index);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, RGB(241, 245, 249));
        SetBkColor(hdc, RGB(30, 33, 43));
        return reinterpret_cast<LRESULT>(g_app.edit_bg_brush);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (rc.bottom > chrome_height()) {
            RECT body_rc{0, chrome_height(), rc.right, rc.bottom};
            FillRect(hdc, &body_rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        paint_chrome(hdc);
        EndPaint(hwnd, &ps);
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
        if (g_app.ui_font) { DeleteObject(g_app.ui_font); g_app.ui_font = nullptr; }
        if (g_app.edit_bg_brush) { DeleteObject(g_app.edit_bg_brush); g_app.edit_bg_brush = nullptr; }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BufferedPaintInit();

    Gdiplus::GdiplusStartupInput gdi_in;
    ULONG_PTR gdi_token = 0;
    Gdiplus::GdiplusStartup(&gdi_token, &gdi_in, nullptr);

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    enable_dark_uxtheme_menus();

    g_app.hinstance = hInstance;
    HICON app_icon = LoadIconW(hInstance, MAKEINTRESOURCEW(kAppIconId));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = app_icon;
    wc.hIconSm = app_icon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
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

    Gdiplus::GdiplusShutdown(gdi_token);
    BufferedPaintUnInit();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
