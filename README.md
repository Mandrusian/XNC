# SNC — S is not Chrome

**SNC** is a super-lightweight (<5 MB native binary, well under 20–30 MB), Chromium-free desktop web browser built with native OS UI toolkits and system web engines:

- **macOS:** Swift + AppKit + `WKWebView` (full-bleed transparent titlebar + `.underWindowBackground` vibrancy blur)
- **Linux:** C++20 + GTK3 `GtkHeaderBar` + `WebKit2GTK 4.1`
- **Windows:** C++20 + Win32 + `WebView2` + Windows 11 Mica backdrop (`DwmSetWindowAttribute`)

## Features

- **Full-Bleed Native Chrome & Vibrancy/Mica:** Tabs and translucent toolbar reach straight into the titlebar on macOS (vibrancy behind-window blur), integrated `GtkHeaderBar` on Linux, and Windows 11 Mica titlebar on Windows.
- **uBlock-Style Shield:** Network-level third-party ad/tracker domain blocking (`64` high-impact domains), cosmetic CSS element hiding (`shared/cosmetic_rules.css`), and per-site Pause/Resume allowlisting.
- **Interactive Element Zapper (`Zap`):** Hover over page elements, **Click** to remove immediately (`U` to undo, `Esc` to exit), or **Shift+Click** to permanently hide the selector for that host.
- **Developer Tools (`Dev`):**
  - Web Inspector / DevTools (`Cmd+Opt+I` on macOS, `Ctrl+Shift+I` / `F12` on Linux & Windows)
  - JavaScript Console (`Cmd+Opt+J` on macOS) & View Page Source (`Cmd+Opt+U` on macOS)
  - Per-tab JavaScript toggle, User-Agent presets (macOS), and Clear Cookies/Cache
- **Multi-Tab + Private Tabs:** Isolated ephemeral/in-private tabs excluded from local history (`BrowserStore`).
- **Smart Address Bar:** Normalizes URLs, defaults `localhost`/`127.0.0.1` to `http://`, searches via DuckDuckGo.

## Keyboard Shortcuts

| Action | macOS | Linux / Windows |
| --- | --- | --- |
| New Tab / Private Tab | `Cmd+T` / `Cmd+Shift+N` | `Ctrl+T` / `Ctrl+Shift+N` |
| Close Tab | `Cmd+W` | `Ctrl+W` |
| Focus Address Bar | `Cmd+L` | `Ctrl+L` |
| Reload / Stop | `Cmd+R` | `Ctrl+R` |
| Back / Forward | `Cmd+[` / `Cmd+]` | `Alt+Left` / `Alt+Right` |
| Bookmark Page | `Cmd+D` | `Ctrl+D` |
| Toggle uBlock Shield | `Cmd+Shift+B` | `Ctrl+Shift+B` |
| Element Zapper | `Cmd+Shift+Z` | `Ctrl+Shift+Z` |
| Web Inspector / Console / Source | `Cmd+Opt+I` / `J` / `U` | `Ctrl+Shift+I` or `F12` |

## Build & Run

### Shared C++20 Core Tests (all platforms)

```bash
cmake -S core -B core/build -DCMAKE_BUILD_TYPE=Release
cmake --build core/build
ctest --test-dir core/build --output-on-failure
```

### macOS (`macos/`)

```bash
cd macos
swift test
./scripts/package_app.sh
open dist/SNC.app
```

### Linux (`linux/`)

Requires `libgtk-3-dev`, `libwebkit2gtk-4.1-dev`, and `pkg-config`.

```bash
cmake -S linux -B linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build linux/build
./linux/build/snc
```

### Windows (`windows/`)

Requires Visual Studio C++ build tools, CMake, and the `Microsoft.Web.WebView2` NuGet package.

```powershell
nuget install Microsoft.Web.WebView2 -OutputDirectory windows/packages -ExcludeVersion
cmake -S windows -B windows/build
cmake --build windows/build --config Release
.\windows\build\Release\snc.exe
```
