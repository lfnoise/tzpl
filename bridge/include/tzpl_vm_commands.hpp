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
#include "vm.hpp"
#include "value.hpp"
#include "tzpl_rt_tempo_scheduler.hpp"

namespace bridge {

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
        if (callable_) callable_->retain();
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
                callable_->release();
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

// Attaches a VM to a Silo. The VM must be fully initialized before sending.
struct AttachVMCmd : engine::Command {
    ts::VM* vm_;

    explicit AttachVMCmd(ts::VM* vm) : vm_(vm) {}

    void doRT(engine::Silo* s) override {
        s->vm_ = vm_;
    }
};

// Detaches a VM from a Silo.
struct DetachVMCmd : engine::Command {
    void doRT(engine::Silo* s) override {
        s->vm_ = nullptr;
    }
};

// Attaches an RT tempo scheduler to a Silo.
struct AttachRTTempoSchedulerCmd : engine::Command {
    RTTempoScheduler* scheduler_;

    explicit AttachRTTempoSchedulerCmd(RTTempoScheduler* sched)
        : scheduler_(sched) {}

    void doRT(engine::Silo* s) override {
        s->rtTempoScheduler_ = scheduler_;
        s->tempoSchedFn_ = &RTTempoScheduler::processSampleCallback;
    }
};

// Detaches the RT tempo scheduler from a Silo.
struct DetachRTTempoSchedulerCmd : engine::Command {
    void doRT(engine::Silo* s) override {
        s->rtTempoScheduler_ = nullptr;
        s->tempoSchedFn_ = nullptr;
    }
};

// Schedules a beat-timed event on the Silo's RT tempo scheduler.
// The handler Obj* is retained by this command and transferred to the scheduler.
struct RTTempoSchedCmd : engine::Command {
    f64 beatTime_;
    ts::Obj* handler_;

    RTTempoSchedCmd(f64 beatTime, ts::Obj* handler)
        : beatTime_(beatTime), handler_(handler)
    {
        if (handler_) handler_->retain();
    }

    void doRT(engine::Silo* s) override {
        auto* sched = static_cast<RTTempoScheduler*>(s->rtTempoScheduler_);
        if (sched && handler_) {
            sched->addEntry(beatTime_, handler_);
            // Scheduler took ownership (retained again), release our ref
            handler_->release();
            handler_ = nullptr;
        }
    }

    bool doNRT(engine::Silo* s) override {
        // If doRT didn't fire (no scheduler), release on NRT
        if (handler_) {
            handler_->release();
            handler_ = nullptr;
        }
        return true;
    }
};

// Schedules a tempo change on the Silo's RT tempo scheduler.
struct RTTempoChangeCmd : engine::Command {
    f64 beatTime_;
    f64 targetTempoBPS_;
    f64 rampBeats_;

    RTTempoChangeCmd(f64 beatTime, f64 targetTempoBPS, f64 rampBeats)
        : beatTime_(beatTime), targetTempoBPS_(targetTempoBPS)
        , rampBeats_(rampBeats) {}

    void doRT(engine::Silo* s) override {
        auto* sched = static_cast<RTTempoScheduler*>(s->rtTempoScheduler_);
        if (sched) {
            sched->addTempoChange(beatTime_, targetTempoBPS_, rampBeats_);
        }
    }
};

// Sets the RT scheduler's tempo immediately.
struct RTSetTempoCmd : engine::Command {
    f64 tempoBPS_;

    explicit RTSetTempoCmd(f64 tempoBPS) : tempoBPS_(tempoBPS) {}

    void doRT(engine::Silo* s) override {
        auto* sched = static_cast<RTTempoScheduler*>(s->rtTempoScheduler_);
        if (sched) {
            sched->setTempo(tempoBPS_, s->sampleTime_);
        }
    }
};

} // namespace bridge

#endif /* tzpl_vm_commands_hpp */
