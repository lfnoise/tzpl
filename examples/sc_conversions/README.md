# SC2 example conversions -- work in progress

Tzopilotl translations of the SuperCollider 2 example corpus
(`sc-examples/SC2-examples/examples-1.txt` through `examples-12.txt`), one
`.x` file per source file, plus `sc2_common.x` for the shared machinery.

**This is a work in progress. Not all examples are faithful recreations
yet.** Every example compiles and renders, but several are known to sound
different from the SC2 originals, and a number of translations are
approximations by construction. In particular:

- `LFNoise2` is stood in for by `lfnoise3` (cubic); SC2's `LFNoise2` at
  audio rates may have had a different spectrum -- "police state" (file 2)
  and "Griot modeling" (file 4) are known not to match.
- `Resonz` is built from `bpf` with SC3's unity-peak-gain convention; the
  Resonz-based examples (pulsing bottles, crackle band, resonant dust) are
  much quieter than they were in SC2.
- `Klank` is a summed `ring` bank with no per-mode normalization;
  "just-scale resonators" (file 2) hits the limiter.
- SC pattern streams (`Prand`, `Pseq`, `.scramble`) are approximated: one
  pattern per texture instance, rotations instead of permutations.
- `OverlapTexture` / `XFadeTexture` / `Spawn` are a voicer plus a
  script-driven spawn loop (see the notes at the top of `sc2_common.x` for
  the timing model).
- A few examples are skipped (they need MIDI input or sound files); each
  file's header comment lists what is ported, approximated, or skipped.

Each file ends with a `playAllScN()` / `renderScN()` driver. Run with audio
on, with `examples/` and `examples/sc_conversions/` on the module path.
