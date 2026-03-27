# Survey of Text-Based Live Coding Environments

A survey of existing text-based live coding environments, their interfaces, and design patterns. Conducted in preparation for Phase 10 (UI Framework) of the implementation plan.

**Date**: 2026-03-25

---

## Major Environments

### SuperCollider
- **Interface**: Custom Qt IDE (scide) with tabbed editor, Post Window, Help Browser. Also usable from VS Code, Emacs, Vim, Sublime.
- **Execution**: Smart eval (Ctrl+Return) evaluates current line, selection, or parenthesized region. Shift+Return for single line. Double-click bracket to select matching region.
- **Feedback**: Post Window for logs/errors. Status bar shows interpreter state (green) and server stats (CPU, synth count). Brief highlight on evaluated region. VS Code extension adds live sliders/controls sidebar.
- **Editor**: Autocomplete, method call assistance, auto-indent, syntax highlighting, go-to-definition, integrated help (Ctrl+D).
- **Architecture**: Client-server via OSC (sclang interpreter + scsynth audio server as separate processes). Multiple clients can control one server; server can restart without losing code.
- **Innovation**: The original (1996). Client/server separation became the dominant design pattern. Patterns system for composition.

### Sonic Pi
- **Interface**: Standalone Qt app. Large editor, 10 named buffers (A-J), log viewer, cue viewer, oscilloscope (mono/stereo/Lissajous), help/tutorial panel. Buttons for Run/Stop/Save/Record.
- **Execution**: **Whole-buffer only** -- Run always evaluates the entire buffer. No line/selection eval. Deliberate design choice: the buffer *is* the complete state.
- **Feedback**: Timestamped log of every synth trigger/sample play. Cue viewer for internal events and MIDI/OSC. Real-time oscilloscope.
- **Editor**: Syntax highlighting, auto-indent, 10 persistent named buffers. No autocomplete (by design -- simplicity). Integrated searchable tutorial.
- **Architecture**: Erlang/BEAM for timing, Ruby interpreter for language, SuperCollider for synthesis. `live_loop` redefinition hot-swaps at next cycle boundary.
- **Innovation**: 10-buffer layering (drums in A, bass in B, etc). Whole-buffer model eliminates "which code is running?" confusion. Tau5 is moving to browser-based with collaborative jam sessions.

### TidalCycles
- **Interface**: No standalone app -- runs as editor plugin (Pulsar, VS Code, Emacs, Vim). Sends Haskell expressions to GHCi, which sends OSC to SuperDirt (SuperCollider).
- **Execution**: Shift+Enter for single line, Ctrl+Enter for multi-line block (contiguous non-empty lines). Patterns replace previous pattern on same stream, quantized to next cycle.
- **Feedback**: Pulsar plugin highlights individual tokens in mini-notation strings in sync with playback. VS Code adds configurable highlight color and eval count.
- **Editor**: Haskell syntax highlighting. VS Code adds Tidal-specific completion and hover docs.
- **Architecture**: Patterns assigned to numbered streams (d1-d16). `hush` silences all. Mini-notation for rhythmic patterns (`"bd [sn cp] bd sn"`).
- **Innovation**: The mini-notation has been adopted by Strudel, Gibber, Sardine, Mercury, and others. Pattern algebra for mathematical composition of patterns.

### Gibber
- **Interface**: Entirely browser-based. CodeMirror editor. 3D graphics (Three.js) render behind the code. Zero installation.
- **Execution**: Ctrl+Enter evaluates selection or current block.
- **Feedback**: **Animated inline code annotations** -- rotating borders on triggered values, pattern step highlighting, inline sparkline graphs for continuous signals, fade progress indicators, generated pattern display in adjacent comments. Annotations are positioned proximal to responsible code.
- **Innovation**: The most sophisticated visual feedback system in any live coding environment, based on published HCI research by Charlie Roberts. Keeps performer's eyes on the code rather than a separate visualization panel.

### Strudel
- **Interface**: Browser-based at strudel.cc. CodeMirror editor. Zero installation.
- **Execution**: Ctrl+Enter evaluates. Pattern replaces current pattern, quantized to next cycle.
- **Feedback**: The richest visualization set of any browser tool:
  - Mini-notation token highlighting (customizable colors)
  - Pianoroll (scrolling note display)
  - Spiral (events on rotating spiral, cycles align vertically)
  - Scope (oscilloscope, zero-crossing aligned)
  - Pitchwheel (frequencies on a pitch circle)
  - Spectrum analyzer
  - All come in two variants: background (full page) and **inline** (rendered within the code editor)
- **Innovation**: Inline visualizations embedded in the editor. Spiral view elegantly represents cyclic time. URL-based pattern sharing. TidalCycles' pattern algebra ported to JavaScript.

### Orca
- **Interface**: A 2D character grid -- nothing like any other live coding tool. Desktop (Electron), terminal (C), browser, even assembly versions.
- **Execution**: No "evaluate" action. The grid is **continuously evaluated every frame** like a cellular automaton. Uppercase operators fire every frame; lowercase fire only when banged.
- **Feedback**: The grid *is* the visualization -- data flows visibly as characters change. No separation between code and visual.
- **Innovation**: Completely unique paradigm -- 2D spatial esoteric programming language. 26 operators (one per letter). Sends MIDI/OSC to external audio software. Constraint-based creative environment.

### ChucK
- **Interface**: MiniAudicle IDE (Qt). Code editor + VM Monitor showing running "shreds" (threads) + Console. Also WebChucK IDE (browser via WASM).
- **Execution**: **Add Shred** compiles and adds code as a new concurrent thread. **Replace Shred** hot-swaps a running thread's code. **Remove Shred** stops it. You manage concurrent threads, not evaluate blocks.
- **Architecture**: "Strongly-timed" -- time doesn't advance unless explicitly requested (`1::second => now`). The `=>` (chuck) operator for left-to-right data flow.
- **Innovation**: The strongly-timed concurrent programming model. Shred management (add/replace/remove threads) is a distinct interaction paradigm.

### Extempore
- **Interface**: No IDE -- "compiler-as-a-service" running in terminal, accepting code via TCP socket. Editor plugins for Emacs, VS Code, Sublime.
- **Execution**: Evaluate top-level expression at cursor. Code sent to Extempore process, compiled by LLVM on the fly, executed immediately.
- **Architecture**: Two languages -- Scheme (dynamic, GC'd) and xtlang (statically typed, LLVM-compiled, RT-safe for DSP). **Temporal recursion**: a function schedules itself at a precise future time, creating a loop that can be redefined between iterations.
- **Innovation**: Dual-language design for both rapid prototyping and real-time DSP. Near-C-speed xtlang via LLVM. Most architecturally similar to this project (statically typed language that compiles to native code for RT audio).

### Mercury
- **Interface**: Max/MSP standalone or browser playground. Editor limited to **30 lines of code** by design. Background renders reactive visuals.
- **Execution**: Continuously re-evaluated as you type. Changes at next metric boundary.
- **Editor**: English-like syntax (`new sample beat time(1/4)`). 30-line limit forces conciseness.
- **Innovation**: 30-line constraint ensures entire program is always visible and audience-readable. Human-readable syntax designed for non-programmer audiences.

### Others of Note
- **FoxDot/Renardo**: Python-based, Tkinter/web editor, high-level abstractions (`p1 >> pluck([0,1,2,3])`). Lowest barrier to entry.
- **Sardine**: Python library, editor-agnostic, `@swim` decorator for temporal recursion. Leverages entire Python ecosystem.
- **Glicol**: Browser/Rust/WASM. Graph-oriented syntax (`~output: sin 440 >> mul 0.5`) where text directly mirrors signal flow.
- **Estuary**: Browser-based collaborative platform hosting multiple languages (Tidal, Punctual, Hydra) simultaneously.
- **Fluxus**: Code editor rendered on top of 3D OpenGL scene. Code overlaid on its visual output.

---

## Cross-Cutting Patterns

### Universal Conventions
- **Ctrl+Enter / Cmd+Enter** for evaluate (nearly universal)
- **Block-based evaluation** (contiguous non-empty lines or delimited regions)
- **Client-server architecture** separating editor from audio engine (OSC or TCP)
- **Dark themes** (performance context: projected screen in dark room)
- **Hot-swap at musical boundaries** (next cycle/beat, not immediately)
- **Named streams/voices** that can be independently started/stopped/modified

### What Works Well
- **Browser-based / zero-install** (Strudel, Glicol, Gibber) dramatically lowers the barrier
- **Inline annotations and pattern highlighting** (Gibber, Strudel) tighten the code-sound connection
- **Whole-buffer evaluation** (Sonic Pi) eliminates "which code is active?" confusion
- **Graceful error handling** -- previous music continues on error (Glicol, Strudel) -- critical for performance
- **Constraint-based design** (Mercury 30-line limit, Orca grid) paradoxically increases creativity

### What Doesn't Work Well
- **Setup complexity** -- TidalCycles needs Haskell + GHCi + SuperCollider + SuperDirt + editor plugin
- **"Which code is active?" problem** in block-based eval environments after many edits
- **Lack of visual feedback** in terminal-based tools (audience sees nothing but scrolling text)
- **Clock quantization latency** -- waiting for next cycle boundary can feel sluggish

---

## Relevance to This Project

This project (Tzopilotl + engine + synthdef-compiler) is most architecturally similar to **Extempore** -- a statically typed language that compiles for real-time audio, with a separate audio engine. Key design decisions for Phase 10:

1. **Evaluation granularity**: Line, block, selection, or whole-buffer? Most environments offer block + selection. Sonic Pi's whole-buffer is simpler but more limiting.
2. **Visual feedback on eval**: At minimum, flash/highlight the evaluated region. Gibber/Strudel's inline annotations are the gold standard but complex to implement.
3. **Error handling during performance**: The music must keep playing when new code has errors (Glicol/Strudel approach).
4. **The "what's running?" problem**: Consider a running shred/voice list (like ChucK's VM Monitor) or Sonic Pi's named buffer approach.
5. **Dear ImGui** (current recommendation in the implementation plan) is well-suited -- it's what Orca's Electron version and several other tools use for immediate-mode UIs in real-time contexts.
