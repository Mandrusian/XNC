#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${1:-$REPO_ROOT/macos/searxng-runtime}"
OUT_ABS="$(mkdir -p "$(dirname "$OUT_DIR")" && cd "$(dirname "$OUT_DIR")" && pwd)/$(basename "$OUT_DIR")"

WORK_TMP="$(mktemp -d "${TMPDIR:-/tmp}/xnc-searxng.XXXXXX")"
trap 'rm -rf "$WORK_TMP"' EXIT

echo "-> Installing relocatable Python 3.12 via uv..."
uv python install 3.12
PY_BIN="$(uv python find 3.12)"
PY_ROOT="$(cd "$(dirname "$PY_BIN")/.." && pwd)"

echo "-> Preparing runtime at $OUT_ABS"
rm -rf "$OUT_ABS"
mkdir -p "$OUT_ABS/python" "$OUT_ABS/site-packages" "$OUT_ABS/searxng"
ditto "$PY_ROOT" "$OUT_ABS/python"

# Ensure relocatable python3 binary works offline
PORTABLE_PY="$OUT_ABS/python/bin/python3"
if [[ ! -x "$PORTABLE_PY" ]]; then
  echo "Portable python3 not found at $PORTABLE_PY" >&2
  exit 1
fi

echo "-> Cloning upstream searxng/searxng..."
git clone --depth 1 https://github.com/searxng/searxng.git "$WORK_TMP/searxng-src"

# Filter out optional uwsgi / gunicorn / uvloop if unneeded for waitress WSGI serving
grep -viE '^(uwsgi|gunicorn|setproctitle|uvloop)' "$WORK_TMP/searxng-src/requirements.txt" > "$WORK_TMP/reqs.txt"

echo "-> Installing SearXNG dependencies + waitress into site-packages..."
uv pip install \
  --python "$PORTABLE_PY" \
  --target "$OUT_ABS/site-packages" \
  --no-cache \
  -r "$WORK_TMP/reqs.txt" \
  waitress

echo "-> Copying SearXNG package + launcher..."
cp -R "$WORK_TMP/searxng-src/searx" "$OUT_ABS/searxng/searx"
cp "$REPO_ROOT/shared/run_searxng.py" "$OUT_ABS/run_searxng.py"

echo "-> Slimming runtime caches, tests, and static docs..."
find "$OUT_ABS" -type d \( -name "__pycache__" -o -name "tests" -o -name "test" \) -prune -exec rm -rf {} +
rm -rf "$OUT_ABS/python/share" "$OUT_ABS/python/include" "$OUT_ABS/python/lib/tcl"* "$OUT_ABS/python/lib/tk"* || true
find "$OUT_ABS/python/lib/python3.12" -maxdepth 1 -type d \( -name "test" -o -name "idlelib" -o -name "tkinter" -o -name "ensurepip" -o -name "turtledemo" \) -exec rm -rf {} + || true

echo "-> Verifying relocatable SearXNG runtime..."
PYTHONNOUSERSITE=1 PYTHONDONTWRITEBYTECODE=1 "$PORTABLE_PY" "$OUT_ABS/run_searxng.py" --check --data-dir "$WORK_TMP/check-data"
du -sh "$OUT_ABS"
