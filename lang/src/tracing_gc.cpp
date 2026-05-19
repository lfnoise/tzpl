// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "tracing_gc.hpp"
#include "vm.hpp"
#include "value.hpp"   // CodeBlock, StackMap, all Obj subclasses

namespace ts {

// Tag-dispatched scan. Replaces a virtual call (vtable lookup + indirect
// branch) with a switch over the 1-byte gcTag_ field, calling the specific
// subclass's gcScanChildren via qualified non-virtual syntax. The qualified
// form `static_cast<T*>(obj)->T::gcScanChildren(gc)` bypasses the vtable
// even though gcScanChildren is virtual -- the compiler emits a direct call.
//
// GCTag::Default keeps the virtual fallback so untagged subclasses still
// work; this is a strict optimization. Untagged subclasses can opt in
// incrementally without breaking anything in the meantime.
void gcScanByTag(GCObj* obj, TracingGC& gc) {
    switch (obj->gcTag()) {
    case GCTag::None:
        return;  // leaf: no children to scan
    case GCTag::ObjArray:
        static_cast<ObjArray*>(obj)->ObjArray::gcScanChildren(gc);
        return;
    case GCTag::InlineArray:
        static_cast<InlineArray*>(obj)->InlineArray::gcScanChildren(gc);
        return;
    case GCTag::ListNode:
        static_cast<ListNode*>(obj)->ListNode::gcScanChildren(gc);
        return;
    case GCTag::RefValue:
        static_cast<RefValue*>(obj)->RefValue::gcScanChildren(gc);
        return;
    case GCTag::InlineRef:
        static_cast<InlineRef*>(obj)->InlineRef::gcScanChildren(gc);
        return;
    case GCTag::Struct:
        static_cast<Struct*>(obj)->Struct::gcScanChildren(gc);
        return;
    case GCTag::Tuple:
        static_cast<Tuple*>(obj)->Tuple::gcScanChildren(gc);
        return;
    case GCTag::Enum:
        static_cast<Enum*>(obj)->Enum::gcScanChildren(gc);
        return;
    case GCTag::MapObj:
        static_cast<MapObj*>(obj)->MapObj::gcScanChildren(gc);
        return;
    case GCTag::SetObj:
        static_cast<SetObj*>(obj)->SetObj::gcScanChildren(gc);
        return;
    case GCTag::Lambda:
        static_cast<Lambda*>(obj)->Lambda::gcScanChildren(gc);
        return;
    case GCTag::CoroutineObj:
        static_cast<CoroutineObj*>(obj)->CoroutineObj::gcScanChildren(gc);
        return;
    case GCTag::CoroutineFrame:
        static_cast<CoroutineFrame*>(obj)->CoroutineFrame::gcScanChildren(gc);
        return;
    case GCTag::Default:
    default:
        obj->gcScanChildren(gc);  // virtual fallback for untagged subclasses
        return;
    }
}

u32 gcScanChunkByTag(GCObj* obj, TracingGC& gc, u32 cursor) {
    // Only container types push partials; restrict the fast paths to those.
    switch (obj->gcTag()) {
    case GCTag::ObjArray:
        return static_cast<ObjArray*>(obj)->ObjArray::gcScanChunk(gc, cursor);
    case GCTag::InlineArray:
        return static_cast<InlineArray*>(obj)->InlineArray::gcScanChunk(gc, cursor);
    case GCTag::MapObj:
        return static_cast<MapObj*>(obj)->MapObj::gcScanChunk(gc, cursor);
    case GCTag::SetObj:
        return static_cast<SetObj*>(obj)->SetObj::gcScanChunk(gc, cursor);
    default:
        return obj->gcScanChunk(gc, cursor);
    }
}

// Look up the stack map for the given PC offset in a CodeBlock. Stack maps
// are appended in code order during codegen, so pcOffset values are strictly
// monotonically increasing -- binary search applies. With ~30 stack maps per
// CodeBlock and frame stacks that go 1000+ deep under binary_trees-style
// recursion, this drops the per-frame lookup from O(N) to O(log N), which
// is what makes synchronous root scan fast enough that incrementalizing it
// is no longer the dominant pause source.
static StackMap const* findStackMap(CodeBlock const* cb, u32 pcOffset) {
    if (!cb) return nullptr;
    auto const& v = cb->stackMaps_;
    u32 lo = 0, hi = (u32)v.size();
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        u32 mp = v[mid].pcOffset;
        if (mp == pcOffset) return &v[mid];
        if (mp < pcOffset)  lo = mid + 1;
        else                hi = mid;
    }
    return nullptr;
}

void TracingGC::mark(GCObj* obj) {
    if (!obj) return;
    if (obj->isImmortal()) return;
    if (obj->color_ != GCColor::White) return;
    obj->color_ = GCColor::Gray;
    grayWorklist_.push_back(obj);
}

void TracingGC::resetColors() {
    // Every non-immortal live object starts a cycle as White. Immortals
    // (object constants, type metadata) stay invisible to color machinery;
    // mark() short-circuits on them.
    for (GCObj* obj = vm_.allObjsHead_; obj; obj = obj->allObjsNext_) {
        if (!obj->isImmortal()) obj->color_ = GCColor::White;
    }
}

// Phase 6 step 5: the old synchronous markRoots() has been split into three
// incremental step_root_* methods, each with a cursor so the scan can pause
// and resume across step() calls under the deadline budget. requestCycle
// seeds the cursors; step_mark drives them before draining gray. Each
// frame is visited at the moment the marker reaches it, so the stack map
// always matches the current PC -- mutator activity between cycle start
// and root visit is harmless. SATB on heap stores covers the snapshot
// invariant for heap fields that get overwritten before their referent
// is marked. Plain register overwrites are not snapshotted, which is
// correct: once a register is overwritten with a non-ref and no other root
// holds the old value, it's genuinely garbage.

// Walk numGlobals: scales with declared globals, not heap objects. Typical
// programs have a few thousand globals; this stays well under a 100 us
// budget on its own, but is incremental anyway so a hypothetical 100k-
// global REPL session can't blow it either.
void TracingGC::step_root_globals(u64 deadlineNanos, u32& sinceCheck, u32& done) {
    u32 n = vm_.numGlobals();
    while (rootGlobalCursor_ < n) {
        u32 i = rootGlobalCursor_++;
        if (vm_.globalIsObj(i)) {
            if (Obj* o = vm_.global(i).o) {
                mark(o); ++lastRootCount_;
            }
        }
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return;
        }
    }
    rootPhase_ = RootPhase::DynVars;
    rootDynVarCursor_ = 0;
}

void TracingGC::step_root_dynvars(u64 deadlineNanos, u32& sinceCheck, u32& done) {
    u32 n = vm_.numDynVars();
    while (rootDynVarCursor_ < n) {
        u32 i = rootDynVarCursor_++;
        if (vm_.dynVarIsObj(i)) {
            if (Obj* o = vm_.dynVar(i).o) {
                mark(o); ++lastRootCount_;
            }
        }
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return;
        }
    }
    rootPhase_ = RootPhase::Frames;
    rootFrameCursor_ = 0;
}

// Walk frames bottom-up so the cursor advances monotonically even if new
// inner calls are pushed mid-scan -- they land at higher indices and get
// visited as the cursor catches up. If frames return mid-scan (frameCount_
// drops below cursor), we stop: the popped frame's roots were visited
// earlier when they were still live, and the returned-from work was
// captured by the SATB barrier on any heap stores that happened.
//
// Each frame visit reads its CURRENT PC and registers, so the stack map
// matches the bytecode's view at that exact moment. The
// CallFrame::baseReg-stores-caller's-base convention from pushFrame is
// the same as the old synchronous version.
void TracingGC::step_root_frames(u64 deadlineNanos, u32& sinceCheck, u32& done) {
    while (rootFrameCursor_ < vm_.frameCount_) {
        u32 i = rootFrameCursor_++;
        CallFrame const& f = vm_.frames_[i];
        CodeBlock const* cb = f.codeBlock;
        if (cb && !cb->code.empty()) {
            bool isTop = (i + 1 == vm_.frameCount_);
            Code const* pc = isTop ? vm_.pc_ : vm_.frames_[i + 1].returnPC;
            if (pc) {
                u32 pcOffset = (u32)(pc - cb->code.data());
                if (StackMap const* sm = findStackMap(cb, pcOffset)) {
                    u32 activeBase = isTop ? vm_.baseReg_
                                           : vm_.frames_[i + 1].baseReg;
                    Word const* base = vm_.regs_ + activeBase;
                    for (u16 reg : sm->liveRefRegs) {
                        if (Obj* o = base[reg].o) {
                            mark(o); ++lastRootCount_;
                        }
                    }
                }
            }
        }
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return;
        }
    }
    // Mark all open upvalues as roots. Each open UpVar is referenced from
    // vm_.openUpVars_ (and possibly from closures we'll reach via the frame
    // scan above), but until a closure picks it up it is reachable only via
    // that head pointer. Walking the list here guarantees a GC fired
    // between op_capture_upvar and op_make_lambda still keeps the cell.
    // The contained value (location_[0..sizeWords)) lives in the register
    // file -- already scanned via stack maps above -- so we don't need to
    // re-trace it; we just need to keep the UpVar object alive.
    for (UpVar* uv = vm_.openUpVars_; uv != nullptr; uv = uv->next_) {
        mark(uv); ++lastRootCount_;
    }
    rootPhase_ = RootPhase::Extras;
    rootExtraCursor_ = 0;
}

// Host wrappers (e.g. NRTVM) register tables of Obj* that aren't reachable
// from globals/dynvars/frames. Each scanner runs to completion; we treat the
// whole scanner as one work unit for the deadline budget because scanners
// typically iterate small tables. If a scanner ever needs chunked progress,
// it can call gc.mark() on a per-entry basis and still finish quickly.
void TracingGC::step_root_extras(u64 deadlineNanos, u32& sinceCheck, u32& done) {
    u32 n = vm_.numExtraRootScanners();
    while (rootExtraCursor_ < n) {
        u32 i = rootExtraCursor_++;
        vm_.invokeExtraRootScanner(i, *this);
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return;
        }
    }
    rootPhase_ = RootPhase::Done;
}

// Legacy entry point kept for compatibility with runFullCycle / tests that
// expect requestCycle to seed gray synchronously. Just drives the whole
// incremental root scan to completion.
void TracingGC::markRoots() {
    lastRootCount_ = 0;
    rootPhase_ = RootPhase::Globals;
    rootGlobalCursor_ = 0;
    u32 sinceCheck = 0, done = 0;
    while (rootPhase_ != RootPhase::Done) {
        switch (rootPhase_) {
        case RootPhase::Globals: step_root_globals(kGCNoDeadline, sinceCheck, done); break;
        case RootPhase::DynVars: step_root_dynvars(kGCNoDeadline, sinceCheck, done); break;
        case RootPhase::Frames:  step_root_frames(kGCNoDeadline, sinceCheck, done);  break;
        case RootPhase::Extras:  step_root_extras(kGCNoDeadline, sinceCheck, done);  break;
        case RootPhase::Done:    break;
        }
    }
}

void TracingGC::pushPartial(GCObj* container) {
    pendingContainers_.push_back({container, 0});
}

u32 TracingGC::step_mark(u64 deadlineNanos) {
    u32 done = 0;
    u32 sinceCheck = 0;

    // Phase 6 step 5: incremental root scan happens here under the deadline
    // budget instead of synchronously at requestCycle time. The substate
    // machine drains globals, dyn vars, and frames in order, with cursors
    // so each step picks up where the last left off.
    while (rootPhase_ != RootPhase::Done) {
        switch (rootPhase_) {
        case RootPhase::Globals: step_root_globals(deadlineNanos, sinceCheck, done); break;
        case RootPhase::DynVars: step_root_dynvars(deadlineNanos, sinceCheck, done); break;
        case RootPhase::Frames:  step_root_frames(deadlineNanos, sinceCheck, done);  break;
        case RootPhase::Extras:  step_root_extras(deadlineNanos, sinceCheck, done);  break;
        case RootPhase::Done:    break;
        }
        if (gcMonoNanos() >= deadlineNanos) return done;
    }

    // Phase 6 step 3: drain the partial-container queue first. Each entry
    // is a large container already Blacked; we advance its child scan by
    // kFanoutChunk per work unit. Bounding this is what caps the worst-
    // case mark step for programs holding 100k-entry Maps / Arrays / etc.
    while (!pendingContainers_.empty()) {
        PartialEntry& e = pendingContainers_.back();
        u32 next = gcScanChunkByTag(e.obj, *this, e.cursor);
        if (next == ~u32{0}) {
            pendingContainers_.pop_back();
        } else {
            e.cursor = next;
        }
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return done;
        }
    }

    while (!grayWorklist_.empty()) {
        GCObj* obj = grayWorklist_.back();
        grayWorklist_.pop_back();
        if (obj->color_ != GCColor::Gray) { ++done; }
        else {
            obj->color_ = GCColor::Black;
            ++currentBlackCount_;
            // Transitive scan via tag-dispatched gcScanByTag. Tagged subclasses
            // (Tuple, Struct, ObjArray, ListNode, RefValue, MapObj, SetObj,
            // Lambda, CoroutineObj, ...) get a direct non-virtual call;
            // untagged ones (GCTag::Default) fall back to the virtual.
            // Containers with > kFanoutThreshold entries push themselves to
            // the partial queue and return immediately.
            gcScanByTag(obj, *this);
            ++done;
        }
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return done;
        }
        // A gcScan call may have enqueued a partial container. Service
        // that before grabbing the next gray, to keep the partial queue
        // from growing unboundedly while gray drains.
        if (!pendingContainers_.empty()) {
            while (!pendingContainers_.empty()) {
                PartialEntry& e = pendingContainers_.back();
                u32 next = gcScanChunkByTag(e.obj, *this, e.cursor);
                if (next == ~u32{0}) pendingContainers_.pop_back();
                else e.cursor = next;
                ++done;
                if (++sinceCheck >= kCheckEvery) {
                    sinceCheck = 0;
                    if (gcMonoNanos() >= deadlineNanos) return done;
                }
            }
        }
    }
    // Both worklists drained: advance to Sweep.
    phase_ = Phase::Sweep;
    sweepSlot_ = &vm_.allObjsHead_;
    return done;
}

u32 TracingGC::step_sweep(u64 deadlineNanos) {
    u32 done = 0;
    u32 sinceCheck = 0;
    // Singly-linked sweep. `sweepSlot_` is the slot in the previous node
    // (or the head pointer itself) that points at the current candidate.
    // Unlinking a freed object is just `*sweepSlot_ = obj->next_`; sweep
    // doesn't need a prev pointer per object.
    while (*sweepSlot_) {
        GCObj* obj = *sweepSlot_;
        if (!obj->isImmortal() && obj->color_ == GCColor::White) {
            ++currentWhiteCount_;
            // Unlink before delete so subclass destructors that allocate
            // (rare, but defensible) can't accidentally re-enter sweep
            // with a half-detached node visible from the list.
            *sweepSlot_ = obj->allObjsNext_;
            delete obj;
        } else {
            sweepSlot_ = &obj->allObjsNext_;
        }
        ++done;
        if (++sinceCheck >= kCheckEvery) {
            sinceCheck = 0;
            if (gcMonoNanos() >= deadlineNanos) return done;
        }
    }
    if (sweepSlot_ && !*sweepSlot_) {
        lastWhiteCount_ = currentWhiteCount_;
        lastBlackCount_ = currentBlackCount_;
        currentWhiteCount_ = 0;
        currentBlackCount_ = 0;
        phase_ = Phase::Idle;
        ++cyclesCompleted_;
        // Reset the alloc counter so the next cycle is triggered by fresh
        // allocation pressure, not the alloc history before this cycle.
        allocsSinceLastCycle_ = 0;
        // Phase 5.4: set the next-cycle trigger proportional to the live
        // set we just observed. Doing this here (after a real measurement)
        // adapts the GC rate to the program: a tight inner loop that
        // builds many short-lived nodes will see lastBlackCount stay low
        // and cycle frequently; a program holding a large stable tree
        // will see lastBlackCount large and cycle rarely. Bottom-bounded
        // by kMinTriggerAllocs so we don't churn on very small heaps.
        u64 want = (u64)lastBlackCount_ * (u64)kGrowthFactor;
        if (want < kMinTriggerAllocs) want = kMinTriggerAllocs;
        if (want > UINT32_MAX) want = UINT32_MAX;
        nextTriggerAllocs_ = (u32)want;
    }
    return done;
}

u32 TracingGC::step(u64 deadlineNanos, GCStepSource source) {
    // step_mark may transition to Sweep when the gray worklist drains; do
    // both phases in one call so a slack deadline gets used fully rather
    // than burning a host tick on the phase boundary. Wrap the dispatch in
    // wall-clock measurement; per-source split lets RT-callback max pause
    // be observed separately from the looser NRT heartbeat numbers.
    u64 start = gcMonoNanos();
    u32 done = 0;
    if (phase_ == Phase::Mark) {
        done += step_mark(deadlineNanos);
        if (phase_ == Phase::Sweep && gcMonoNanos() < deadlineNanos) {
            done += step_sweep(deadlineNanos);
        }
    } else if (phase_ == Phase::Sweep) {
        done += step_sweep(deadlineNanos);
    }
    u64 elapsed = gcMonoNanos() - start;
    ++stepCount_;
    stepSumNanos_ += elapsed;
    if (elapsed > stepMaxNanos_) stepMaxNanos_ = elapsed;
    u8 si = (u8)source;
    if (si < (u8)GCStepSource::kCount) {
        ++stepCountBySource_[si];
        stepSumNanosBySource_[si] += elapsed;
        if (elapsed > stepMaxNanosBySource_[si]) stepMaxNanosBySource_[si] = elapsed;
    }
    return done;
}

void TracingGC::requestCycle() {
    if (phase_ != Phase::Idle) return;  // already running
    resetColors();
    currentBlackCount_ = 0;
    currentWhiteCount_ = 0;
    lastRootCount_ = 0;
    phase_ = Phase::Mark;
    // Phase 6 step 5: do NOT call markRoots synchronously here. step_mark
    // will drive the root scan incrementally under the deadline budget,
    // starting from RootPhase::Globals.
    rootPhase_ = RootPhase::Globals;
    rootGlobalCursor_ = 0;
}

void TracingGC::runFullCycle() {
    requestCycle();
    while (phase_ != Phase::Idle) {
        step(kGCNoDeadline, GCStepSource::Sync);  // unbounded
    }
}

} // namespace ts
