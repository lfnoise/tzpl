# VM crash: enum payload with nested enums (found in block-engine M5)

`bundles.x` here is the first M5 form of `bridge/modules/bundles.x`, whose
`EngineCmd.input` carried nested enums (`InputPort`, `InputRegion`,
`InputMode`). With it, `examples/music_fx_demos.x` segfaults in a cold-cache
NRT render, every time (6/6), during demo 2:

    EXC_BAD_ACCESS address=0x3fe0000000000028   (0.5 as a pointer + 0x28)
    ts::ListNode::force <- builtin_head_list <- op_call_primitive
      <- VM::callCallable <- NRTTempoScheduler::tickTo <- runRenderThread

The trigger is demo 1's three `bundle() setInput(...) ... sched(...)` chains,
built inside the `go(coro ...)` that runs the demos; replacing them with
`begin(); setInput(...); sched(...)` or flattening the payload (the shipped
form) makes it go away. Standalone repros (plain `tzpl`, and `tzpl_app` with
bundles built in a coroutine next to lazy lists) did not crash.

Reproduce (shadow the module ahead of bridge/modules):

    mkdir -p /tmp/modshadow && cp devplans/repros/nested_enum_payload_crash/bundles.x /tmp/modshadow/
    TZPL_RNG_SEED=0x5eed TZPL_LANG_SEED=0x7a91 TZPL_BUILD=/tmp/fresh_$RANDOM \
      ./build/app/tzpl_app --nogui --nrt /tmp/x.wav --duration 215 --silos 4 \
      -I /tmp/modshadow -I lang/modules -I bridge/modules -I examples \
      examples/music_fx_demos.x

## Resolved (2026-10-26)

The cause was not the nested enums. `builtin_toList_coroutine` built the head
node of `score(...)`'s list and then resumed the coroutine to peek at the
second value, holding the node only in a C++ local; a GC cycle inside that
resume swept it, and `_playerCo` read the freed list later. Fixed by pinning
the node (`gcKeepAlivePush`). The same hunt added SATB barriers to inline Ref,
global and dynvar stores, dynvar save/restore, coroutine resume and lazy-list
forcing. With the fix applied at a64b21d, where this repro crashed every time,
it renders all three demos (HEAD no longer triggers it either way).

How it was found: quarantine swept objects instead of freeing them (tag them
dead), check reads and Ref stores for dead pointers, and verify at each mark
end that a stop-the-world trace from the roots finds nothing the incremental
mark missed. When the verifier passes but a dead object is read, the pointer
came from somewhere the GC never scans (a C++ local), not a missing barrier.
