---
name: release
description: Use when cutting a TZPL release -- version bump, NEWS.md stamping, macOS DMG build/sign/notarize, tagging, and publishing the GitHub release. Covers the full checklist and the signing environment variables.
---

# Cutting a TZPL release

The release unit is a git tag `vX.Y.Z` on `main` plus a GitHub release with
curated notes. A signed+notarized macOS DMG is attached when signing
credentials are available; v0.1.0 shipped as tag+notes only (no assets).

Publishing a release is outward-facing: confirm the version number and
whether to attach a DMG with James before pushing the tag or creating the
GitHub release.

## 1. Preflight

- Clean tree, on `main`, in sync with `origin/main` (`git status -sb` after
  a fetch).
- CI green on main: `gh run list -R lfnoise/tzpl -b main -L 3` (both the
  `tests` and `deploy-docs` workflows).
- Run the test suites locally if anything landed since the last green run:
  `cd lang/tests && bash run_tests.sh` and
  `./build/synthdef-compiler/synthdef-compiler --test`.

## 2. Version bump

The only version source is the root `CMakeLists.txt`:

    project(tzpl VERSION X.Y.Z LANGUAGES C CXX)

It propagates to the CLI, the app bundle version / `JUCE_APPLICATION_VERSION_STRING`
(`app/CMakeLists.txt`), and the DMG filename `Tzopilotl-X.Y.Z.dmg`.

## 3. Stamp NEWS.md

Retitle the `## Unreleased` heading to `## vX.Y.Z (D Month YYYY)`.
Curate the entries while there -- NEWS.md is the user-facing changelog and
renders on the doc site.

Commit the bump + stamp together as the release commit, matching v0.1.0's
style:

    release: vX.Y.Z -- <one-line summary>

(Project rule: no em dashes in commit messages, use `--`.)

## 4. Build the DMG (when shipping a binary)

    cmake --build build --target dist

This runs the `COMPONENT dist` install into `build/dist-stage/Tzopilotl/`
(app, `bin/tzpl`, modules, examples, docs, editors, README) and then
`packaging/make_dist_dmg.sh`, producing `build/Tzopilotl-X.Y.Z.dmg`.
Make sure `build/` was configured with `TZPL_BUILD_APP_JUCE=ON` (it is
cached in this checkout) and freshly built at the release commit first.

Signing/notarization are controlled by env vars read by the script:

- `TZPL_CODESIGN_IDENTITY` -- `"Developer ID Application: Name (TEAMID)"`.
  Unset: binaries keep ad-hoc signatures (fine for local testing, NOT
  distributable -- Gatekeeper blocks downloaded unsigned apps). Check what
  is installed with `security find-identity -v -p codesigning`.
- `TZPL_NOTARY_PROFILE` -- notarytool keychain profile name (e.g.
  `tzpl-notary`), created once interactively with
  `xcrun notarytool store-credentials`. Unset: skip notarization/stapling.

So a real distributable build is (this identity and profile exist on
James's machine since v0.2.0):

    TZPL_CODESIGN_IDENTITY="Developer ID Application: JAMES EVERETT MCCARTNEY (9KC49MZKRM)" \
    TZPL_NOTARY_PROFILE=tzpl-notary \
    cmake --build build --target dist

The script hard-fails if any shipped binary links a non-system dylib
(e.g. a dynamic Homebrew libnats); fix by installing static libs
(`brew install cnats openssl@3`) and reconfiguring.

Smoke-test the DMG: mount it, launch the app, run one example, and check
`spctl -a -vv` on the app if it was signed.

## 5. Tag and publish

Annotated tag, then push commit and tag together:

    git tag -a vX.Y.Z -m "TZPL vX.Y.Z -- <one-line summary>"
    git push origin main vX.Y.Z

GitHub release (repo `lfnoise/tzpl`): body style follows v0.1.0 --
a one-paragraph intro, `## Highlights` grouped under bold area headers
(**Language & VM**, **SynthDefs & UGens**, **Engine & Control**,
**Libraries & Examples**, **Docs & Site** -- use whichever apply), a link
to NEWS.md for the curated changelog, and a closing **Requirements** line.

    gh release create vX.Y.Z -R lfnoise/tzpl --title "TZPL vX.Y.Z" \
        --notes-file /path/to/notes.md
    gh release upload vX.Y.Z build/Tzopilotl-X.Y.Z.dmg   # if a DMG shipped

Preferred: draft first, so James can test the DMG via a real browser
download (quarantine flag and all) before anything goes public. Add
`--draft --target main` to `gh release create`; no git tag is created and
the release is only visible to collaborators. Swap in a rebuilt DMG with
`gh release upload vX.Y.Z --clobber ...`, and after James approves,
publish with `gh release edit vX.Y.Z --draft=false` -- publishing creates
the vX.Y.Z tag at the target branch's current head, so no manual
`git tag`/push of the tag is needed (fix-up commits made while testing
land inside the tag automatically). This replaces step "Tag and push" --
only `git push origin main` for the release commit is needed up front.

## 6. Post-release

Open the next cycle with a fresh `## Unreleased` heading at the top of
NEWS.md, committed as:

    docs: open Unreleased section in NEWS.md for the next cycle

The doc site (including the Changelog page) redeploys automatically via
the `deploy-docs` workflow on push to main -- nothing manual to do.
