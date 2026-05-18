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
#include "value.hpp"   // CodeBlock, StackMap
#include "arc.hpp"

namespace ts {

// Look up the stack map for the given PC offset in a CodeBlock. Stack maps
// are appended in code order during codegen, so pcOffset values are
// monotonically increasing; a binary search would be faster but a linear
// scan is fine while live-ref counts per CodeBlock are modest.
static StackMap const* findStackMap(CodeBlock const* cb, u32 pcOffset) {
    if (!cb) return nullptr;
    for (auto const& sm : cb->stackMaps_) {
        if (sm.pcOffset == pcOffset) return &sm;
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

void TracingGC::markRoots() {
    lastRootCount_ = 0;

    // Root set 1: AutoReleasePool. Every entry contributes +1 to its
    // target's refcount, so it is a real root under the current ARC.
    for (GCObj* obj : vm_.autoReleasePool()) {
        if (obj) { mark(obj); ++lastRootCount_; }
    }

    // Root set 2: globals. Only slots flagged as holding an Obj* in
    // globalIsObj_ are dereferenced. Inline-composite globals span
    // multiple Word slots; for Phase 3b we use the same conservative
    // policy as the rest of the codebase -- globalIsObj_ is set per
    // base slot, and that slot's Word::o is the boxed pointer.
    for (u32 i = 0; i < vm_.numGlobals(); ++i) {
        if (vm_.globalIsObj(i)) {
            if (Obj* o = vm_.global(i).o) {
                if (auto* g = dynamic_cast<GCObj*>(o)) {
                    mark(g); ++lastRootCount_;
                }
            }
        }
    }

    // Root set 3: dynamic-scope variables. Same convention as globals.
    for (u32 i = 0; i < vm_.numDynVars(); ++i) {
        if (vm_.dynVarIsObj(i)) {
            if (Obj* o = vm_.dynVar(i).o) {
                if (auto* g = dynamic_cast<GCObj*>(o)) {
                    mark(g); ++lastRootCount_;
                }
            }
        }
    }

    // The deferred-delete queue is intentionally NOT a root. Its entries
    // have refcount 0 and are awaiting destruction; the tracer should leave
    // them white so that tracing's "white set" can be cross-checked against
    // ARC's "about to free" set during validation.

    // Phase 5.1: stack-map roots. Walk every active call frame and use its
    // saved PC's stack map to mark register-held GC references. For the top
    // frame the saved PC is vm_.pc_ (the current instruction, an op_safepoint
    // when we get here via safepointPoll). For lower frames it is
    // frames_[i+1].returnPC -- the resume address the caller will hit when
    // the inner frame returns. CodeBlocks only carry stack maps at safepoint
    // emission sites today (loop tails and function entry); Phase 5.2 adds
    // call-site stack maps so lower frames also have precise root data.
    for (u32 i = 0; i < vm_.frameCount_; ++i) {
        CallFrame const& f = vm_.frames_[i];
        CodeBlock const* cb = f.codeBlock;
        if (!cb || cb->code.empty()) continue;
        Code const* pc = (i + 1 == vm_.frameCount_) ? vm_.pc_ : vm_.frames_[i + 1].returnPC;
        if (!pc) continue;
        u32 pcOffset = (u32)(pc - cb->code.data());
        StackMap const* sm = findStackMap(cb, pcOffset);
        if (!sm) continue;
        Word const* base = vm_.regs_ + f.baseReg;
        for (u16 reg : sm->liveRefRegs) {
            if (Obj* o = base[reg].o) {
                if (auto* g = dynamic_cast<GCObj*>(o)) {
                    mark(g); ++lastRootCount_;
                }
            }
        }
    }
}

u32 TracingGC::step_mark(u32 budget) {
    u32 done = 0;
    while (done < budget && !grayWorklist_.empty()) {
        GCObj* obj = grayWorklist_.back();
        grayWorklist_.pop_back();
        if (obj->color_ != GCColor::Gray) { ++done; continue; }
        obj->color_ = GCColor::Black;
        ++currentBlackCount_;
        // Phase 3 transitive scan: walk every Obj* child via the virtual
        // gcScanChildren. Default no-op for classes without GC children;
        // overridden mirror of releaseChildren for the rest (Ref, Tuple,
        // Struct, Enum, ObjArray, MapObj, SetObj, ListNode, ListGenerator
        // subclasses, Lambda, CoroutineFrame, CoroutineObj, AnyObj, ...).
        obj->gcScanChildren(*this);
        ++done;
    }
    if (grayWorklist_.empty()) {
        phase_ = Phase::Sweep;
        sweepCursor_ = vm_.allObjsHead_;
    }
    return done;
}

u32 TracingGC::step_sweep(u32 budget) {
    u32 done = 0;
    while (done < budget && sweepCursor_) {
        GCObj* obj = sweepCursor_;
        sweepCursor_ = obj->allObjsNext_;
        if (obj->isImmortal()) { ++done; continue; }
        if (obj->color_ == GCColor::White) {
            ++currentWhiteCount_;
            // Shadow mode (Phase 3): do not free here. ARC owns reclamation
            // until Phase 5. The white count is exposed for validation.
        }
        ++done;
    }
    if (!sweepCursor_) {
        lastWhiteCount_ = currentWhiteCount_;
        lastBlackCount_ = currentBlackCount_;
        currentWhiteCount_ = 0;
        currentBlackCount_ = 0;
        phase_ = Phase::Idle;
        // Reset the alloc counter so the next cycle is triggered by fresh
        // allocation pressure, not the alloc history before this cycle.
        allocsSinceLastCycle_ = 0;
    }
    return done;
}

u32 TracingGC::step(u32 budget) {
    switch (phase_) {
    case Phase::Idle:  return 0;
    case Phase::Mark:  return step_mark(budget);
    case Phase::Sweep: return step_sweep(budget);
    }
    return 0;
}

void TracingGC::requestCycle() {
    if (phase_ != Phase::Idle) return;  // already running
    resetColors();
    currentBlackCount_ = 0;
    currentWhiteCount_ = 0;
    phase_ = Phase::Mark;
    markRoots();
    if (grayWorklist_.empty()) {
        // No roots reached anything: jump straight to sweep.
        phase_ = Phase::Sweep;
        sweepCursor_ = vm_.allObjsHead_;
    }
}

void TracingGC::runFullCycle() {
    requestCycle();
    while (phase_ != Phase::Idle) {
        step(1u << 20);  // effectively unbounded
    }
}

} // namespace ts
