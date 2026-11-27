# SNC — S is not Chrome

**SNC** is a super-lightweight (<5 MB native binary, well under 20–30 MB), Chromium-free desktop web browser built with native OS UI toolkits and system web engines:

- **macOS:** Swift + AppKit + `WKWebView` (WebKit)
- **Linux:** C++20 + GTK3 + `WebKit2GTK 4.1` (WebKit)
- **Windows:** C++20 + Win32 + `WebView2`

Because SNC uses the OS-provided WebKit / WebView runtime instead of bundling Chromium or Electron, each binary stays under a few megabytes with fast cold starts and minimal memory overhead.

## Features

- Multi-tab browsing + isolated **Private Tabs** (ephemeral storage, excluded from history)
- Smart address bar (normalizes URLs, defaults local dev hosts to `http://`, searches via DuckDuckGo)
- Built-in third-party ad & tracker blocker (`Shield`) powered by `shared/blocklist.json` / `shared/blocklist_domains.txt`
- Local bookmarks and capped browsing history (up to 500 entries)
- Native keyboard shortcuts across macOS, Linux, and Windows

## Keyboard Shortcuts

| Action | macOS | Linux / Windows |
| --- | --- | --- |
| New Tab | `Cmd+T` | `Ctrl+T` |
| New Private Tab | `Cmd+Shift+N` | `Ctrl+Shift+N` |
| Close Tab | `Cmd+W` | `Ctrl+W` |
| Focus Address Bar | `Cmd+L` | `Ctrl+L` |
| Reload / Stop | `Cmd+R` | `Ctrl+R` |
| Back / Forward | `Cmd+[` / `Cmd+]` | `Alt+Left` / `Alt+Right` |
| Bookmark Page | `Cmd+D` | `Ctrl+D` |
| Toggle Tracker Blocker | `Cmd+Shift+B` | `Ctrl+Shift+B` |

## Build & Run

### Shared C++20 Core Tests (all platforms)

```bash
cmake -S core -B core/build
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
