#!/usr/bin/env python3
"""Validate internal links and anchors in the assembled _site/ directory.

Checks every href/src in the staged HTML: internal page links must point at
a staged file, and fragment links (#anchor) must resolve to an id that
exists in the target page. External (http/https/mailto) links are skipped.
Exits non-zero on any broken link, so CI fails before a bad deploy.
Pure Python 3 stdlib; run after site/build.py:

    python3 site/check_links.py
"""

import re
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "_site"

SKIP_SCHEMES = ("http://", "https://", "mailto:", "data:", "javascript:")


def strip_scripts(html):
    """Ignore link-looking strings inside inline scripts."""
    return re.sub(r"<script\b.*?</script>", "", html, flags=re.S | re.I)


def main():
    pages = {}
    for path in OUT.rglob("*.html"):
        pages[path.relative_to(OUT).as_posix()] = path.read_text(encoding="utf-8")

    ids = {
        name: set(re.findall(r'\bid="([^"]+)"', html))
        for name, html in pages.items()
    }

    errors = []
    for name, html in pages.items():
        body = strip_scripts(html)
        refs = re.findall(r'\b(?:href|src)="([^"]+)"', body)
        for ref in refs:
            if ref.startswith(SKIP_SCHEMES) or ref.startswith("//"):
                continue
            path, _, frag = ref.partition("#")
            if path == "":
                target = name  # same-page anchor
            else:
                base = "/".join(name.split("/")[:-1])
                target = (base + "/" + path).lstrip("/") if base else path
                if target not in pages:
                    if not (OUT / target).exists():
                        errors.append(f"{name}: broken link -> {ref}")
                    continue  # asset or unshelled file; no anchor check
            if frag and frag not in ids.get(target, set()):
                errors.append(f"{name}: missing anchor -> {ref}")

    for err in errors:
        print("error:", err)
    print(f"checked {len(pages)} pages; {len(errors)} broken link(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
