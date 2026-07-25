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

//
//  tzpl_vm_commands.hpp
//  bridge
//
//  Engine command subclasses for dispatching events to the Silo's
//  attached RT VM. These flow through the same from_nrt_ FIFO and
//  scheduler as all other engine commands.
//

#ifndef tzpl_vm_commands_hpp
#define tzpl_vm_commands_hpp

#include "tzpl_command.hpp"
#include "tzpl_silo.hpp"
#include "vm.hpp"
#include "value.hpp"
#include "tracing_gc.hpp"

namespace bridge {

// Cross-VM actor message delivery (NRT -> silo). Carries the encoded Msg
// (raw bytes, copied on the NRT thread, no Obj* crosses the heap boundary) and
// the silo VM's delivery trampoline. On the silo's RT thread it rebuilds a
// Bytes in the silo heap and calls the trampoline, which decodes the message
// and enqueues it into the named local actor.
struct DeliverActorMsgCmd : engine::Command {
    ts::CodeBlock* deliverFn_;      // tzpl_actor_deliver(Symbol, Bytes)
    ts::SymbolPtr  name_;           // target actor name (process-global symbol)
    std::vector<u8> bytes_;         // encoded Msg, owned

    DeliverActorMsgCmd(ts::CodeBlock* fn, ts::SymbolPtr name,
                       const u8* data, size_t len)
        : deliverFn_(fn), name_(name),
          bytes_(data, data + len) {}

    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && deliverFn_) {
            vm->makeCurrent();
            auto* b = new ts::BytesObj(bytes_.data(), bytes_.size());  // silo heap
            ts::Word args[2];
            args[0].s = name_;
            args[1].o = b;
            vm->callFunction(deliverFn_, args, 2);
            vm->gcHeartbeat();
        }
    }
};

// Dispatches an event to the Silo's attached VM.
// Created on an NRT thread, sent through from_nrt_ or the scheduler.
struct VMEventCmd : engine::Command {
    enum EventKind { Timer, NoteOn, NoteOff, ControlChange, Custom };
    EventKind kind_;
    ts::CodeBlock* handler_;       // the handler function to call
    ts::Word args_[8];             // inline argument storage (no allocation)
    u16 argc_;

    VMEventCmd(EventKind kind, ts::CodeBlock* handler,
               const ts::Word* args = nullptr, u16 argc = 0)
        : kind_(kind), handler_(handler), argc_(std::min(argc, (u16)8))
    {
        if (args) {
            for (u16 i = 0; i < argc_; ++i) args_[i] = args[i];
        }
    }

    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && handler_) {
            vm->makeCurrent();
            vm->callFunction(handler_, args_, argc_);
            vm->gcHeartbeat();
        }
    }
};

// Dispatches an event to the Silo's attached VM using a Callable (Lambda).
// The callable Obj* is retained by this command and released after execution.
struct VMCallableCmd : engine::Command {
    ts::Obj* callable_;           // retained Callable (Lambda or Primitive)

    explicit VMCallableCmd(ts::Obj* callable) : callable_(callable) {
    }

    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && callable_) {
            vm->makeCurrent();
            vm->callCallable(callable_, nullptr, 0);
            vm->gcHeartbeat();
        }
    }

    bool doNRT(engine::Silo* s) override {
        // Release the callable on the NRT thread.
        // This is safe because the NRT thread can acquire the VM for cleanup.
        if (callable_) {
            auto* vm = static_cast<ts::VM*>(s->vm_);
            if (vm) {
                vm->makeCurrent();
                vm->gcHeartbeat();
            }
        }
        return true;
    }
};

// Installs new compiled code on the RT VM.
// Created on an NRT thread (after compilation), sent through from_nrt_.
struct CodeInstallCmd : engine::Command {
    ts::CompileResult* newCode_;

    explicit CodeInstallCmd(ts::CompileResult* code) : newCode_(code) {}

    void doRT(engine::Silo* s) override {
        auto* vm = static_cast<ts::VM*>(s->vm_);
        if (vm && newCode_) {
            vm->makeCurrent();
            vm->install(*newCode_);
        }
    }

    bool doNRT(engine::Silo* s) override {
        // newCode_ ownership is managed by the caller.
        // The CompileResult holds CodeBlocks that are system-allocated
        // and managed by the Compiler's compilation context.
        return true;
    }
};

// Per-buffer GC tick for the attached VM. Called from the RT audio thread
// once per audio buffer. Phase 6: this drives time-bounded tracing GC
// progress regardless of whether any language code ran on this callback --
// without it, an in-flight cycle would stall while DSP runs without firing
// language events. The deadline is tight (200 us) so DSP keeps most of the
// block; tune via per-VM setGCStepBudgetNanos before attach.
//
// kRTGCBudgetNanos is the fallback if the VM hasn't had its budget set
// explicitly. For a 5.8 ms audio block (256 frames @ 44.1 kHz), 200 us
// is ~3% of the block -- generous for DSP, still meaningful GC progress.
inline constexpr u64 kRTGCBudgetNanos = 200'000;
inline void rtVMHeartbeat(void* vm, engine::Silo* s) {
    auto* v = static_cast<ts::VM*>(vm);
    // Prefer the VM's configured budget if it has been set tighter than
    // the NRT default; otherwise use the conservative RT default. Picking
    // min() lets per-VM setGCStepBudgetNanos override the default downward
    // without the engine knowing about per-VM config.
    u64 budget = v->gcStepBudgetNanos();
    if (budget > kRTGCBudgetNanos) budget = kRTGCBudgetNanos;
    v->rtTick(ts::gcMonoNanos() + budget);

    // Republish the collector's counters for the performance monitor. They
    // are plain members owned by this thread, so reading them here is free
    // and race-free; the host only ever sees the atomics. Monotone -- the
    // host takes deltas rather than resetting from another thread.
    auto& gc = v->tracingGC();
    auto& st = s->stats_;
    st.hasVM.store(1, std::memory_order_relaxed);
    st.gcStepCount.store(gc.stepCount(), std::memory_order_relaxed);
    st.gcCycles.store(gc.cyclesCompleted(), std::memory_order_relaxed);
    st.gcRtStepCount.store(gc.stepCountBySource(ts::GCStepSource::RtTick),
                           std::memory_order_relaxed);
    st.gcRtMaxNanos.store(gc.stepMaxNanosBySource(ts::GCStepSource::RtTick),
                          std::memory_order_relaxed);
}

// Attaches a VM to a Silo. The VM must be fully initialized before sending.
struct AttachVMCmd : engine::Command {
    ts::VM* vm_;

    explicit AttachVMCmd(ts::VM* vm) : vm_(vm) {}

    void doRT(engine::Silo* s) override {
        s->vm_ = vm_;
        s->heartbeatFn_ = &rtVMHeartbeat;
        // MMU: on the audio thread, guarantee the mutator a minimum share by
        // default so the collector can't take an unbounded slice of any short
        // window. 90% mutator over any 15 ms window; the per-step 200 us pause
        // bound (rtVMHeartbeat) still applies, and the allocation-ratio safety
        // valve lifts the cap if a cycle falls behind. Config is set here
        // (single-writer: this runs on the silo's own RT thread) before the
        // VM begins stepping. A host that wants different behavior can call
        // setMMUEnabled/setMMUTarget after attach.
        vm_->setMMUTarget(900, 15'000'000);
        vm_->setMMUEnabled(true);
    }
};

// Detaches a VM from a Silo.
struct DetachVMCmd : engine::Command {
    void doRT(engine::Silo* s) override {
        s->vm_ = nullptr;
        s->heartbeatFn_ = nullptr;
    }
};

// NOTE: Beat-based tempo scheduling now lives in the engine's per-silo
// TempoClock slots (engine::TempoClock, scheduled via engine::sched/setTempo/
// schedTempoChange). The former single-slot RT tempo-scheduler commands that
// drove bridge::RTTempoScheduler through Silo::rtTempoScheduler_ were removed.

} // namespace bridge

#endif /* tzpl_vm_commands_hpp */
