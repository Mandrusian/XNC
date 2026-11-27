#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MACOS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$MACOS_DIR/.." && pwd)"

cd "$MACOS_DIR"
swift build -c release

BIN_PATH="$(swift build -c release --show-bin-path)/SNC"
if [[ ! -f "$BIN_PATH" ]]; then
  echo "Release binary not found at $BIN_PATH" >&2
  exit 1
fi

APP_DIR="$MACOS_DIR/dist/SNC.app"
rm -rf "$APP_DIR"
mkdir -p "$APP_DIR/Contents/MacOS" "$APP_DIR/Contents/Resources"

cp "$BIN_PATH" "$APP_DIR/Contents/MacOS/SNC"
chmod +x "$APP_DIR/Contents/MacOS/SNC"
strip -x "$APP_DIR/Contents/MacOS/SNC" || true

if [[ -f "$REPO_ROOT/shared/blocklist.json" ]]; then
  cp "$REPO_ROOT/shared/blocklist.json" "$APP_DIR/Contents/Resources/blocklist.json"
fi

cat > "$APP_DIR/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>SNC</string>
  <key>CFBundleIdentifier</key>
  <string>com.mandrusian.snc</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>SNC</string>
  <key>CFBundleDisplayName</key>
  <string>SNC</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>1</string>
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
ls -lh "$APP_DIR/Contents/MacOS/SNC"
du -sh "$APP_DIR"
