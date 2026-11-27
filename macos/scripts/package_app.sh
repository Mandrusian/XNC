#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$MACOS_DIR/.." && pwd)"

cd "$MACOS_DIR"
swift build -c release

BIN_PATH="$(swift build -c release --show-bin-path)/XNC"
if [[ ! -f "$BIN_PATH" ]]; then
  echo "Release binary not found at $BIN_PATH" >&2
  exit 1
fi

APP_DIR="$MACOS_DIR/dist/XNC.app"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources"

cp "$BIN_PATH" "$APP_DIR/Contents/MacOS/XNC"
chmod +x "$APP_DIR/Contents/MacOS/XNC"
strip -x "$APP_DIR/Contents/MacOS/XNC" || true

if [[ -f "$REPO_ROOT/shared/blocklist.json" ]]; then
  cp "$REPO_ROOT/shared/blocklist.json" "$APP_DIR/Contents/Resources/blocklist.json"
fi
if [[ -f "$REPO_ROOT/shared/cosmetic_rules.css" ]]; then
  cp "$REPO_ROOT/shared/cosmetic_rules.css" "$APP_DIR/Contents/Resources/cosmetic_rules.css"
fi
if [[ -f "$REPO_ROOT/shared/zapper.js" ]]; then
  cp "$REPO_ROOT/shared/zapper.js" "$APP_DIR/Contents/Resources/zapper.js"
fi

cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>XNC</string>
  <key>CFBundleIdentifier</key>
  <string>com.mandrusian.xnc</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>XNC</string>
  <key>CFBundleDisplayName</key>
  <string>XNC</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.2.0</string>
  <key>CFBundleVersion</key>
  <string>2</string>
  <key>LSMinimumSystemVersion</key>
  <string>13.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSAppTransportSecurity</key>
  <dict>
    <key>NSAllowsArbitraryLoads</key>
    <true/>
  </dict>
</dict>
</plist>
PLIST

echo "Packaged: $APP_DIR"
ls -lh "$APP_DIR/Contents/MacOS/XNC"
du -sh "$APP_DIR"
