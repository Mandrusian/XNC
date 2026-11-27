#!/usr/bin/env bash
set -euo pipefail

REPO_URL="${XNC_REPO_URL:-https://github.com/Mandrusian/XNC.git}"
SCRIPT_PATH="${BASH_SOURCE[0]:-}"
TMP_DIR=""

cleanup() {
  if [[ -n "$TMP_DIR" && -d "$TMP_DIR" ]]; then
    rm -rf "$TMP_DIR"
  fi
}
trap cleanup EXIT

for tool in cmake pkg-config c++; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "Missing required tool: $tool" >&2
    exit 1
  fi
done

if ! pkg-config --exists gtk+-3.0 webkit2gtk-4.1; then
  echo "Missing GTK3 / WebKit2GTK 4.1 development libraries." >&2
  echo "  Ubuntu/Debian: sudo apt-get install -y cmake pkg-config build-essential libgtk-3-dev libwebkit2gtk-4.1-dev" >&2
  echo "  Fedora:        sudo dnf install -y cmake pkgconf-pkg-config gcc-c++ gtk3-devel webkit2gtk4.1-devel" >&2
  echo "  Arch:          sudo pacman -S --needed cmake pkgconf base-devel gtk3 webkit2gtk-4.1" >&2
  exit 1
fi

find_repo_root() {
  if [[ -n "$SCRIPT_PATH" && -f "$SCRIPT_PATH" ]]; then
    local candidate
    candidate="$(cd "$(dirname "$SCRIPT_PATH")/.." && pwd)"
    if [[ -f "$candidate/linux/CMakeLists.txt" ]]; then
      echo "$candidate"
      return 0
    fi
  fi
  if [[ -f "linux/CMakeLists.txt" ]]; then
    pwd
    return 0
  fi
  return 1
}

if REPO_ROOT="$(find_repo_root)"; then
  echo "Using local repository at $REPO_ROOT"
else
  command -v git >/dev/null 2>&1 || { echo "git is required" >&2; exit 1; }
  TMP_DIR="$(mktemp -d)"
  echo "Cloning XNC from $REPO_URL..."
  git clone --depth 1 "$REPO_URL" "$TMP_DIR/XNC"
  REPO_ROOT="$TMP_DIR/XNC"
fi

cmake -S "$REPO_ROOT/linux" -B "$REPO_ROOT/linux/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$REPO_ROOT/linux/build" -j"$(nproc 2>/dev/null || echo 4)"

BIN_DIR="$HOME/.local/bin"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/xnc"
ICON_PNG_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/512x512/apps"
ICON_SVG_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/scalable/apps"
APP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

mkdir -p "$BIN_DIR" "$DATA_DIR/shared" "$ICON_PNG_DIR" "$ICON_SVG_DIR" "$APP_DIR"
install -m 0755 "$REPO_ROOT/linux/build/xnc" "$BIN_DIR/xnc"
cp -f "$REPO_ROOT/shared/"* "$DATA_DIR/shared/"
if [[ -f "$REPO_ROOT/shared/xnc_icon.png" ]]; then
  cp -f "$REPO_ROOT/shared/xnc_icon.png" "$ICON_PNG_DIR/xnc.png"
fi
if [[ -f "$REPO_ROOT/shared/xnc_logo.svg" ]]; then
  cp -f "$REPO_ROOT/shared/xnc_logo.svg" "$ICON_SVG_DIR/xnc.svg"
fi

cat > "$APP_DIR/xnc.desktop" <<DESKTOP
[Desktop Entry]
Name=XNC
Comment=Super-lightweight native WebKit browser (XNC is not Chrome)
Exec=$BIN_DIR/xnc %U
Icon=xnc
Terminal=false
Type=Application
Categories=Network;WebBrowser;
StartupWMClass=xnc
DESKTOP

command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true

echo ""
echo "Installed XNC binary: $BIN_DIR/xnc"
ls -lh "$BIN_DIR/xnc"
echo "Make sure $BIN_DIR is in your PATH, then run: xnc"
