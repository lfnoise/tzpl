## What does this PR do?

A short description of the change and the motivation for it.

## Checklist

- [ ] `cd lang/tests && bash run_tests.sh` passes (if `lang/` is affected)
- [ ] `bash integration-tests/run_synthc_dump_test.sh` and
      `bash integration-tests/run_forms_equivalence_test.sh` pass (if synthc
      or the engine is affected)
- [ ] New behavior is covered by tests where applicable
- [ ] Real-time safety rules are respected (no system allocator or blocking
      calls on RT paths)
