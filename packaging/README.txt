Tzopilotl
=========

An audio coding platform: the Tzopilotl language, a synth compiler, and
a real-time audio engine in one app.

This folder is self-contained. Keep it anywhere you like (Applications,
a projects drive, ...) -- just keep the app and its sibling folders
together, because the app finds them relative to itself:

  Tzopilotl.app    the app (Tzopilotl.exe on Windows)
  bin/tzpl         command-line interpreter and REPL (tzpl.exe on Windows)
  modules/         the standard library
  examples/        runnable demos -- open one in the app
  docs/            language and reference documentation (HTML)
  editors/         syntax packages for VS Code, TextMate, tree-sitter
  include/         plugin headers, used when compiling synth definitions
  toolchain/       (Windows) the compiler used for synth definitions

New here? Start with docs/Getting_Started.html -- installing,
environment variables, and project configuration.

Projects
--------
A project is any folder containing a file named `tzpl-config` (engine
settings, one `key = value` per line; see docs). Opening any file
inside a project makes the app apply its settings and search its
modules/ directory. File > New Project creates one, with a starter
notebook. Your projects live outside this folder; files opened from
examples/ open as copies, so the originals stay pristine.

Updating
--------
Replace this whole folder with the new version. Nothing of yours is
stored inside it.

Requirements
------------
macOS: compiling synth definitions uses the system C++ compiler; install
the Xcode command line tools if prompted (xcode-select --install).

Windows: nothing to install; toolchain/ holds the compiler. The binaries
are not code signed, so the first launch shows "Windows protected your
PC": click "More info", then "Run anyway". Unzip the folder anywhere;
keep it together.
