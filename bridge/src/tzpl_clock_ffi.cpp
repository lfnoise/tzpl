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

// fn cancel(timerID Int) Void
static void ffi_cancel(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) return;
    sched->cancel(vm.reg(argBase).i);
}

// fn setTempo(bpm Float) Void
static void ffi_setTempo(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* sched = getScheduler(vm);
    if (!sched) return;
    sched->setTempoBPM(vm.reg(argBase).f);
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
    auto* sched = getScheduler(vm);
    if (!sched) { vm.reg(dst).i = -1; return; }
    f64 beat = vm.reg(argBase).f;
    f64 targetBPM = vm.reg(argBase + 1).f;
    f64 rampBeats = vm.reg(argBase + 2).f;
    vm.reg(dst).i = sched->schedTempoChangeBPM(beat, targetBPM, rampBeats);
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
