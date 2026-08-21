## What does this PR do?

A short description of the change and the motivation for it.

## Checklist

- [ ] `cd lang/tests && bash run_tests.sh` passes (if `lang/` is affected)
- [ ] `./build/synthdef-compiler/synthdef-compiler --test` passes (if the
      synthdef compiler is affected)
- [ ] New behavior is covered by tests where applicable
- [ ] Real-time safety rules are respected (no system allocator or blocking
      calls on RT paths)
