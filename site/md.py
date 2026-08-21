#!/usr/bin/env python3
"""Minimal markdown -> HTML converter for the TZPL docs site.

Covers the subset the project's markdown actually uses (verified against
lang/Theory_of_Operation.md, engine/Architecture.md, and
synthdef-compiler/ARCHITECTURE.md): ATX headings (with generated anchor
ids), fenced code blocks, pipe tables, unordered/ordered lists with one
nesting level, horizontal rules, paragraphs, and the inline forms `code`,
**bold**, *italic*, [links](url), and bare-URL autolinks. Pure stdlib, so
neither contributors nor CI need a markdown package installed.
"""

import html
import re


def slugify(text):
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[^\w\s-]", "", text.lower())
    return re.sub(r"[\s_]+", "-", text).strip("-") or "section"


def inline(text):
    """Inline markup on already-escaped text."""
    # Code spans first; their contents are protected from other rules.
    parts = re.split(r"(`+)(.+?)\1", text)
    out = []
    i = 0
    while i < len(parts):
        if i + 2 < len(parts) and re.fullmatch(r"`+", parts[i + 1] or ""):
            out.append(styled(parts[i]))
            out.append(f"<code>{parts[i + 2]}</code>")
            i += 3
        else:
            out.append(styled(parts[i]))
            i += 1
    return "".join(out)


def styled(text):
    text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r'(?<![\w&">=])(https?://[^\s<>&"]+[^\s<>&".,;:!?)])',
                  r'<a href="\1">\1</a>', text)
    text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", text)
    return text


def table_row(line, tag):
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    tds = "".join(f"<{tag}>{inline(c)}</{tag}>" for c in cells)
    return f"<tr>{tds}</tr>"


def convert(md):
    """Convert markdown text to an HTML fragment. Returns (html, title):
    title is the first h1's text, or None."""
    lines = md.split("\n")
    out = []
    title = None
    seen_slugs = set()
    i = 0
    list_stack = []  # open list tags, innermost last

    def close_lists(depth=0):
        while len(list_stack) > depth:
            out.append(f"</{list_stack.pop()}>")

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Fenced code block.
        m = re.match(r"^```(\w*)\s*$", stripped)
        if m:
            close_lists()
            lang = f' class="lang-{m.group(1)}"' if m.group(1) else ""
            body = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                body.append(html.escape(lines[i]))
                i += 1
            out.append(f"<pre><code{lang}>" + "\n".join(body) + "</code></pre>")
            i += 1
            continue

        # Blank line.
        if not stripped:
            close_lists()
            i += 1
            continue

        # Heading.
        m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", stripped)
        if m:
            close_lists()
            level = len(m.group(1))
            text = inline(html.escape(m.group(2)))
            if level == 1 and title is None:
                title = re.sub(r"<[^>]+>", "", text)
            slug = slugify(m.group(2))
            base, n = slug, 2
            while slug in seen_slugs:
                slug = f"{base}-{n}"
                n += 1
            seen_slugs.add(slug)
            out.append(f'<h{level} id="{slug}">{text}</h{level}>')
            i += 1
            continue

        # Horizontal rule.
        if re.match(r"^(-{3,}|\*{3,}|_{3,})$", stripped):
            close_lists()
            out.append("<hr>")
            i += 1
            continue

        # Table: current line has pipes and the next is a separator row.
        if "|" in stripped and i + 1 < len(lines) and \
                re.match(r"^\s*\|?[\s:|-]+\|[\s:|-]*$", lines[i + 1]) and \
                "-" in lines[i + 1]:
            close_lists()
            rows = [table_row(html.escape(stripped, quote=False), "th")]
            i += 2
            while i < len(lines) and "|" in lines[i] and lines[i].strip():
                rows.append(table_row(html.escape(lines[i], quote=False), "td"))
                i += 1
            out.append('<div class="table-wrap"><table>'
                       + "".join(rows) + "</table></div>")
            continue

        # List item (one nesting level: 2+ leading spaces = nested).
        m = re.match(r"^(\s*)([-*+]|\d+[.)])\s+(.*)$", line)
        if m:
            depth = 1 if len(m.group(1)) >= 2 else 0
            tag = "ol" if m.group(2)[0].isdigit() else "ul"
            while len(list_stack) > depth + 1:
                out.append(f"</{list_stack.pop()}>")
            if len(list_stack) == depth:
                out.append(f"<{tag}>")
                list_stack.append(tag)
            out.append(f"<li>{inline(html.escape(m.group(3), quote=False))}</li>")
            i += 1
            continue

        # Paragraph: gather until a blank line or block start.
        close_lists()
        para = [stripped]
        i += 1
        while i < len(lines):
            nxt = lines[i].strip()
            if not nxt or nxt.startswith(("#", "```", "|", "- ", "* ")) \
                    or re.match(r"^\d+[.)]\s", nxt):
                break
            para.append(nxt)
            i += 1
        out.append("<p>" + inline(html.escape(" ".join(para), quote=False)) + "</p>")

    close_lists()
    return "\n".join(out), title
