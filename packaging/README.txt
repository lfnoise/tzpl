Tzopilotl
=========

An audio coding platform: the Tzopilotl language, a synth compiler, and
a real-time audio engine in one app.

This folder is self-contained. Keep it anywhere you like (Applications,
a projects drive, ...) -- just keep the app and its sibling folders
together, because the app finds them relative to itself:

  Tzopilotl.app    the app
  bin/tzpl         command-line interpreter and REPL
  modules/         the standard library
  examples/        runnable demos -- open one in the app
  docs/            language and reference documentation (HTML)
  editors/         syntax packages for VS Code, TextMate, tree-sitter

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
Compiling synth definitions uses the system C++ compiler; install the
Xcode command line tools if prompted (xcode-select --install).
