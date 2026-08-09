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
//  tzpl_clock_ffi.cpp
//  bridge
//
//  FFI bridge for tempo-based scheduling. Registers Tzopilotl functions
//  in the "clock" module that call into the NRTTempoScheduler.
//

#include "tzpl_clock_ffi.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_nrt_render.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl.hpp"
#include "value.hpp"

namespace bridge {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// If we are inside an NRT render context (setup closure or one of its
// scheduler handlers), route to that render's per-render tempo scheduler.
// Otherwise route to the live AppContext scheduler.
static ts::NRTTempoScheduler* getScheduler(ts::VM& vm) {
    if (auto const* r = bridge::currentRenderContext()) {
        return r->scheduler;
    }
    auto* ctx = static_cast<AppContext*>(vm.userData());
    return ctx ? ctx->tempoScheduler : nullptr;
}

// The engine whose tempo clocks back this scheduler context (render-local
// engine inside a render, else the live engine). clock.x tempo changes are
// mirrored onto this engine's clock slot 0 so that beat-scheduled bundles
// (ae.sched(0, beat)) follow the same tempo as the clock module. The
// NRTTempoScheduler still drives the lang callbacks; the engine clock is the
// authority for engine-side beat scheduling.
static engine::Engine* getClockEngine(ts::VM& vm) {
    if (auto const* r = bridge::currentRenderContext()) {
        return r->engine;
    }
    auto* ctx = static_cast<AppContext*>(vm.userData());
    return ctx ? ctx->engine : nullptr;
}

// clock.x targets engine clock slot 0 for now. Multi-slot tempo control is
// available via the audio_engine FFI (setTempo(clock, bpm) / schedTempoChange).
static constexpr int kClockSlot = 0;

// ---------------------------------------------------------------------------
// FFI functions
// ---------------------------------------------------------------------------

// fn sched(beats Float, handler Fn() Float) Int
// Schedule a handler relative to the current logical beat.
// If the handler returns a positive finite number, it is rescheduled
// that many beats later (SuperCollider Routine convention).
// Returns a timer ID.
static void ffi_sched(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).i = -1; return; }
    f64 deltaBeats = vm.reg(argBase).f;
    ts::Obj* handler = vm.reg(argBase + 1).o;
    vm.reg(dst).i = sched->sched(deltaBeats, handler);
}

// fn schedAbs(beat Float, handler Fn() Float) Int
// Schedule a handler at an absolute beat position.
static void ffi_schedAbs(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).i = -1; return; }
    f64 beat = vm.reg(argBase).f;
    ts::Obj* handler = vm.reg(argBase + 1).o;
    vm.reg(dst).i = sched->schedAbs(beat, handler);
}

// fn after(beats Float, handler Fn() Void) Int
// One-shot scheduling relative to the current logical beat.
// Same as sched() but with a Void handler (never reschedules).
static void ffi_after(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).i = -1; return; }
    f64 deltaBeats = vm.reg(argBase).f;
    ts::Obj* handler = vm.reg(argBase + 1).o;
    vm.reg(dst).i = sched->sched(deltaBeats, handler);
}

// fn at(beat Float, handler Fn() Void) Int
// One-shot scheduling at an absolute beat.
static void ffi_at(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).i = -1; return; }
    f64 beat = vm.reg(argBase).f;
    ts::Obj* handler = vm.reg(argBase + 1).o;
    vm.reg(dst).i = sched->schedAbs(beat, handler);
}

// fn delayReal(seconds Float) Future<Void>
//
// Awaitable WALL-CLOCK delay. The core `delay(beats)` builtin lives on the
// VM's virtual-beat timeline, which a top-level await fast-forwards without
// waiting; `await delayReal(1.0)` instead parks the caller (releasing the
// NRTVM mutex via the host-wait hook) until the scheduler thread resolves
// the future one real second later. The deadline is a fixed wall-clock time
// in the scheduler's separate delay queue: tempo changes cannot move it, and
// it survives clearAll() (panic clears handlers, not awaits). In a render
// context the resolution rides the render's manual clock, i.e. logical audio
// time. For musical (beat) scheduling use sched/after.
static void ffi_delayReal(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 seconds = vm.reg(argBase).f;

    auto& tu = vm.typeUniverse();
    ts::Type* voidT = tu.types().voidType;
    ts::FutureType* futT = tu.futureType(voidT);
    u16 vw = (u16)((voidT && voidT->sizeWords_ > 0) ? voidT->sizeWords_ : 1);
    auto* future = ts::Future::create(futT, voidT, vw);
    vm.registerExternalFuture(future);   // GC-root it while in flight
    vm.reg(dst).o = future;

    auto* sched = getScheduler(vm);
    if (!sched || !(seconds > 0.)) {
        // Nothing to wait for, or no scheduler to drive the resolution:
        // resolve now so a top-level await doesn't hang.
        vm.resolveExternalFuture(future);
        return;
    }
    sched->schedResolveFutureSecs(seconds, future);
}

// fn delayBeats(beats Float) Future<Void>
//
// Awaitable MUSICAL delay on the live tempo clock: the future resolves when
// the clock reaches now + beats, tracking tempo changes and ramps exactly
// like a sched() handler (the entry rides the same beat queue, and fires
// latency-early the same way, so resumed code can submit engine commands
// that land on the beat). Like sched(), a call from within a clock handler
// is relative to the handler's beat. The pending entry survives clearAll()
// (panic clears handlers, not awaits). In a render context it resolves at
// the render's logical beat. For a tempo-independent wait use delayReal.
static void ffi_delayBeats(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 beats = vm.reg(argBase).f;

    auto& tu = vm.typeUniverse();
    ts::Type* voidT = tu.types().voidType;
    ts::FutureType* futT = tu.futureType(voidT);
    u16 vw = (u16)((voidT && voidT->sizeWords_ > 0) ? voidT->sizeWords_ : 1);
    auto* future = ts::Future::create(futT, voidT, vw);
    vm.registerExternalFuture(future);   // GC-root it while in flight
    vm.reg(dst).o = future;

    auto* sched = getScheduler(vm);
    if (!sched || !(beats > 0.)) {
        // Nothing to wait for, or no scheduler to drive the resolution:
        // resolve now so a top-level await doesn't hang.
        vm.resolveExternalFuture(future);
        return;
    }
    sched->schedResolveFutureBeats(beats, future);
}

// fn cancel(timerID Int) Void
static void ffi_cancel(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) return;
    sched->cancel(vm.reg(argBase).i);
}

// fn setTempo(bpm Float) Void
static void ffi_setTempo(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 bpm = vm.reg(argBase).f;
    // Drive the NRT scheduler (lang callback timing) ...
    if (auto* sched = getScheduler(vm)) sched->setTempoBPM(bpm);
    // ... and the authoritative engine clock (engine-side beat scheduling).
    if (auto* e = getClockEngine(vm)) engine::setTempo(e, kClockSlot, bpm);
}

// fn getTempo() Float
static void ffi_getTempo(ts::VM& vm, u16 dst, u16, u16) {
    auto* sched = getScheduler(vm);
    vm.reg(dst).f = sched ? sched->tempoBPM() : 0.;
}

// fn getBeats() Float
static void ffi_getBeats(ts::VM& vm, u16 dst, u16, u16) {
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).f = 0.; return; }
    // If inside a handler callback, return the logical beat.
    // Otherwise, return the current wall-clock beat.
    f64 lb = sched->logicalBeat();
    vm.reg(dst).f = (lb >= 0.) ? lb : sched->beats();
}

// fn getBeatDur() Float
// Duration of one beat in seconds at the current tempo.
static void ffi_getBeatDur(ts::VM& vm, u16 dst, u16, u16) {
    auto* sched = getScheduler(vm);
    vm.reg(dst).f = sched ? sched->beatDur() : 0.;
}

// fn schedTempoChange(beat Float, targetBPM Float, rampBeats Float) Int
// Schedule a tempo ramp at a specific beat.
static void ffi_schedTempoChange(ts::VM& vm, u16 dst, u16, u16 argBase) {
    f64 beat = vm.reg(argBase).f;
    f64 targetBPM = vm.reg(argBase + 1).f;
    f64 rampBeats = vm.reg(argBase + 2).f;
    i64 id = -1;
    // Drive the NRT scheduler (lang callback timing) ...
    if (auto* sched = getScheduler(vm)) {
        id = sched->schedTempoChangeBPM(beat, targetBPM, rampBeats);
    }
    // ... and the authoritative engine clock (engine-side beat scheduling).
    if (auto* e = getClockEngine(vm)) {
        engine::schedTempoChange(e, kClockSlot, beat, targetBPM, rampBeats);
    }
    vm.reg(dst).i = id;
}

// fn setLatency(seconds Float) Void
static void ffi_setLatency(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) return;
    sched->setLatency(vm.reg(argBase).f);
}

// fn getLatency() Float
static void ffi_getLatency(ts::VM& vm, u16 dst, u16, u16) {
    auto* sched = getScheduler(vm);
    vm.reg(dst).f = sched ? sched->latency() : 0.;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void registerClockFFI(ts::Compiler& compiler) {
    auto* Void  = compiler.voidType();
    auto* Int   = compiler.intType();
    auto* Float = compiler.floatType();

    // Function types for handlers.
    // Vec<Type*> with null allocator uses malloc (compile-thread path).
    ts::Vec<ts::Type*> noArgs;
    ts::Type* FnFloat = reinterpret_cast<ts::Type*>(
        compiler.functionType(noArgs, Float));
    ts::Type* FnVoid = reinterpret_cast<ts::Type*>(
        compiler.functionType(noArgs, Void));

    using R = void (*)(ts::VM&, u16, u16, u16);

    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("clock_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    // Scheduling (reschedulable -- handler returns Float)
    reg("sched",    Int, {Float, FnFloat},  ffi_sched);
    reg("schedAbs", Int, {Float, FnFloat},  ffi_schedAbs);

    // Scheduling (one-shot -- handler returns Void)
    reg("after",    Int, {Float, FnVoid},   ffi_after);
    reg("at",       Int, {Float, FnVoid},   ffi_at);

    // Awaitable delays: wall-clock seconds and tempo-clock beats
    ts::Type* FutureVoid = reinterpret_cast<ts::Type*>(compiler.futureType(Void));
    reg("delayReal",  FutureVoid, {Float},  ffi_delayReal);
    reg("delayBeats", FutureVoid, {Float},  ffi_delayBeats);

    // Cancel
    reg("cancel",   Void, {Int},            ffi_cancel);

    // Tempo control (BPM)
    reg("setTempo", Void, {Float},          ffi_setTempo);
    reg("getTempo", Float, {},              ffi_getTempo);

    // Beat queries
    reg("getBeats",   Float, {},            ffi_getBeats);
    reg("getBeatDur", Float, {},            ffi_getBeatDur);

    // Tempo ramp scheduling
    reg("schedTempoChange", Int, {Float, Float, Float}, ffi_schedTempoChange);

    // Latency
    reg("setLatency", Void, {Float},        ffi_setLatency);
    reg("getLatency", Float, {},            ffi_getLatency);
}

} // namespace bridge
