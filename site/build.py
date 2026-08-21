#!/usr/bin/env python3
"""Assemble the TZPL documentation site into _site/.

Reads the hand-authored doc pages from lang/docs/, injects the shared site
shell (header bar, nav menu, prev/next links, shared CSS/JS), and stages
everything plus the landing page and assets from site/ into _site/ at the
repository root. Doc page sources are never modified; all injection happens
on the staged copies. Pure Python 3 stdlib; run from anywhere:

    python3 site/build.py
"""

import html as html_mod
import json
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
DOCS = ROOT / "lang" / "docs"
OUT = ROOT / "_site"


def esc(s):
    return html_mod.escape(s, quote=False)

HEAD_SNIPPET = (
    "<!-- tzpl-shell-head-begin -->"
    '<link rel="stylesheet" href="assets/tzpl-site.css">'
    '<script defer src="assets/tzpl-site.js"></script>'
    "<!-- tzpl-shell-head-end -->"
)


def build_header(nav, current_file):
    """The fixed top bar: brand, docs menu, quick links, GitHub, theme."""
    sections_html = []
    for section in nav["sections"]:
        links = []
        for page in section["pages"]:
            cur = ' class="tzpl-current-page"' if page["file"] == current_file else ""
            links.append(f'<a href="{page["file"]}"{cur}>{esc(page["title"])}</a>')
        sections_html.append(
            f'<div class="tzpl-menu-section"><h3>{esc(section["title"])}</h3>'
            + "".join(links)
            + "</div>"
        )
    return (
        "<!-- tzpl-shell-header-begin -->"
        '<header class="tzpl-header">'
        f'<a class="tzpl-brand" href="index.html">Tzopilotl</a>'
        '<details class="tzpl-menu"><summary>Docs</summary>'
        '<div class="tzpl-menu-panel">' + "".join(sections_html) + "</div>"
        "</details>"
        '<nav class="tzpl-quicklinks">'
        '<a href="Getting_Started.html">Get Started</a>'
        '<a href="Tzopilotl_by_Example.html">By Example</a>'
        '<a href="Tzopilotl_Music_Cookbook.html">Cookbook</a>'
        "</nav>"
        '<div class="tzpl-header-right">'
        f'<a class="tzpl-github" href="{nav["github"]}">GitHub</a>'
        '<button class="tzpl-theme" aria-label="Toggle light/dark theme">☾</button>'
        "</div>"
        "</header>"
        "<!-- tzpl-shell-header-end -->"
    )


def build_prevnext(flat, index):
    parts = ['<nav class="tzpl-prevnext">']
    prev = flat[index - 1] if index > 0 else None
    nxt = flat[index + 1] if index < len(flat) - 1 else None
    if prev:
        parts.append(
            f'<a class="tzpl-prev" href="{prev["file"]}">'
            f'<span>Previous</span>{esc(prev["title"])}</a>'
        )
    else:
        parts.append('<a class="tzpl-prev" href="index.html"><span>Up</span>Documentation Home</a>')
    if nxt:
        parts.append(
            f'<a class="tzpl-next" href="{nxt["file"]}">'
            f'<span>Next</span>{esc(nxt["title"])}</a>'
        )
    parts.append("</nav>")
    return "".join(parts)


def inject(html, page, header, prevnext):
    """Inject shell pieces into one staged doc page."""
    # Shared assets into <head>.
    html, n = re.subn(r"</head>", HEAD_SNIPPET + "</head>", html, count=1, flags=re.I)
    if n != 1:
        raise SystemExit(f"error: no </head> found in {page['file']}")

    # Consistent tab titles.
    html = re.sub(
        r"<title>.*?</title>",
        f"<title>{esc(page['title'])} — Tzopilotl</title>",
        html,
        count=1,
        flags=re.I | re.S,
    )

    # Header bar right after <body>.
    html, n = re.subn(r"(<body[^>]*>)", r"\1" + header, html, count=1, flags=re.I)
    if n != 1:
        raise SystemExit(f"error: no <body> found in {page['file']}")

    # Prev/next links: inside </main> when the page has one (flex-row body
    # layouts would otherwise render an appended element as a stray flex
    # item); before </body> otherwise.
    if re.search(r"</main>", html, flags=re.I):
        html = re.sub(r"</main>", prevnext + "</main>", html, count=1, flags=re.I)
    else:
        html = re.sub(r"</body>", prevnext + "</body>", html, count=1, flags=re.I)
    return html


def build_doc_cards(nav):
    """Section cards for the landing page, generated from nav.json."""
    out = []
    for section in nav["sections"]:
        out.append(f'<h3 class="tzpl-cards-heading">{section["title"]}</h3>')
        out.append('<div class="tzpl-cards">')
        for page in section["pages"]:
            desc = esc(page["description"])
            out.append(
                f'<a class="tzpl-card" href="{page["file"]}">'
                f"<strong>{esc(page['title'])}</strong><span>{desc}</span></a>"
            )
        out.append("</div>")
    return "".join(out)


def main():
    nav = json.loads((SITE / "nav.json").read_text(encoding="utf-8"))
    flat = [p for s in nav["sections"] for p in s["pages"]]

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()
    shutil.copytree(SITE / "assets", OUT / "assets")

    # Doc pages.
    staged = set()
    for i, page in enumerate(flat):
        src = DOCS / page["file"]
        if not src.exists():
            raise SystemExit(f"error: nav.json lists missing page {src}")
        html = src.read_text(encoding="utf-8")
        header = build_header(nav, page["file"])
        prevnext = build_prevnext(flat, i)
        (OUT / page["file"]).write_text(
            inject(html, page, header, prevnext), encoding="utf-8"
        )
        staged.add(page["file"])

    # Pages present in lang/docs but absent from nav.json still deploy
    # unshelled, so existing deep links keep working; warn so nav.json
    # gets updated when new docs land.
    for src in sorted(DOCS.glob("*.html")):
        if src.name in staged or src.name == "index.html":
            continue
        print(f"warning: {src.name} is not in site/nav.json; deploying without shell")
        shutil.copy(src, OUT / src.name)

    # Landing page.
    landing = (SITE / "index.html").read_text(encoding="utf-8")
    landing = landing.replace("<!-- tzpl:header -->", build_header(nav, "index.html"))
    landing = landing.replace("<!-- tzpl:doccards -->", build_doc_cards(nav))
    (OUT / "index.html").write_text(landing, encoding="utf-8")

    print(f"staged {len(list(OUT.glob('*.html')))} pages into {OUT}")


if __name__ == "__main__":
    sys.exit(main())
