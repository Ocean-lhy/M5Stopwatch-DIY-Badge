#!/usr/bin/env python3
"""Local preview of the badge editor with a mock of the firmware HTTP API.

    python3 tools/dev_server.py [--port 8766] [--out .cache/dev_assets]

Uploaded layers are written to --out (card.jpg, about.jpg, name.jpg, avatar.jpg)
together with config.json, so the desktop simulator can render them:

    sim/build/badge_sim --assets .cache/dev_assets --out .cache/sim
"""
import argparse
import json
import sys
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_profile import load as load_profile  # noqa: E402

PROFILE_DIR = ROOT / "profile" if (ROOT / "profile" / "profile.json").exists() else ROOT / "profile.example"
_profile = load_profile(PROFILE_DIR)
THEME_COLORS = [
    {"bg": "#000000", "ink": "#F2F6FA", "muted": "#8A9BB0", "accent": "#3BA7FF"},
    {"bg": "#032447", "ink": "#FFFFFF", "muted": "#A0C4E6", "accent": "#7FD0FF"},
    {"bg": "#F4F1EA", "ink": "#1A1A1A", "muted": "#6E6A62", "accent": "#E0542E"},
]
DEFAULT_CONFIG = {
    "published": False,
    **{k: _profile[k] for k in ("name", "title", "company", "intro", "seeking", "booth", "tags", "email", "phone")},
    "theme": _profile["theme"],
    "colors": THEME_COLORS[_profile["theme"]],
    "brightness": 200,
    "ambientBrightness": 40,
    "ambientAfter": 45,
    "scanTimeout": 30,
    "rotation": 0,
    "tz": _profile["tz"],
    "touch": True,
    "about": True,
    "layers": 0,
    "qr": [{"label": q.get("label", ""), "text": q["text"]} for q in _profile["qr"]],
}
LAYER_BITS = {"card": 1, "about": 2, "name": 4, "avatar": 0}


class Handler(SimpleHTTPRequestHandler):
    out: Path = ROOT / ".cache" / "dev_assets"
    config: dict = json.loads(json.dumps(DEFAULT_CONFIG))

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT / "web"), **kwargs)

    def log_message(self, fmt, *args):
        print("dev_server:", fmt % args)

    def send_json(self, body, status=200):
        data = json.dumps(body, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def body(self):
        return self.rfile.read(int(self.headers.get("Content-Length", 0)))

    def do_GET(self):
        if self.path.startswith("/api/state"):
            state = dict(self.config)
            state["device"] = {"battery": 82, "charging": False, "runtime": 9.6, "ssid": "M5Badge-1A2B",
                               "ip": "192.168.4.1", "version": "dev", "timeValid": True}
            return self.send_json(state)
        if self.path.startswith("/avatar.jpg"):
            path = self.out / "avatar.jpg"
            if not path.exists():
                path = PROFILE_DIR / "avatar.jpg"
            data = path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return None
        return super().do_GET()

    def do_POST(self):
        time.sleep(0.15)
        if self.path == "/api/assets":
            name = self.headers.get("X-Asset-Name", "")
            data = self.body()
            if name not in LAYER_BITS or data[:2] != b"\xff\xd8":
                return self.send_json({"ok": False, "message": "unknown asset or not a JPEG"}, 400)
            self.out.mkdir(parents=True, exist_ok=True)
            (self.out / f"{name}.jpg").write_bytes(data)
            self.config["layers"] |= LAYER_BITS[name]
            return self.send_json({"ok": True})
        if self.path == "/api/config":
            incoming = json.loads(self.body() or b"{}")
            if incoming.pop("publish", False):
                incoming["published"] = True
            incoming.pop("time", None)
            self.config.update({k: v for k, v in incoming.items() if k in DEFAULT_CONFIG})
            self.out.mkdir(parents=True, exist_ok=True)
            (self.out / "config.json").write_text(json.dumps(self.config, ensure_ascii=False, indent=2))
            return self.send_json({"ok": True})
        if self.path == "/api/time":
            self.body()
            return self.send_json({"ok": True})
        if self.path == "/api/reset":
            Handler.config = json.loads(json.dumps(DEFAULT_CONFIG))
            return self.send_json({"ok": True})
        return self.send_json({"ok": False, "message": "not found"}, 404)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--out", type=Path, default=ROOT / ".cache" / "dev_assets")
    args = ap.parse_args()
    Handler.out = args.out.resolve()
    print(f"dev_server: http://127.0.0.1:{args.port}/  (assets -> {Handler.out})")
    ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
