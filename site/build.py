#!/usr/bin/env python3
"""Assemble the TZPL documentation site into _site/.

Reads the hand-authored doc pages from lang/docs/, injects the shared site
shell (header bar, nav menu, prev/next links, edit links, shared CSS/JS,
Pagefind index attributes), splits the pages that nav.json marks with a
"split" config into per-chapter pages (the original URL becomes an overview
page that redirects old #anchors to the right chapter), and stages
everything plus the landing page and assets from site/ into _site/ at the
repository root. Doc page sources are never modified; all transformation
happens on the staged copies. Pure Python 3 stdlib; run from anywhere:

    python3 site/build.py

After building, the deploy workflow runs Pagefind over _site/ to produce
the search index, then site/check_links.py to validate internal links.
"""

import html as html_mod
import json
import re
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import md as mdmod  # noqa: E402  (site/md.py, the vendored converter)

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
DOCS = ROOT / "lang" / "docs"
OUT = ROOT / "_site"

HEAD_SNIPPET = (
    "<!-- tzpl-shell-head-begin -->"
    '<link rel="stylesheet" href="assets/tzpl-site.css">'
    '<script defer src="assets/tzpl-site.js"></script>'
    "<!-- tzpl-shell-head-end -->"
)


def esc(s):
    return html_mod.escape(s, quote=False)


def esc_attr(s):
    return html_mod.escape(s, quote=True)


# --------------------------------------------------------------------------
# Shell pieces
# --------------------------------------------------------------------------

def build_header(nav, current_file):
    """The fixed top bar: brand, docs menu, quick links, search, GitHub,
    theme toggle. `data-pagefind-ignore` keeps all of it out of the search
    index."""
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
        '<header class="tzpl-header" data-pagefind-ignore>'
        f'<a class="tzpl-brand" href="index.html">Tzopilotl</a>'
        '<details class="tzpl-menu"><summary>Docs</summary>'
        '<div class="tzpl-menu-panel">' + "".join(sections_html) + "</div>"
        "</details>"
        '<nav class="tzpl-quicklinks">'
        '<a href="Getting_Started.html">Get Started</a>'
        '<a href="Tzopilotl_by_Example.html">By Example</a>'
        '<a href="Tzopilotl_Music_Cookbook.html">Music Cookbook</a>'
        '<a href="Gallery.html">Audio Gallery</a>'
        "</nav>"
        '<div class="tzpl-header-right">'
        '<button class="tzpl-search-btn" aria-label="Search the docs">'
        'Search<kbd>⌘K</kbd></button>'
        f'<a class="tzpl-github" href="{nav["github"]}">GitHub</a>'
        '<button class="tzpl-theme" aria-label="Toggle light/dark theme">☾</button>'
        "</div>"
        "</header>"
        "<!-- tzpl-shell-header-end -->"
    )


def build_foot(nav, flat, index, source_file):
    """Prev/next links plus the edit-on-GitHub link."""
    parts = ['<div class="tzpl-foot" data-pagefind-ignore>',
             '<nav class="tzpl-prevnext">']
    prev = flat[index - 1] if index > 0 else None
    nxt = flat[index + 1] if index < len(flat) - 1 else None
    if prev:
        parts.append(
            f'<a class="tzpl-prev" href="{prev["file"]}">'
            f'<span>Previous</span>{esc(prev["title"])}</a>'
        )
    else:
        parts.append('<a class="tzpl-prev" href="index.html">'
                     "<span>Up</span>Documentation Home</a>")
    if nxt:
        parts.append(
            f'<a class="tzpl-next" href="{nxt["file"]}">'
            f'<span>Next</span>{esc(nxt["title"])}</a>'
        )
    parts.append("</nav>")
    parts.append(
        '<p class="tzpl-editlink"><a href='
        f'"{nav["github"]}/edit/main/{source_file}">'
        "Edit this page on GitHub</a></p>"
    )
    parts.append("</div>")
    return "".join(parts)


# --------------------------------------------------------------------------
# Page transformation
# --------------------------------------------------------------------------

def inject_shell(html, entry, nav, flat, index):
    """Inject shell pieces into one staged page. `entry` needs: file,
    title (tab title), section (nav section name), source (lang/docs file
    the edit link targets)."""
    html, n = re.subn(r"</head>", HEAD_SNIPPET + "</head>", html, count=1, flags=re.I)
    if n != 1:
        raise SystemExit(f"error: no </head> found in {entry['file']}")

    html = re.sub(
        r"<title>.*?</title>",
        f"<title>{esc(entry['title'])} — Tzopilotl</title>",
        html,
        count=1,
        flags=re.I | re.S,
    )

    # Mark the page body for Pagefind (pages without the attribute -- the
    # landing page -- are then excluded from the index automatically), and
    # declare the page's nav section as a search filter + result metadata.
    html, n = re.subn(
        r"<body([^>]*)>",
        r"<body\1 data-pagefind-body>"
        + build_header(nav, entry["file"])
        + '<div data-pagefind-filter="section[data-s]" '
        'data-pagefind-meta="section[data-s]" '
        f'data-s="{esc_attr(entry["section"])}" hidden></div>',
        html,
        count=1,
        flags=re.I,
    )
    if n != 1:
        raise SystemExit(f"error: no <body> found in {entry['file']}")

    # Keep per-page chrome out of the search index.
    html = html.replace('<nav class="sidebar">',
                        '<nav class="sidebar" data-pagefind-ignore>')
    html = html.replace('<div class="toc">',
                        '<div class="toc" data-pagefind-ignore>')

    # Prev/next + edit link: inside </main> when the page has one (flex-row
    # body layouts would otherwise render an appended element as a stray
    # flex item); before </body> otherwise.
    foot = build_foot(nav, flat, index, entry["source"])
    if re.search(r"</main>", html, flags=re.I):
        html = re.sub(r"</main>", foot + "</main>", html, count=1, flags=re.I)
    else:
        html = re.sub(r"</body>", foot + "</body>", html, count=1, flags=re.I)
    return html


# --------------------------------------------------------------------------
# Monolith splitting
# --------------------------------------------------------------------------

DIV_TAG_RE = re.compile(r"<div\b|</div>", re.I)


def balanced_div_end(text, start):
    """Index just past the </div> matching the <div at `start`."""
    depth = 0
    for m in DIV_TAG_RE.finditer(text, start):
        depth += 1 if m.group(0).lower().startswith("<div") else -1
        if depth == 0:
            return m.end()
    raise SystemExit("error: unbalanced <div> while splitting")


def find_region(html):
    """Split a page into (pre, region, post) around its main content:
    <main> for the guide-style pages, <div class="container"> for the
    reference-style pages."""
    m = re.search(r"<main>", html, flags=re.I)
    if m:
        end = html.lower().find("</main>")
        return html[: m.end()], html[m.end():end], html[end:]
    m = re.search(r'<div class="container">', html)
    if not m:
        raise SystemExit("error: split page has neither <main> nor .container")
    end = balanced_div_end(html, m.start())
    close = html.rfind("</div>", m.start(), end)
    return html[: m.end()], html[m.end():close], html[close:]


def split_chunks(region, mode):
    """Return (head, [(id, title, chunk_html), ...])."""
    chunks = []
    if mode == "h2":
        marks = list(re.finditer(r'<h2 id="([^"]+)"', region))
        if not marks:
            raise SystemExit("error: split mode h2 found no <h2 id=...>")
        head = region[: marks[0].start()]
        for i, m in enumerate(marks):
            end = marks[i + 1].start() if i + 1 < len(marks) else len(region)
            chunks.append((m.group(1), region[m.start():end]))
    elif mode == "sections":
        marks = list(re.finditer(r'<div class="section" id="([^"]+)"', region))
        if not marks:
            raise SystemExit("error: split mode sections found no section divs")
        head = region[: marks[0].start()]
        for m in marks:
            end = balanced_div_end(region, m.start())
            chunks.append((m.group(1), region[m.start():end]))
    else:
        raise SystemExit(f"error: unknown split mode {mode!r}")

    out = []
    for cid, chunk in chunks:
        m = re.search(r"<h2[^>]*>(.*?)</h2>", chunk, flags=re.S)
        title = re.sub(r"<[^>]+>", "", m.group(1)) if m else cid
        title = html_mod.unescape(re.sub(r"\s+", " ", title)).strip()
        out.append((cid, title, chunk))
    return head, out


def plan_split(page, html):
    """Compute the output pages for one split monolith. Returns
    (outputs, moved): a list of entry dicts with a 'content' field holding
    the assembled page HTML (overview first), and the map of anchors that
    moved out of the original URL into a chapter file."""
    prefix = page["split"]["prefix"]
    mode = page["split"]["mode"]
    pre, region, post = find_region(html)
    head, chunks = split_chunks(region, mode)

    # Where does every anchor live?
    anchor_map = {}
    for cid, _title, chunk in chunks:
        fname = f"{prefix}-{cid}.html"
        for aid in re.findall(r'id="([^"]+)"', chunk):
            anchor_map[aid] = fname
    for aid in re.findall(r'id="([^"]+)"', head):
        anchor_map[aid] = page["file"]

    def rewrite(page_html, own_ids):
        def sub(m):
            frag = m.group(1)
            if frag in own_ids or frag not in anchor_map:
                return m.group(0)
            return f'href="{anchor_map[frag]}#{frag}"'
        return re.sub(r'href="#([^"]+)"', sub, page_html)

    outputs = []

    # Overview at the original URL: intro/TOC plus a generated chapter list
    # for pages whose in-page TOC lives in the (mobile-hidden) sidebar, and
    # a redirect for old deep links whose anchor moved into a chapter.
    chapter_list = ""
    if mode == "h2":
        items = "".join(
            f'<li><a href="{prefix}-{cid}.html">{esc(title)}</a></li>'
            for cid, title, _ in chunks
        )
        chapter_list = f'<ol class="tzpl-chapters">{items}</ol>'
    moved = {aid: f for aid, f in anchor_map.items() if f != page["file"]}
    redirect = (
        "<script>(function(){var m=" + json.dumps(moved, separators=(",", ":"))
        + ";var h=location.hash.replace('#','');"
        "if(m[h])location.replace(m[h]+'#'+h);})();</script>"
    )
    overview = pre + head + chapter_list + post
    overview = overview.replace("</head>", redirect + "</head>", 1)
    own = set(re.findall(r'id="([^"]+)"', overview))
    outputs.append({
        "file": page["file"],
        "title": page["title"],
        "source": f"lang/docs/{page['file']}",
        "content": rewrite(overview, own),
    })

    for cid, title, chunk in chunks:
        fname = f"{prefix}-{cid}.html"
        # Search results should name the chapter within its guide.
        search_title = esc_attr(page["title"] + ": " + title)
        chunk = re.sub(
            r"<h2",
            '<h2 data-pagefind-meta="title[data-tzt]" data-tzt="'
            + search_title.replace("\\", "\\\\") + '"',
            chunk,
            count=1,
        )
        crumb = (
            '<p class="tzpl-crumb" data-pagefind-ignore>'
            f'<a href="{page["file"]}">{esc(page["title"])}</a></p>'
        )
        page_html = pre + crumb + chunk + post
        own = set(re.findall(r'id="([^"]+)"', page_html))
        outputs.append({
            "file": fname,
            "title": f"{title} — {page['title']}",
            "source": f"lang/docs/{page['file']}",
            "content": rewrite(page_html, own),
        })
    return outputs, moved


# --------------------------------------------------------------------------
# Markdown pages and the gallery
# --------------------------------------------------------------------------

def render_md_page(page):
    """A page whose source is a repo markdown file, rendered through
    site/md_template.html."""
    src = ROOT / page["md"]
    if not src.exists():
        raise SystemExit(f"error: nav.json lists missing markdown {src}")
    body, _title = mdmod.convert(src.read_text(encoding="utf-8"))
    template = (SITE / "md_template.html").read_text(encoding="utf-8")
    return (template
            .replace("{TITLE}", esc(page["title"]))
            .replace("{CONTENT}", body))


TZ_KEYWORDS = (
    "fn|let|var|const|import|go|coro|yield|yieldAll|await|if|else|while|"
    "for|match|case|return|break|continue|struct|enum|type|constraint"
)
TZ_TOKEN_RE = re.compile(
    r"(--[^\n]*)"                                  # comment
    r'|("(?:[^"\\\n]|\\.)*")'                      # string
    rf"|(\b(?:{TZ_KEYWORDS})\b)"                   # keyword
    r"|(\b\d+(?:\.\d+)?\b)"                        # number
)


def tz_highlight(code):
    """Tiny Tzopilotl highlighter over HTML-escaped code."""
    def sub(m):
        cmt, s, kw, num = m.groups()
        if cmt:
            return f'<span class="cmt">{cmt}</span>'
        if s:
            return f'<span class="str">{s}</span>'
        if kw:
            return f'<span class="kw">{kw}</span>'
        return f'<span class="num">{num}</span>'
    return TZ_TOKEN_RE.sub(sub, code)


def gallery_excerpt(entry):
    text = (ROOT / entry["script"]).read_text(encoding="utf-8")
    if "excerpt_range" in entry:
        a, b = entry["excerpt_range"]
        excerpt = "\n".join(text.split("\n")[a - 1:b])
    else:
        m = re.search(
            r"-- gallery-excerpt-begin\n(.*?)-- gallery-excerpt-end",
            text, flags=re.S)
        excerpt = m.group(1).rstrip() if m else text
    return tz_highlight(html_mod.escape(excerpt, quote=False))


GALLERY_STYLE = """<style>
  .gal-entry { margin: 2.2rem 0 2.8rem; }
  .gal-entry h2 { border-bottom: none; margin-bottom: 0.3rem; }
  .gal-entry pre { max-height: 24rem; overflow: auto; }
  .gal-entry audio { width: 100%; margin: 0.8rem 0 0.2rem; display: block; }
  .gal-links { font-size: 0.85rem; margin-top: 0.3rem; }
  .gal-pending { color: var(--muted-fg); font-size: 0.9rem; font-style: italic;
                 margin: 0.8rem 0 0.2rem; }
  .kw { color: #c678dd; } .str { color: #4ec970; }
  .cmt { color: #6a737d; font-style: italic; } .num { color: #e5a537; }
  html.light .kw { color: #8b5cf6; } html.light .str { color: #16a34a; }
  html.light .cmt { color: #94a3b8; } html.light .num { color: #d97706; }
</style>"""


def render_gallery_page(page, nav):
    """The audio gallery, generated from site/gallery/gallery.json. Players
    appear only for clips staged in _site/clips (rendered at deploy time by
    site/render_gallery.py); locally-unrendered entries show a note."""
    manifest = json.loads(
        (SITE / "gallery" / "gallery.json").read_text(encoding="utf-8"))
    parts = [GALLERY_STYLE,
             "<h1>Audio Gallery</h1>",
             "<p>Every clip below was rendered offline by the platform "
             "itself from the linked source -- the code shown is the code "
             "you hear. Clips are re-rendered from source on every site "
             "deploy, so they can never drift out of date.</p>"]
    for entry in manifest["entries"]:
        clip = OUT / "clips" / f"{entry['id']}.m4a"
        parts.append('<div class="gal-entry">')
        parts.append(f"<h2 id=\"{entry['id']}\">{esc(entry['title'])}</h2>")
        parts.append(f"<p>{esc(entry['description'])}</p>")
        if clip.exists():
            parts.append(
                f'<audio controls preload="none" '
                f'src="clips/{entry["id"]}.m4a"></audio>')
        else:
            parts.append('<p class="gal-pending">Audio clip is rendered at '
                         "deploy time and not present in this local build "
                         "(run site/render_gallery.py).</p>")
        parts.append("<pre><code>" + gallery_excerpt(entry) + "</code></pre>")
        parts.append(
            f'<p class="gal-links"><a href="{nav["github"]}/blob/main/'
            f'{entry["script"]}">Full source: {entry["script"]}</a></p>')
        parts.append("</div>")
    template = (SITE / "md_template.html").read_text(encoding="utf-8")
    return (template
            .replace("{TITLE}", esc(page["title"]))
            .replace("{CONTENT}", "\n".join(parts)))


# --------------------------------------------------------------------------
# Landing page helpers
# --------------------------------------------------------------------------

def build_doc_cards(nav):
    out = []
    for section in nav["sections"]:
        out.append(f'<h3 class="tzpl-cards-heading">{esc(section["title"])}</h3>')
        out.append('<div class="tzpl-cards">')
        for page in section["pages"]:
            out.append(
                f'<a class="tzpl-card" href="{page["file"]}">'
                f"<strong>{esc(page['title'])}</strong>"
                f"<span>{esc(page['description'])}</span></a>"
            )
        out.append("</div>")
    return "".join(out)


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    nav = json.loads((SITE / "nav.json").read_text(encoding="utf-8"))

    # Stage the output skeleton first: the gallery generator checks which
    # audio clips are present in _site/clips (rendered separately by
    # site/render_gallery.py into site/_audio, never committed).
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()
    shutil.copytree(SITE / "assets", OUT / "assets")
    audio = SITE / "_audio"
    if audio.is_dir() and any(audio.glob("*.m4a")):
        shutil.copytree(audio, OUT / "clips",
                        ignore=shutil.ignore_patterns("*.wav"))

    # Expand nav pages into the flat list of staged entries, splitting
    # monoliths. Entries carry: file, title (tab title), section, source
    # (repo-relative path for the edit link), content.
    flat = []
    moved_by_orig = {}  # original file -> {anchor: chapter file}
    for section in nav["sections"]:
        for page in section["pages"]:
            if page.get("gallery"):
                outputs = [{
                    "file": page["file"],
                    "title": page["title"],
                    "source": "site/gallery/gallery.json",
                    "content": render_gallery_page(page, nav),
                }]
            elif "md" in page:
                outputs = [{
                    "file": page["file"],
                    "title": page["title"],
                    "source": page["md"],
                    "content": render_md_page(page),
                }]
            else:
                src = DOCS / page["file"]
                if not src.exists():
                    raise SystemExit(
                        f"error: nav.json lists missing page {src}")
                html = src.read_text(encoding="utf-8")
                if "split" in page:
                    outputs, moved = plan_split(page, html)
                    moved_by_orig[page["file"]] = moved
                else:
                    outputs = [{
                        "file": page["file"],
                        "title": page["title"],
                        "source": f"lang/docs/{page['file']}",
                        "content": html,
                    }]
            for entry in outputs:
                entry["section"] = section["title"]
                flat.append(entry)

    # Cross-page links that target an anchor which moved into a chapter go
    # straight to the chapter (the overview's redirect script remains as a
    # fallback for external bookmarks).
    def retarget(m):
        orig, frag = m.group(1), m.group(2)
        dest = moved_by_orig.get(orig, {}).get(frag)
        return f'href="{dest}#{frag}"' if dest else m.group(0)

    orig_pat = "|".join(re.escape(f) for f in moved_by_orig)
    if orig_pat:
        link_re = re.compile(rf'href="({orig_pat})#([^"]+)"')
        for entry in flat:
            entry["content"] = link_re.sub(retarget, entry["content"])

    prevnext = [{"file": e["file"], "title": e["title"].split(" — ")[0]}
                for e in flat]
    for i, entry in enumerate(flat):
        staged = inject_shell(entry["content"], entry, nav, prevnext, i)
        (OUT / entry["file"]).write_text(staged, encoding="utf-8")

    # Pages present in lang/docs but absent from nav.json still deploy
    # unshelled, so existing deep links keep working; warn so nav.json
    # gets updated when new docs land.
    staged_names = {e["file"] for e in flat}
    for src in sorted(DOCS.glob("*.html")):
        if src.name in staged_names or src.name == "index.html":
            continue
        print(f"warning: {src.name} is not in site/nav.json; deploying without shell")
        shutil.copy(src, OUT / src.name)

    landing = (SITE / "index.html").read_text(encoding="utf-8")
    landing = re.sub(
        r"<!-- tzpl:source-note-begin -->.*?<!-- tzpl:source-note-end -->\n?",
        "",
        landing,
        flags=re.S,
    )
    landing = landing.replace("<!-- tzpl:header -->", build_header(nav, "index.html"))
    landing = landing.replace("<!-- tzpl:doccards -->", build_doc_cards(nav))
    hero_audio = ""
    if (OUT / "clips" / "bubbles.m4a").exists():
        hero_audio = (
            '<figure class="hero-audio">'
            "<figcaption>What it sounds like (rendered offline by the "
            "platform from this code):</figcaption>"
            '<audio controls preload="none" src="clips/bubbles.m4a"></audio>'
            "</figure>"
        )
    landing = landing.replace("<!-- tzpl:hero-audio -->", hero_audio)
    (OUT / "index.html").write_text(landing, encoding="utf-8")

    print(f"staged {len(list(OUT.glob('*.html')))} pages into {OUT}")


if __name__ == "__main__":
    sys.exit(main())
