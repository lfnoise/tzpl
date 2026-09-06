# Download & Install

TZPL runs on **macOS** (packaged releases) and **Linux** (build from
source). A **Windows** port is in progress; see
[docs/WINDOWS.md](https://github.com/lfnoise/tzpl/blob/main/docs/WINDOWS.md)
for the state of it and how to build.

## Packaged releases

Official releases ship as a disk image, `Tzopilotl-<version>.dmg`, from the
[GitHub releases page](https://github.com/lfnoise/tzpl/releases). Open the
image and drag the **Tzopilotl** folder anywhere you like -- `/Applications`,
your home directory, a projects drive. There is no installer and nothing is
written outside the folder.

The folder is self-contained and relocatable:

```
Tzopilotl/
  Tzopilotl.app     the app: editor, notebooks, REPL, audio engine
  bin/tzpl          command-line interpreter and REPL (language only)
  modules/          the standard library (std.*, music.*, ugens, ...)
  examples/         runnable demos -- open one in the app
  docs/             the guides and reference documentation
  editors/          syntax packages for VS Code, TextMate, tree-sitter
  include/          plugin headers (used when compiling synth definitions)
  README.txt
```

The one rule: keep the folder together. Don't move `Tzopilotl.app` out on
its own, or it will no longer find its sibling `modules/` directory.

To update, replace the whole `Tzopilotl/` folder with the new version.
Nothing of yours is stored inside it, so this is always safe.

## Requirements

- **macOS.** The app uses CoreAudio; no other audio drivers are needed.
- **Xcode Command Line Tools.** Compiling synth definitions uses the system
  C++ compiler. If synthdef compilation fails with a missing-compiler
  error: `xcode-select --install`.
- **Linux**: see [docs/LINUX.md](https://github.com/lfnoise/tzpl/blob/main/docs/LINUX.md).
  **Windows**: see [docs/WINDOWS.md](https://github.com/lfnoise/tzpl/blob/main/docs/WINDOWS.md)
  (the Windows folder bundles its own plugin compiler).

## Building from source

```
git clone https://github.com/lfnoise/tzpl.git
cd tzpl
./build.sh
```

Requires Clang (or GCC 15+), CMake 3.21+, and a C++23 toolchain. See
[Getting Started](Getting_Started.html) for build options, the headless
CLI, and environment variables.

## Editor support

Syntax highlighting for Tzopilotl (`.x` files) is available for:

- **VS Code** -- `lang/editors/vscode` (also in the distribution's
  `editors/` folder)
- **Zed** -- `lang/editors/zed-tzpl`
- **TextMate / Sublime Text** -- `lang/editors/Tzopilotl.tmbundle`
- **Tree-sitter grammar** -- `lang/editors/tree-sitter-tzpl`
