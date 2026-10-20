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


FACTORY_CURVE = [
    {"mv": 4180, "pct": 100}, {"mv": 4100, "pct": 92}, {"mv": 4030, "pct": 84},
    {"mv": 3970, "pct": 75}, {"mv": 3920, "pct": 66}, {"mv": 3870, "pct": 56},
    {"mv": 3830, "pct": 46}, {"mv": 3800, "pct": 37}, {"mv": 3770, "pct": 28},
    {"mv": 3740, "pct": 20}, {"mv": 3700, "pct": 13}, {"mv": 3650, "pct": 7},
    {"mv": 3580, "pct": 3}, {"mv": 3450, "pct": 0},
]


class Handler(SimpleHTTPRequestHandler):
    out: Path = ROOT / ".cache" / "dev_assets"
    config: dict = json.loads(json.dumps(DEFAULT_CONFIG))
    cal_cleared = False

    def power_body(self):
        """Demo trace: voltage sags early, remaining hours fall with used energy."""
        now = int(time.time())
        steps = 8 * 12
        ma = 49.0
        samples = []
        for i in range(steps + 1):
            frac = i / steps
            if frac < 0.35:
                mv = int(4160 - 380 * (frac / 0.35))
            elif frac < 0.8:
                mv = int(3780 - 90 * ((frac - 0.35) / 0.45))
            else:
                mv = int(3690 - 80 * ((frac - 0.8) / 0.2))
            hours = 9.0 - (9.0 - 0.8) * frac
            samples.append({
                "t": now - (steps - i) * 300,
                "mv": mv,
                "ma": ma,
                "h": round(hours, 2),
                "charging": False,
            })
        last = samples[-1]
        used = 450 - last["h"] * ma
        learned = not Handler.cal_cleared
        return {
            "mv": last["mv"],
            "ma": ma,
            "percent": int(round(last["h"] * ma / 450 * 100)),
            "runtime": last["h"],
            "charging": False,
            "used_mah": round(used, 1),
            "capacity_mah": 450,
            "motor_mah": 0.02,
            "learned": learned,
            "from_full": False,
            "samples": samples,
            "learned_curve": [
                {"mv": 4160, "used": 8}, {"mv": 4050, "used": 35}, {"mv": 3950, "used": 70},
                {"mv": 3870, "used": 120}, {"mv": 3800, "used": 190}, {"mv": 3740, "used": 270},
                {"mv": 3680, "used": 340}, {"mv": 3600, "used": 410},
            ] if learned else [],
            "factory_curve": FACTORY_CURVE,
        }

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
            power = self.power_body()
            state["device"] = {"battery": power["percent"], "charging": power["charging"],
                               "runtime": power["runtime"], "ssid": "M5Badge-1A2B",
                               "ip": "192.168.4.1", "version": "dev", "timeValid": True}
            return self.send_json(state)
        if self.path.startswith("/api/power"):
            return self.send_json(self.power_body())
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
        if self.path == "/api/power/reset":
            self.body()
            Handler.cal_cleared = True
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
