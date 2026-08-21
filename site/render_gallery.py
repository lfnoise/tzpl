#!/usr/bin/env python3
"""Render the gallery audio clips into site/_audio/ (gitignored).

For each entry in site/gallery/gallery.json: run the headless app in NRT
mode to render the entry's script to WAV, then encode to AAC (.m4a) with
macOS afconvert. site/build.py stages site/_audio/*.m4a into _site/clips/
when present; when absent, the gallery page simply omits the players. In
CI this runs on a macOS runner after building the `tzpl_app` target
headless (TZPL_BUILD_GUI=OFF); audio is never committed to the repository.

    python3 site/render_gallery.py [--only id[,id...]]
"""

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "site" / "_audio"
APP = Path(sys.argv[sys.argv.index("--app") + 1]) if "--app" in sys.argv \
    else ROOT / "build" / "app" / "tzpl_app"
BITRATE = "96000"


def main():
    if not APP.exists():
        raise SystemExit(f"error: {APP} not found -- build the tzpl_app "
                         "target first (cmake -DTZPL_BUILD_APP=ON "
                         "-DTZPL_BUILD_GUI=OFF)")
    if not shutil.which("afconvert"):
        raise SystemExit("error: afconvert not found -- gallery rendering "
                         "requires macOS")

    only = None
    if "--only" in sys.argv:
        only = set(sys.argv[sys.argv.index("--only") + 1].split(","))

    manifest = json.loads(
        (ROOT / "site" / "gallery" / "gallery.json").read_text())
    OUT.mkdir(parents=True, exist_ok=True)
    failed = []

    for entry in manifest["entries"]:
        eid = entry["id"]
        if only and eid not in only:
            continue
        script = ROOT / entry["script"]
        with tempfile.TemporaryDirectory() as td:
            wav = Path(td) / f"{eid}.wav"
            cmd = [str(APP), "--nrt", str(wav),
                   "-I", str(ROOT / "lang" / "modules"),
                   "-I", str(ROOT / "bridge" / "modules")]
            cap = entry.get("duration_cap", 0)
            if cap:
                cmd += ["--duration", str(cap)]
            else:
                cmd += ["--nrt-safety-cap", "600"]
            cmd.append(str(script))
            print(f"render {eid}: {script.name}", flush=True)
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not wav.exists():
                print(f"error: render of {eid} failed:\n{r.stdout[-2000:]}"
                      f"\n{r.stderr[-2000:]}")
                failed.append(eid)
                continue
            m4a = OUT / f"{eid}.m4a"
            r = subprocess.run(
                ["afconvert", "-f", "m4af", "-d", "aac", "-b", BITRATE,
                 str(wav), str(m4a)],
                capture_output=True, text=True)
            if r.returncode != 0:
                print(f"error: afconvert of {eid} failed:\n{r.stderr[-1000:]}")
                failed.append(eid)
                continue
            print(f"  -> {m4a.relative_to(ROOT)} "
                  f"({m4a.stat().st_size // 1024} KB)")

    if failed:
        raise SystemExit(f"error: {len(failed)} clip(s) failed: "
                         + ", ".join(failed))
    print("all clips rendered")


if __name__ == "__main__":
    main()
