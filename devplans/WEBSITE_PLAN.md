# TZPL Documentation Site Plan

A phased plan for evolving https://lfnoise.github.io/tzpl/ from a flat list of
standalone HTML guides into an organized documentation site, using the Flow
language site (https://flooooooooooow.github.io/flow/) as the reference model.

## Reference model: how the Flow site works

Flow's site is **not** a framework or static-site-generator product. It is a
small hand-rolled shell in the repo's `site/` directory:

- `index.html` + `wiki.js` + `wiki.css` -- a single-page shell that fetches
  markdown files on demand and renders them client-side (marked +
  highlight.js), routed by URL hash (`#wiki-home.md#anchor`), so browser
  back/forward and deep links work.
- **Sidebar navigation** generated from a `wiki-nav.json` manifest (sections
  and tabs), plus a flat ordering used for prev/next page links.
- **Search**: Pagefind (client-side full-text, built at deploy time) with a
  small fallback `search-index.json`; ⌘K modal, category filters, keyboard
  navigation.
- **Per-page affordances**: auto-generated "On this page" TOC with scroll-spy
  (IntersectionObserver), edit-on-GitHub links, read-progress bar, theme
  toggle persisted in localStorage.
- **Interactive extras**: runnable code blocks backed by a WASM build of the
  compiler, a tutorial runner, and CI-generated status badges for examples.

Content types Flow offers that TZPL's site currently does not: a real landing
page with a pitch and live example, a demo **gallery**, a **tutorial** track
distinct from reference docs, a **changelog**, a **playground**, and
prominent community/contribution links.

## Current TZPL state

- Deployed by `.github/workflows/deploy-docs.yml`, which copies
  `lang/docs/*.html` verbatim to GitHub Pages.
- `lang/docs/index.html` is a plain link list -- functional but not a landing
  page (no pitch, no code sample, no screenshots, no audio).
- ~10 hand-written, fully self-contained HTML guides, each with its own
  inline `<style>` block (styling and title conventions drift between pages);
  two are large monoliths (`Tzopilotl_by_Example.html` ~220 KB,
  `Writing_SynthDefs.html` ~128 KB).
- The pages already share a dark/light theme system (dark default,
  `html.light` override, per-page toggle persisted under the
  `tzpl-docs-theme` localStorage key) and carry per-page tables of contents
  (a sticky sidebar on the guide-style pages; inline TOC boxes on the
  reference-style pages).
- What's missing is everything *between* pages: no cross-page navigation,
  no search, no prev/next, no edit links, no scroll-spy, and no shared
  shell (two distinct page templates that drift independently).
- Architecture docs (`engine/Architecture.md`,
  `synthdef-compiler/ARCHITECTURE.md`, `lang/Theory_of_Operation.md`) are
  repo-only markdown, not published.
- No gallery, tutorials track, changelog, downloads page, or FAQ.

## Guiding decisions

1. **Keep the docs hand-authored HTML; do not migrate to markdown.** The
   guides carry heavy custom markup that would be lossy to convert, and
   Flow's own approach proves a good site needs a shell + manifest, not a
   source-format change. New *long-form* pages may be authored in markdown
   and rendered at build time, but nothing existing gets rewritten.
2. **No framework.** Like Flow: a shared CSS file, a shared JS file, a
   `nav.json` manifest, and one small build script run in CI. Third-party
   pieces limited to Pagefind (search) and, later, marked + highlight.js as
   vendored assets for markdown-authored pages.
3. **Doc sources stay in `lang/docs/`** (CLAUDE.md, the app, and existing
   links reference them). Site-only assets (shell CSS/JS, landing page,
   nav manifest, build script, images, audio) live in a new top-level
   `site/` directory. CI assembles `_site/` from both.
4. **TZPL's differentiator is sound.** Where Flow shows compile-status
   badges, TZPL should ship rendered audio: every gallery entry pairs code
   with an `<audio>` clip (and screenshots of the app/notebooks).
5. **No audio files in the repository.** All audio clips are rendered at
   deploy time in CI and exist only in the published Pages artifact (see
   Phase 3 gallery item for the mechanism). Committed binary media is
   limited to small, rarely-changing images.

## Phase 1 -- Landing page and unified shell

Goal: the site looks and navigates like one product instead of ten files.

- **New landing page** (`site/index.html`): one-line pitch, a short
  Tzopilotl code sample with what it sounds like (audio clip, CI-rendered
  once the Phase 3 render pipeline exists; ship without it initially), a
  screenshot of the JUCE app, install/build snippet, and card links into
  Get Started / By Example / Reference / Cookbook / GitHub.
- **Shared shell**: extract a common stylesheet (`site/tzpl-docs.css`) and
  header/footer template -- top nav bar (Home, Guides, Reference, GitHub),
  consistent `Tzopilotl -- <Page>` titles, footer with repo/license links.
- **Build script** (`site/build.py` or similar, run in `deploy-docs.yml`):
  - reads `site/nav.json` (sections -> pages -> titles/descriptions);
  - injects the shared header, sidebar, and footer into each doc page
    (marker comments in the HTML keep pages viewable standalone from disk);
  - generates prev/next links from the nav order;
  - stages `_site/` exactly as the workflow does today.
- **Scroll-spy**: small shared JS that highlights the current section in
  the per-page sidebar TOCs the guide-style pages already have.
- **Header theme toggle** sharing the pages' existing `tzpl-docs-theme`
  localStorage convention, so the choice follows the reader across pages.
- Update `deploy-docs.yml` paths (`site/**` triggers) and verify the
  published result renders identically for existing deep links (all current
  URLs must keep working).

Exit criteria: every page shares one look, is reachable from every other
page, has prev/next, and the landing page pitches the project.

**Status (2026-08-21): implemented on branch `site-phase1`.** Delivered:
`site/nav.json` (nav manifest), `site/build.py` (stdlib-only build script:
shell injection, title normalization, prev/next, landing assembly, warns on
pages missing from nav.json and deploys them unshelled so deep links
survive), `site/assets/tzpl-site.css` + `tzpl-site.js` (fixed header with
grouped Docs menu, quick links, GitHub, theme toggle; scroll-spy;
coexistence overrides for both page families), `site/index.html` (landing:
hero, cookbook `bubbles` code sample, three-pillar overview, doc cards
generated from nav.json, build-from-source section), and the updated
workflow. Verified in-browser on both page families in both themes.

## Phase 2 -- Search and navigation polish

Goal: Flow-grade findability.

- **Pagefind** run over `_site/` at deploy time (it indexes static HTML
  directly -- no source changes needed). ⌘K search modal in the shell with
  keyboard navigation and category filters (Guides / Reference / Cookbook /
  Design Notes) driven by `data-pagefind-filter` attributes the build script
  injects from `nav.json`.
- **Split the monoliths** for search quality and load time:
  `Tzopilotl_by_Example.html` and `Writing_SynthDefs.html` become per-chapter
  pages under the same section, with a stub at the old URL redirecting old
  anchors (`<meta refresh>` + JS anchor map) so existing links survive.
- **Edit-on-GitHub link** on every page (build script knows each page's
  source path).
- Link checking in CI (e.g. lychee) over `_site/` so reorganizations can't
  silently break cross-references.

Exit criteria: any function, module, or concept is reachable in a few
keystrokes from any page.

## Phase 3 -- The missing content types

Goal: add the document categories Flow has and TZPL lacks. These are
content-writing tasks on top of the Phase 1 shell; each is independently
shippable.

- **Gallery / showcase** (highest value): a grid of examples, each with a
  code excerpt, an audio render, and a link to the full source in
  `examples/`. Audio is rendered **at deploy time in CI** -- no audio files
  are ever committed to the repository (binary clips in git history grow the
  repo permanently on every re-render). The deploy workflow builds `tzpl` on
  a `macos-latest` runner (platform is currently macOS-only; free for public
  repos), runs each gallery example in NRT mode to render audio to file,
  encodes to Opus/OGG (~96 kbps), and stages the clips into `_site/`
  alongside the HTML. The clips exist only in the Pages deployment artifact,
  can never drift out of sync with the example code, and the render step
  doubles as a CI smoke test that the gallery examples run. Prerequisites:
  verify offline render-to-file works without opening the live CoreAudio
  device, and add build caching to keep deploy times tolerable. Include
  notebook screenshots (piano roll, UI controls) for the app-side examples;
  screenshots are captured manually and are small and rarely re-taken, so
  committing those under `site/` is acceptable.
- **Tutorial track**: a numbered, progressive series distinct from
  reference -- e.g. 1. First sound, 2. Your first synthdef, 3. Sequencing
  with `music.pat`, 4. Live controls and notebooks, 5. A small live set.
  Getting Started remains the install/build page; tutorials assume the app
  is running. Author these in markdown, rendered by the build script.
- **Changelog / news page**: curated `NEWS.md` (or generated from GitHub
  releases), rendered into the site; linked from the landing page.
- **Download / install page**: DMG releases (from `packaging/`), system
  requirements, editor-support installation (VS Code, Zed, tree-sitter).
- **Architecture section**: publish `lang/Theory_of_Operation.md`,
  `engine/Architecture.md`, and `synthdef-compiler/ARCHITECTURE.md` via the
  markdown rendering path, under an "Internals" nav section.
- **Community / contributing page**: links CONTRIBUTING.md, SECURITY.md,
  issue templates, and any chat/forum venue once one exists.
- **FAQ**: seeded from questions the docs answer awkwardly today (RT-safety
  rules, `!` convention, auto-mapping surprises, licensing/JUCE).

## Phase 4 -- Interactive (stretch, research-grade)

Goal: Flow's playground/runnable-examples tier, adapted to audio. Each item
is a project in its own right; none blocks Phases 1-3.

- **Browser REPL (text-only)**: Emscripten build of `tzpl_lib` running pure
  language code (no engine) in a page -- the VM is single-threaded with its
  own allocator, which is WASM-friendly; the blockers are the C++23/musttail
  toolchain story and stubbing NRT IO builtins.
- **Audio in the browser**: synthdef graphs executed via a WebAudio
  AudioWorklet backend (either a WASM interpreter for synthdefs or a WASM
  cross-compile path replacing the dlopen/clang pipeline). Large effort;
  prototype before promising.
- **Example status badges**: CI job that type-checks/compiles every
  `examples/*.x` and publishes a JSON the gallery reads, Flow-style.
- **Live-coding videos/GIFs** on the landing page and gallery as a cheap
  precursor to a real playground.

## Sequencing and effort

| Phase | Scope | Rough effort |
|-------|-------|--------------|
| 1 | Landing page, shell, build script, TOC, theme | days |
| 2 | Pagefind, monolith split + redirects, edit links, link CI | days |
| 3 | Gallery, tutorials, changelog, downloads, internals, FAQ | weeks (content-bound, shippable piecemeal) |
| 4 | WASM REPL, WebAudio playground, status badges | open-ended |

Phases 1 and 2 are pure infrastructure and should land before investing in
Phase 3 content (so new content is authored against the final shell). Within
Phase 3, the gallery is the highest-leverage single item: it is the page
that shows, in thirty seconds with sound, what the platform is for.
