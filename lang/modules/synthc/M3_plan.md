# synthc M3 Plan -- Control Flow (if / switch / for, subgraphs, phi)

> **Historical milestone notes** for the synthc port, retained as a development record.

Status: M3 DONE for if_/for_/switch_ -- analysis (M3.1/M3.2/M3.3), codegen
(M3.4), and parity (M3.5) all complete. The Tier-1 dump and Tier-2 generated C++
byte-match the C++ compiler, and the Tier-3 render A/B is bit-identical, for
if/else, no-else, nested, multi-if, for loops, and switches.
M3 tail (subgraph-local delays in branches) DONE: the pull_nested / pulltwo
examples -- nested `pause` branches each holding a `combn` (runtime comb delay),
multichannel branch signals, and `dust` (RNG) inside a branch -- byte-match the
C++ codegen. Required: per-graph hash-consing (each subgraph has its own cons
set, so a shared sampleDur cast stays inlined per graph rather than becoming a
cross-graph inst var); per-graph delay-advance (advance only a graph's own
delays, emitted at the end of its control-flow block); per-graph RNG state
(rgenN, N = the RNG node's graph); a control-flow loop emits its block directly
(no channel for-loop even when multichannel); and a dynamic-scope fix in
cutGraphToTrees (per-tree `var ` accumulators were declared in the loop body and
accumulated across iterations -> dynamic-scope overflow on many-tree synths;
now scoped in a per-tree helper).

GC bug found and FIXED (lang VM): compiling many heavy control-flow synths
back-to-back used to fault in builtin_push_array. Root cause: the GC did not
scan the dynamic-var SAVE STACK (dynStack_) -- values shadowed by an in-scope
`var `x` rebinding. The front end's nested _makeSubGraph (used by nested pause/
control flow) shadows `curGraphExprs (the enclosing graph's live exprs array);
the saved array was unrooted and swept while still in use, then its reused memory
faulted later. Fixed in tracing_gc.cpp step_root_dynvars: scan dynStack_, marking
1-word object saves and walking multi-word inline-composite saves through the
side payload buffer. The pull examples now compile together in the main
synthc_controlflow_diff.x with no crash.

Remaining M3 tail: event-rate iso-groups spanning subgraphs. M4 = voicers/spectral.

M1/M2 are complete: the Tzopilotl-hosted synthdef compiler (`lang/modules/synthc/`)
byte-matches the C++ `synthdef-compiler/` for the whole non-control-flow corpus
(constants, ops, controls, delays, multichannel, rewrite engine, vec/reduce, RNG,
mergeDelays, buffers), validated by Tier-1 dump diffs, Tier-2 source byte-match,
and Tier-3 NRT render A/B. M3 adds control flow, which introduces the first
multi-graph structure (nested subgraphs for branches/loop bodies) and phi nodes.

This doc is the implementation plan; it records the survey findings so the work
can proceed without re-deriving the C++ behavior.

---

## The key architectural insight

M3 is *not* "rewrite every pass to recurse per-graph." The C++ compiler keeps a
**single flattened `sorted` array** of all expressions across all graphs. Only
`topologicalSortExprs` (pass 1) recurses into subgraphs, via `num_subgraphs()` /
`get_subgraph(i)`; the other 16 passes iterate the flat `sorted` array and use a
per-`Expr` `graph` pointer tag. The graph hierarchy survives only in:

1. each node's `graph` tag (`Expr::graph`), and
2. **audio-rate loops bucketed per-graph** -- `splitRates()` does
   `loop->graph->loops.push_back(loop)` (synthdef_synth.cpp:861). Init/reset/event
   loops stay synth-global.

So the synthc port needs: a `graphOf` node tag, a subgraph-aware topo sort, a few
new GraphCut rules, per-graph audio-loop bucketing, and control-flow codegen
recursion -- not a per-graph rewrite of every pass.

Source refs: `graphAnalysis()` pass list at synthdef_synth.cpp:1111-1145;
`topologicalSortExprs` recursion at :138-180; per-graph audio loops at :854-865;
`struct Graph` at synthdef_synth.hpp:80-94; `struct Synth` graph fields at
synthdef_synth.hpp:99-165.

---

## Already done (M2 built more scaffolding than the original plan assumed)

- **Iso-groups are fully implemented** -- `computeIsoGroups` (passes.x:803-886),
  the activation graph, topological ordering, and the `processEvents` iso-group
  event codegen. The original plan listed these as M3 work; they exist for the
  no-subgraph case. M3 only needs them to keep working when event-rate trees live
  in subgraphs (rare). C++ refs: `computeIsoGroups` synthdef_synth.cpp:682-766;
  event codegen synthdef_cpp_codegen.cpp:2816-2867.
- **`select` / `select2`** -- the closest non-branching relative -- is done
  end-to-end as a 3-input ternary (importer.x select/select2 -> `selectK`;
  types.x `_selectChans`/`_updateSelect`; codegen.x `_inlineSelect`). N-way select
  is stubbed (`/* FIXME select n-way */`, codegen.x); fold it into M3.
- `GraphCut.controlFlow` already exists in the synthc enum.

---

## Front-end representation (what we import)

`enum SignalExprKind` (synthdef.x:413-447):

    if_(SignalGraph, SignalGraph)    -- then, else; test is ins[0]
    for_(Int, SignalGraph)           -- count, body; body has a varexpr loop index
    switch_([SignalGraph])           -- N case graphs; selector is ins[0]
    varexpr String                   -- named loop/branch variable
    select / select2                 -- non-branching mux (already done)

`struct SignalGraph` (synthdef.x:151-156): `exprs [SignalExpr]`, `delays`,
`buffers`, `root SignalExpr`. Each branch/body is a fresh SignalGraph built by
`_makeSubGraph`/`_makeSubGraph1` (synthdef.x:850-905), which run the branch lambda
in fresh `\`curGraphExprs/Delays/Buffers` context. Branch lambdas **capture
enclosing signals by closure** -- so a branch's exprs may reference parent-graph
node IDs directly (see open question below). `for_` (synthdef.x:925-929) makes a
`varexpr` loop index and passes it to the body lambda.

Builders: `if_(test S, thenFn, elseFn)` and a no-else overload defaulting to 0;
Bool-test overloads fold at compile time. `switch(test, [GraphFn])`. `for_(name,
count, bodyFn)`.

S-expression serialization (synthdef.x:1050-1062), the format the differential
test round-trips through:

    (id IfExpr (test) (Graph rootId (exprs...)) (Graph rootId (exprs...)))
    (id ForExpr count (Graph rootId (exprs...)))
    (id SwitchExpr (selector) (Graph...) (Graph...) ...)     -- cases space-separated
    (id VarExpr name)                                        -- SEE BUG below
    (Graph rootId ( ...exprs... ))                           -- subgraph container

---

## C++ IR we mirror

Base `Expr` virtuals relevant to control flow (synthdef_expr.hpp:61-132):
`is_control_flow()`, `num_subgraphs()`, `get_subgraph(i)`, plus the usual
`calcShape`/`update_type`/`inputTypeConstraint`/`should_hash_cons`/`is_sink`.

Control-flow classes (all derive `ControlFlowExpr`, which sets
`is_control_flow()==true` and declares `insertPhiNodes`):

- **IfElseExpr** (synthdef_expr.hpp:627-670): fields `then_expr`, `else_expr`
  (both `PhiNodeExpr`); `num_subgraphs()==2`; `inputTypeConstraint(0)==any_int`
  (test), else `type`; `calcShape` requires scalar test, broadcasts then/else
  chans; `update_type` = `type & then->type & else->type` then pushes back to
  both branches (synthdef_expr.cpp:480-495).
- **SwitchExpr** (synthdef_expr.hpp:672-714): `vector<S> cases` (PhiNodes);
  `num_subgraphs()==cases.size()`; selector scalar; same type/shape merge pattern.
- **ForLoopExpr** (synthdef_expr.hpp:715-750): `loop_body` (PhiNode); count scalar
  int; `update_type` = `type & loop_body->type`.
- **PhiNodeExpr** (synthdef_expr.hpp:576-600): one input (the branch result) +
  `target` back-pointer to the control-flow expr; `setTarget` raises its rate to
  `max(in.rate, target.rate)`; `needs_input_temp_var()==true`;
  `should_hash_cons()==false`; `calcShape` broadcasts target+input chans
  (synthdef_expr.cpp:666-673); `update_type` = `type & in0->type`, propagates to
  target (synthdef_expr.cpp:457-467).
- **VarExpr** (synthdef_expr.hpp:554-574): `varName`, `init_type` (any_int for the
  loop index); no inputs; `calcShape` no-op; hashes by name (no equals_ override).
- **Inlet** (synthdef_expr.hpp:229-252) / **Outlet** (:254-278): subgraph I/O
  ports; not hash-consed; Outlet is a sink. (Whether if/for/switch actually use
  these vs. direct cross-graph refs is the open question.)

`struct Graph` (synthdef_synth.hpp:80-94): `synth`, `parent`, `serial`, `exprs`,
`subgraphs`, `loops` (audio-rate, per-graph), `delayBufs`, `sampleBufs`. New Graph
registers with `parent->subgraphs` and `synth->graphs` (synthdef_synth.cpp:31-39).

GraphCut rules (synthdef_synth.cpp:254-297; classification at
synthdef_expr.hpp:1504-1526):
- Inlet/Control/NoteParam -> `Input` (inst var).
- input needs_input_temp_var -> `Temp`; cross-graph input (`expr->graph !=
  input->graph`) -> `Graph` (inst var); rate change -> `Rate`; broadcast ->
  `Broadcast`.
- self: sink -> `Sink`; PhiNode -> `Phi`; 0 consumers -> `Unused`; >1 -> `FanOut`;
  control-flow -> `ControlFlow`.
- `is_temp_var`: Temp/Broadcast/ControlFlow/SeparateLoop/FanOut.
  `is_inst_var`: Rate/Input/Graph.

`addSubgraphAntecedents` (synthdef_synth.cpp:545-552): for each expr, for each
subgraph i, `expr->tree->antecedents.insert({get_subgraph(i)->tree, true})` --
i.e. the control-flow tree depends on each branch's phi tree, with a
separate-loop constraint.

`treesToLoops` (synthdef_synth.cpp:768-852): control-flow trees and `gets_own_loop`
trees each get their own GenLoop; others merge by `(graph, rate, chans, isoGroup)`.

---

## Codegen we mirror (synthdef_cpp_codegen.cpp)

Control-flow visitors emit a block then recurse into the subgraph's *own* loops:

- **IfElseExpr** (:1710-1732):
  `if (genExpr(test,vx0)) { genLoops(then->graph->loops); genDelayAdvance(then->graph) }
   else { genLoops(else->graph->loops); genDelayAdvance(else->graph) }`
- **SwitchExpr** (:1734-1756): `switch (std::min(u32(sel), u32(ncases-1))) { case i: {
  genLoops(cases[i]->graph->loops); ... } break; ... }`
- **ForLoopExpr** (:1757-1769): `for (i32 i = 0; i < count; ++i) { genLoops(body->graph->loops); ... }`
- **PhiNodeExpr** (:1705-1708): `genVarRef(target, cel) = genExpr(in0, cel);` --
  the branch assigns into the control-flow expr's result variable.
- **VarExpr** (:973): emits `p->varName` (the loop index `i`).
- **Inlet** (:884-910) / **Outlet** (:1627-1651): `((type**)p->inlets)[serial][idx]`
  read / `((type**)p->outlets)[serial][idx] = value` write.

Indentation via `g.indent++/--` around the recursion. Subgraph audio loops live in
`expr->branch->graph->loops` (populated by `splitRates`). Control-flow blocks are
inlined into `processAudio` (no separate functions). Iso-group event codegen at
:2816-2867 (already ported).

---

## Prerequisite bugs in the reference path (FIXED in M3.0)

Both are the same class as the BufVarRead parser fix, and both affected the real
`defSynth` path -- control flow had never round-tripped through the sexpr path.

1. **VarExpr serialization** -- synthdef.x:1062 emitted `"()%^ VarExpr %^)"`
   (stray leading `()`; name unquoted). `parseVarExpr` (synthdef_from_sexpr.cpp:893)
   wants a quoted string. Fixed to `"(%^ VarExpr \"%^\")"`.

2. **for_ count serialization** -- the front end emitted the count as a bare
   literal (`(id ForExpr count (Graph...))`), but `parseForExpr`
   (synthdef_from_sexpr.cpp:941) expects the count as a *signal input*:
   `(id ForExpr (countId) (Graph...))` (ForLoopExpr takes `S count`). Fixed the
   `for_` builder to pass `[count asSignal]` as an input and the serializer to
   emit `o inputsToLisp` instead of the literal. The synthc importer will read
   the count from that constant input node (like the C++), not the enum payload.

## M3.0 findings (the spike) -- resolved

Dumped minimal `if_` and `for_` synths through the C++ (`synthdefAnalysisDump` +
`synthdefGenCppFromSexpr`). Conclusions that shape M3.1+:

1. **Cross-graph references are DIRECT -- no Inlet nodes.** An enclosing value
   used inside a branch (e.g. `sig` in `if_(sig>0, ||sig*0.5, ||sig*0.1)`) is left
   in the enclosing graph and marked `GraphCut::Graph`, which makes it an
   *instance variable*. The branch reads that inst var directly. Generated C++:

        f32 v17;                              // struct: the cross-graph value
        ...
        p->v17 = ((2.0f * f32(v2)) - 1.0f);   // assigned in the OUTER loop, cut "Graph"
        f32 v26;                              // phi-result temp, declared before the block
        if ((p->v17 > 0.0f)) {                // ControlFlow
            f32 v21 = (p->v17 * 0.5f);        // branch reads p->v17 directly
            v26 = v21;                        // PhiNode: assign control-flow node's var
        } else {
            f32 v24 = (p->v17 * 0.1f);
            v26 = v24;
        }
        ((f32**)p->outlets)[0][0] = v26;      // consumer reads the phi result

   So if/for/switch need NO inlet/outlet synthesis -- just tag nodes by graph and
   apply the `Graph` cut to cross-graph-referenced nodes. (Inlet/Outlet exprs are
   for voicer/spectral, M4.)

2. **Phi result = the control-flow node's own variable.** `PhiNodeExpr` codegen
   emits `genVarRef(target) = branch_value`, where `target` is the if/for/switch
   node. That node is cut `ControlFlow` (a temp var), so its `vN` temp is declared
   just before the block and read by consumers. Both branches assign the same var.

3. **Branch bodies are nested per-graph audio loops** emitted inside the
   if/else/for block (`genLoops(branch->graph->loops)`), with a delay-advance per
   subgraph.

4. **Multi-graph dump format** (freeze for M3.1): INIT/RESET/EVENT loop sections
   stay global; then `-- AUDIO` followed by `GRAPH 0`, its audio loops, `GRAPH 1`,
   its audio loops, etc. Control-flow tree antecedents print a `*` suffix on
   separate-loop antecedents, e.g. `TREE 11 [8* 10* 6]` (8,10 = branch phi trees
   with the separate-loop flag; 6 = the cross-graph sig tree).

5. **for_ codegen shape**: `for (i32 i = 0; i < <count>; ++i) { <body loops> }`;
   the `VarExpr` loop index emits as the bare C++ loop var name. Count is read from
   the constant input node.

---

## Data-model changes (M3.1)

`Ctx` (ir.x:327-367) is single-graph today. Add:

- `graphOf [Int]` -- per-node graph index (parallel array).
- `graphs [GraphInfo]` table: `{ serial Int, parent Int, audioLoops [Int],
  usesRng Bool }`. Audio loops move from the single `ctx.audioLoops` to per-graph
  buckets (the existing `audioLoops` field comment already notes "the C++ keeps
  these per Graph; M1 has only the root graph"). Init/reset/event loops stay
  global.
- `subs [[NIdx]]` -- per-node subgraph phi-root indices (empty except for
  control-flow nodes), the synthc analog of then_expr/else_expr/cases.
- NodeKinds: `ifK`, `switchK(Int ncases)`, `forK`, `phiK`, `varK(String)`.
  (`inletK`/`outletK` already exist.)
- Dump: emit `GRAPH N` headers per graph and tag audio loops by graph (today the
  dump hard-codes `GRAPH 0`, ir.x dumpToString).

Exhaustive matches that must gain the new kinds: `nodeStr`, `isSinkKind`,
`shouldHashCons`, `calcShapeOf`, `_kindKey`, `_delayIdxOfKind` (default ok).

---

## Staging

| Sub-milestone | Work | Risk |
|---|---|---|
| **M3.0 Spike** | DONE. Fixed VarExpr + for_ serialization; resolved inlets-vs-cross-graph-refs (direct refs, no inlets); froze the multi-graph dump format. See "M3.0 findings". | -- |
| **M3.1 Data model** | DONE. `graphOf [Int]` + `subs [[NIdx]]` per-node arrays; `graphs [GraphInfo]` (per-graph `audioLoops`); `Loop.graphOf`; NodeKinds `ifK`/`switchK(Int)`/`forK`/`phiK(Int)`/`varK(String)`; multi-graph dump (`GRAPH N`); `splitRates` per-graph audio bucketing. Byte-identical for single-graph; 382 lang tests pass. | low |
| **M3.2 Import + topo + cuts** | if_ DONE; for_/switch_ pending (M3.2b). Recursive subgraph import (`_importSubgraph` wraps each branch root in `phiK`; `_addCFNode` stores `subs`); subgraph-aware `topologicalSortExprs` (`_topoKids` = ins ++ subs); GraphCut `Graph`/`Phi`/`ControlFlow`; real `addSubgraphAntecedents`; control-flow type/shape (`_updateCF`/`_updatePhi`, test->any_int). Tier-1 dump byte-matches if/else, no-else, nested, multi-if. | **high** |
| **M3.3 Types/shape** | DONE (folded into M3.2). `_cfChans` broadcasts a control-flow node over its branch phis; `_phiChans` = broadcast(current, target, input); `shapeInference` re-queues the phi target / control-flow phis on a shape change; `varK` stays chans 0 (Expr default). Switch builder rewritten to avoid an un-codegen-able auto-map. | med |
| **M3.4 Codegen** | DONE. `_genIf`/`_genFor`/`_genSwitch` emit the block + recurse into each branch's per-graph audio loops (`_branchLoops`); phi -> `target = value`; control-flow result var declared by genLoop (hardcoded one tab); VarExpr emits the bare loop-var name but is still declared as an inst var; switch reproduces the C++ +1 indent leak; chans-0 mask prints as the u64 SIZE_MAX. | med |
| **M3.5 Parity** | DONE. synthc_controlflow_diff.x checks Tier-1 (dump) + Tier-2 (codegen) for if/for/switch (basic, no-else, nested, multi-if, bare-capture). Tier-3: a `cf` if_-waveshaper render A/B is bit-identical. GC-stable across runs. | -- |

Start with **M3.0**: its findings (inlets vs cross-graph refs, the dump format)
shape the M3.1 data model.

## Highest-risk areas

1. Cross-graph reference semantics -- the inlet-vs-direct-ref question and getting
   `GraphCut::Graph` -> instance-variable placement byte-exact.
2. Phi-node type/shape inference -- the bidirectional phi <-> target propagation;
   mirror `PhiNodeExpr::update_type` / `IfElseExpr::update_type` structurally.
3. Tree antecedents across graphs -- `addSubgraphAntecedents` + the separate-loop
   constraint so control-flow trees schedule correctly.

## Rough size

~600-900 LOC of `.x` (data model + import + passes + types + codegen) plus the two
reference-path bug fixes, concentrated in M3.2.

## Deferred to later milestones

VoicerExpr / NoteParam voice allocation (M4), SpectralChainExpr (M4), SIMD (M5).
These are also `ControlFlowExpr`s and reuse the M3 subgraph machinery.
