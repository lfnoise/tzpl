#!/usr/bin/env python3
"""Preview server for the assembled site: python3 site/serve.py [port]

Serves _site/ like `python -m http.server` but with two fixes that matter
for the gallery audio: a correct Content-Type for .m4a (the stdlib guesses
audio/mp4a-latm, which browsers refuse to decode) and HTTP Range support
(browser media playback seeks with Range requests; without it players can
stall). Use this, not a bare http.server, to preview locally.
"""

import os
import re
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "_site"


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".m4a": "audio/mp4",
        ".mjs": "text/javascript",
        ".wasm": "application/wasm",
    }

    def send_head(self):
        """SimpleHTTPRequestHandler.send_head plus single-range support."""
        rng = self.headers.get("Range")
        path = self.translate_path(self.path)
        if not (rng and os.path.isfile(path)):
            return super().send_head()
        m = re.match(r"bytes=(\d*)-(\d*)$", rng.strip())
        size = os.path.getsize(path)
        if not m or (not m.group(1) and not m.group(2)):
            return super().send_head()
        start = int(m.group(1)) if m.group(1) else size - int(m.group(2))
        end = int(m.group(2)) if m.group(1) and m.group(2) else size - 1
        start, end = max(0, start), min(end, size - 1)
        if start > end:
            self.send_error(416, "Requested Range Not Satisfiable")
            return None
        f = open(path, "rb")
        f.seek(start)
        self.send_response(206)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        self._range_left = end - start + 1
        return f

    def copyfile(self, source, outputfile):
        left = getattr(self, "_range_left", None)
        if left is None:
            return super().copyfile(source, outputfile)
        while left > 0:
            chunk = source.read(min(65536, left))
            if not chunk:
                break
            outputfile.write(chunk)
            left -= len(chunk)
        self._range_left = None


def main():
    if not ROOT.is_dir():
        raise SystemExit("error: _site/ not found -- run python3 site/build.py first")
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    server = ThreadingHTTPServer(
        ("127.0.0.1", port), partial(Handler, directory=str(ROOT)))
    print(f"serving {ROOT} at http://localhost:{port}/  (Ctrl-C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
