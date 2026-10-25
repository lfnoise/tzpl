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
