-- live_plan_check.x -- headless checks for live.plan (the pure planning
-- layer of the proxy system): graph scans, port mapping, swap-op ordering,
-- and quant math. Needs the synthdef FFI (run under tzpl_app --nogui
-- --no-audio) but no engine.

import std.test.*;
import synthdef.*;
import live.plan.*;

---------------------------------------------------------------------------
-- Def-name mangling

assertEq(anchorDefName(2), "_pxA2", "anchor def name");
assertEq(monitorDefName(4), "_pxM4", "monitor def name");
assertEq(pxDefName(0, 17), "_px0_17", "source def name");
assertEq(pxRefName(1), "pxref1", "ref inlet name");
assertTrue(isPxRefName("pxref0"), "pxref name detected");
assertFalse(isPxRefName("in"), "plain inlet name not a ref");

---------------------------------------------------------------------------
-- wrapGraphFn: appends outlet only when missing

let gNoOut = makeGraph(wrapGraphFn(fn() S { fs() * 0.25 }));
let gWithOut = makeGraph(wrapGraphFn(fn() S { (fs() * 0.25) outlet }));

fn countOutlets(g SignalGraph) Int {
    var n = 0;
    for (e : g.exprs) {
        match (e.kind) {
            outlet(nm): n = n + 1;
            _: 0;
        }
    }
    n
}

assertEq(gNoOut countOutlets, 1, "wrap adds outlet to bare graph");
assertEq(gWithOut countOutlets, 1, "wrap keeps existing outlet");

---------------------------------------------------------------------------
-- scanRefPorts: port numbering with a raw user inlet mixed in

let gRefs = makeGraph(wrapGraphFn(fn() S {
    let ext = inlet(FLOAT32, 2, "in");            -- user's own inlet: port 0
    let a = inlet(FLOAT32, 2, pxRefName(0));      -- ref seq 0: port 1
    let ctl = control("mix", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5,
                                           warp: ControlWarp.linear });
    let b = inlet(FLOAT32, 1, pxRefName(1));      -- ref seq 1: port 2
    (ext + a + b) * ctl
}));

let refs = gRefs scanRefPorts;
assertEq(refs length, 2, "two pxref inlets found");
assertEq(refs[0].0, 1, "first ref on port 1");
assertEq(refs[0].1, 0, "first ref is seq 0");
assertEq(refs[1].0, 2, "second ref on port 2");
assertEq(refs[1].1, 1, "second ref is seq 1");
assertEq(gRefs countNestedRefs, 0, "no nested refs in flat graph");

---------------------------------------------------------------------------
-- nested refs are detected (unsupported -> proxy layer reports an error)

let gNested = makeGraph(wrapGraphFn(fn() S {
    voicer(4, fn() S {
        inlet(FLOAT32, 2, pxRefName(0)) * gate()
    }) sum
}));
assertEq(gNested countNestedRefs, 1, "pxref inside voicer body detected");

---------------------------------------------------------------------------
-- scanControls: declaration order, recurses into subgraphs

let gCtl = makeGraph(wrapGraphFn(fn() S {
    let a = control("alpha", ControlSpec { lo: 0.0, hi: 1.0, init: 0.1,
                                           warp: ControlWarp.linear });
    let v = voicer(4, fn() S {
        let inner = control("inner", ControlSpec { lo: 0.0, hi: 2.0, init: 1.0,
                                                   warp: ControlWarp.linear });
        gate() * inner
    }) sum;
    let b = toggle("beta");
    a * v + b
}));

let ctls = gCtl scanControls;
assertEq(ctls length, 3, "three controls found");
assertEq(ctls[0], "alpha", "control order: alpha first");
assertEq(ctls[1], "inner", "control order: voicer-body control second");
assertEq(ctls[2], "beta", "control order: beta last");

---------------------------------------------------------------------------
-- scanNoteParams: order kept, gate excluded, defaults from spec.init

let gNote = makeGraph(wrapGraphFn(fn() S {
    voicer(4, fn() S {
        let f = noteParam("freq", ControlSpec { lo: 20.0, hi: 2000.0,
                                                init: 440.0,
                                                warp: ControlWarp.exponential });
        let g = gate();
        let a = noteParam("amp", ControlSpec { lo: 0.0, hi: 1.0, init: 0.5,
                                               warp: ControlWarp.linear });
        fs() * 0.0 + f * a * g
    }) sum
}));

let (names, defaults) = gNote scanNoteParams;
assertEq(names length, 2, "gate excluded from noteParams");
assertEq(names[0], 'freq, "noteParam order: freq first");
assertEq(names[1], 'amp, "noteParam order: amp second");
assertNear(defaults[0], 440.0, 0.0001, "freq default from spec.init");
assertNear(defaults[1], 0.5, 0.0001, "amp default from spec.init");

---------------------------------------------------------------------------
-- nextQuant

assertNear(nextQuant(3.7, 4.0), 4.0, 0.0001, "quant rounds up");
assertNear(nextQuant(4.0, 4.0), 8.0, 0.0001, "quant is strictly after now");
assertNear(nextQuant(0.0, 4.0), 4.0, 0.0001, "quant from zero");
assertNear(nextQuant(7.25, 0.0), 7.25, 0.0001, "quant off passes through");

---------------------------------------------------------------------------
-- swap choreography: exact op order (connect new before disconnect old)

let swap = swapOps("_px0_3", 1000012, 1000007, 1000001,
                   [(1000042, 1)], [("freq", 165.0)], 2.0);
assertEq(swap opsToString,
    "newNode(_px0_3, 1000012)\n" $
    "setControl(1000012, freq, 165.0)\n" $
    "connect(1000042, 0, 1000012, 1)\n" $
    "connectX(1000012, 0, 1000001, 0, 2.0)\n" $
    "disconnectSourceX(1000007, 0, 1000001, 0, 2.0)\n",
    "swap op order");

-- first definition: no old source, no disconnect
let firstSwap = swapOps("_px0_3", 1000012, 0, 1000001,
                        [(Int, Int)](), [(String, Float)](), 0.02);
assertEq(firstSwap opsToString,
    "newNode(_px0_3, 1000012)\n" $
    "connectX(1000012, 0, 1000001, 0, 0.02)\n",
    "first-define op order");

let playSeq = playOps("_pxM2", 1000013, 1000001, 0.8, 1.0);
assertEq(playSeq opsToString,
    "newNode(_pxM2, 1000013)\n" $
    "setInput(1000013, 1, 0.8)\n" $
    "connect(1000001, 0, 1000013, 0)\n" $
    "connectX(1000013, 0, 0, 0, 1.0)\n",
    "play op order");

assertEq(stopOps(1000013, 1.0) opsToString,
    "disconnectSourceX(1000013, 0, 0, 0, 1.0)\n", "stop op");
assertEq(silenceOps(1000007, 1000001, 1.0) opsToString,
    "disconnectSourceX(1000007, 0, 1000001, 0, 1.0)\n", "silence op");

---------------------------------------------------------------------------

if (testSummary() == 0) { "LIVE PLAN ALL PASS" println; }
