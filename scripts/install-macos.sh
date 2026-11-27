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

find_repo_root() {
  if [[ -n "$SCRIPT_PATH" && -f "$SCRIPT_PATH" ]]; then
    local candidate
    candidate="$(cd "$(dirname "$SCRIPT_PATH")/.." && pwd)"
    if [[ -f "$candidate/macos/scripts/package_app.sh" ]]; then
      echo "$candidate"
      return 0
    fi
  fi
  if [[ -f "macos/scripts/package_app.sh" ]]; then
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

chmod +x "$REPO_ROOT/macos/scripts/package_app.sh"
"$REPO_ROOT/macos/scripts/package_app.sh"

APP_SRC="$REPO_ROOT/macos/dist/XNC.app"
if [[ -w "/Applications" ]]; then
  DEST_DIR="/Applications"
else
  DEST_DIR="$HOME/Applications"
  mkdir -p "$DEST_DIR"
fi

APP_DEST="$DEST_DIR/XNC.app"
rm -rf "$APP_DEST"
ditto "$APP_SRC" "$APP_DEST"
touch "$APP_DEST"

echo ""
echo "Installed XNC to: $APP_DEST"
du -sh "$APP_DEST"
echo "Launch with: open -a \"$APP_DEST\""
