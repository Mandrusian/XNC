#!/usr/bin/env python3
"""Relocatable SearXNG launcher bundled inside XNC (macOS & Windows)."""

import argparse
import os
import secrets
import sys
import threading
import time
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent
SITE_PACKAGES = BASE_DIR / "site-packages"
SEARXNG_DIR = BASE_DIR / "searxng"

for p in (SITE_PACKAGES, SEARXNG_DIR):
    sp = str(p)
    if p.is_dir() and sp not in sys.path:
        sys.path.insert(0, sp)


def ensure_settings(data_dir: Path, port: int) -> Path:
    data_dir.mkdir(parents=True, exist_ok=True)
    cfg = data_dir / "searxng_settings.yml"
    if not cfg.is_file():
        secret = secrets.token_hex(32)
        cfg.write_text(
            f"""use_default_settings: true

general:
  instance_name: "XNC Search"
  contact_url: "https://github.com/Mandrusian/XNC"

search:
  safe_search: 0
  autocomplete: "duckduckgo"
  formats:
    - html
    - json

server:
  port: {port}
  bind_address: "127.0.0.1"
  secret_key: "{secret}"
  limiter: false
  image_proxy: false

ui:
  default_theme: simple
  query_in_title: true
  infinite_scroll: false
""",
            encoding="utf-8",
        )
    return cfg


def is_pid_alive(pid: int) -> bool:
    if pid <= 0:
        return True
    if os.name == "nt":
        import ctypes

        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        STILL_ACTIVE = 259
        handle = ctypes.windll.kernel32.OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION, False, pid
        )
        if not handle:
            return False
        try:
            code = ctypes.c_ulong(0)
            ok = ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
            return bool(ok and code.value == STILL_ACTIVE)
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def start_parent_watchdog(parent_pid: int) -> None:
    if parent_pid <= 0:
        return

    def watch() -> None:
        while True:
            time.sleep(2.0)
            if not is_pid_alive(parent_pid):
                os._exit(0)

    t = threading.Thread(target=watch, daemon=True)
    t.start()


def main() -> int:
    parser = argparse.ArgumentParser(description="XNC bundled SearXNG server")
    parser.add_argument("--port", type=int, default=18888)
    parser.add_argument("--data-dir", type=str, default=str(BASE_DIR / "data"))
    parser.add_argument("--parent-pid", type=int, default=0)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    cfg = ensure_settings(Path(args.data_dir), args.port)
    os.environ["SEARXNG_SETTINGS_PATH"] = str(cfg)
    os.environ.setdefault("SEARXNG_URL", f"http://127.0.0.1:{args.port}/")

    import waitress
    from searx import webapp

    if args.check:
        print("XNC bundled SearXNG check OK:", getattr(webapp, "app", None) is not None)
        return 0

    start_parent_watchdog(args.parent_pid)
    waitress.serve(
        webapp.app,
        host="127.0.0.1",
        port=args.port,
        threads=8,
        ident="XNC-SearXNG",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
