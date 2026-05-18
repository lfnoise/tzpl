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
//  tzpl_nrt_render.cpp
//  bridge
//

#include "tzpl_nrt_render.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_audio_engine_ffi.hpp"
#include "tzpl_audio_file_writer.hpp"
#include "tzpl_client_interface.hpp"
#include "tzpl_engine.hpp"
#include "tzpl.hpp"
#include "value.hpp"
#include "nrt_vm.hpp"
#include "nrt_tempo_scheduler.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bridge {

// ---------------------------------------------------------------------------
// Thread-local current-render context
// ---------------------------------------------------------------------------

static thread_local RenderContext const* tCurrentRender = nullptr;

RenderContext const* currentRenderContext() { return tCurrentRender; }

ScopedRenderContext::ScopedRenderContext(RenderContext const* ctx)
    : prev_(tCurrentRender) { tCurrentRender = ctx; }

ScopedRenderContext::~ScopedRenderContext() { tCurrentRender = prev_; }

// ---------------------------------------------------------------------------
// RenderJob
// ---------------------------------------------------------------------------

struct RenderJob {
    int64_t                                  handle = 0;
    RenderJobOpts                            opts;
    AppContext*                              appCtx = nullptr;
    std::unique_ptr<engine::Engine>          eng;
    std::unique_ptr<ts::NRTTempoScheduler>   sched;
    RenderContext                            ctx;          // engine + sched + handle
    std::function<void()>                    setup;        // run on render thread before loop
    std::atomic<bool>                        done{false};
    std::atomic<bool>                        stopRequested{false};
    std::atomic<double>                      tailOverride{0.0};
    std::mutex                               cbMtx;
    std::vector<std::function<void()>>       onDoneCallbacks;
    // The std::thread itself is stored in a separate global vector so that
    // its move-assignment can't race with the thread already running and
    // touching RenderJob fields.
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

struct RenderRegistry {
    std::mutex mtx;
    std::unordered_map<int64_t, std::unique_ptr<RenderJob>> jobs;
    std::vector<std::thread>                                threads;
    int64_t nextHandle = 1;
    bool    shutdown = false;
};

static RenderRegistry& registry() {
    static RenderRegistry r;
    return r;
}

static RenderJob* lookup(int64_t handle) {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mtx);
    auto it = r.jobs.find(handle);
    return it == r.jobs.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------------------
// Render loop (runs on the job's background thread)
// ---------------------------------------------------------------------------

static void runRenderThread(RenderJob* job) {
    ScopedRenderContext scope(&job->ctx);

    // Run the setup callback on this render thread so we can call into the
    // VM via callCallable without clobbering whatever the script's own VM
    // execution is doing on the main thread. callCallable resets the VM's
    // frame state, so it cannot be invoked from inside another running VM
    // execution -- but it is fine to run it on a thread that has nothing
    // else going on, under the NRTVM mutex.
    if (job->setup) {
        job->setup();
        job->setup = nullptr; // release the std::function (and any retained Tzpl handler)
    }

    int sampleRate   = int(job->eng->streamParams_.sampleRate);
    int channels     = job->eng->streamParams_.channels;
    int bufferFrames = job->eng->streamParams_.bufferFrames;

    auto* writer = engine::wavOpen(job->opts.path.c_str(), sampleRate, channels);
    if (!writer) {
        // Mark done so isRenderDone returns true and onDone callbacks can fire.
        std::vector<std::function<void()>> cbs;
        {
            std::lock_guard<std::mutex> lk(job->cbMtx);
            cbs = std::move(job->onDoneCallbacks);
        }
        job->done.store(true);
        for (auto& cb : cbs) cb();
        return;
    }

    bool hasFixedDuration = job->opts.durationSeconds > 0.0;
    int64_t durationFrames = hasFixedDuration
        ? int64_t(job->opts.durationSeconds * sampleRate + 0.5) : 0;
    int64_t safetyCapFrames = int64_t(job->opts.safetyCapSeconds * sampleRate + 0.5);
    int64_t maxFrames = hasFixedDuration ? durationFrames : safetyCapFrames;
    int64_t defaultTailFrames = int64_t(job->opts.tailSeconds * sampleRate + 0.5);

    std::vector<engine::f32> blockBuf(size_t(bufferFrames) * size_t(channels));

    int64_t framesRendered = 0;
    int64_t tailStartedAt = -1;
    int64_t tailFrames = defaultTailFrames;

    while (framesRendered < maxFrames) {
        // Tick the per-render tempo scheduler to current logical time so
        // handlers fire BEFORE this block runs and can queue commands that
        // the silos will pick up via processRTCommands.
        double logicalSeconds = double(framesRendered) / sampleRate;
        job->sched->tickTo(logicalSeconds);

        engine::renderNRTBlock(job->eng.get(), blockBuf.data());
        int64_t remaining = maxFrames - framesRendered;
        int framesThisBlock = int(std::min<int64_t>(bufferFrames, remaining));
        engine::wavWrite(writer, blockBuf.data(), framesThisBlock);
        framesRendered += framesThisBlock;

        if (tailStartedAt < 0) {
            bool stopReq = job->stopRequested.load();
            bool autoStop = !hasFixedDuration && job->sched->isIdle();
            if (stopReq || autoStop) {
                double explicitTail = job->tailOverride.load();
                if (explicitTail > 0.0) {
                    tailFrames = int64_t(explicitTail * sampleRate + 0.5);
                }
                tailStartedAt = framesRendered;
            }
        }

        if (tailStartedAt >= 0 && (framesRendered - tailStartedAt) >= tailFrames) {
            break;
        }
    }

    if (!hasFixedDuration && tailStartedAt < 0) {
        std::cerr << "renderNRT[" << job->handle
                  << "]: hit safety cap of " << job->opts.safetyCapSeconds
                  << "s without a stop signal -- script never called endRender()/"
                     "stopRender(), and tempo scheduler never went idle.\n";
    }

    engine::wavClose(writer);

    std::vector<std::function<void()>> cbs;
    {
        // Take the callback list AND set done=true atomically under the
        // cbMtx so that any onRenderDone call racing with completion either
        // sees done=false (and gets queued, fired by the loop below since
        // we'll move our cbs after they're added) or done=true (and fires
        // inline). The order matters: if we set done=true outside the
        // lock, a caller could observe done=false, queue a callback, and
        // be missed.
        std::lock_guard<std::mutex> lk(job->cbMtx);
        cbs = std::move(job->onDoneCallbacks);
        job->done.store(true);
    }
    for (auto& cb : cbs) cb();
}

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

int64_t renderNRTAsync(RenderJobOpts const& opts,
                       AppContext* appCtx,
                       std::function<void()> setup) {
    if (opts.path.empty()) {
        std::cerr << "renderNRT: empty output path\n";
        return 0;
    }

    auto job = std::make_unique<RenderJob>();
    job->opts = opts;
    job->appCtx = appCtx;

    // Build a dedicated NRT engine for this render.
    engine::AudioStreamParameters asp{};
    asp.deviceName  = "";  // unused in NRT
    asp.channels    = opts.channels;
    asp.bufferFrames = opts.bufferFrames;
    asp.sampleRate  = opts.sampleRate;
    asp.firstChannel = 0;

    engine::EngineConfig cfg;
    cfg.numSilos = opts.numSilos;
    job->eng.reset(engine::newEngineNRT(cfg, asp));

    // Register the same built-in node defs / loaded plugins on the per-render
    // engine that the live engine has, so scripts using "sinosc", "AddOp",
    // user synthdefs, etc. find them. The app sets this hook in main.cpp.
    // Register built-in node defs and project plugins on the per-render engine.
    if (appCtx && appCtx->initEngine) {
        appCtx->initEngine(job->eng.get());
    }
    // Copy any runtime-compiled synthdefs (from defSynth / compileSynthDefAndLoad)
    // that were loaded on the live engine. This way render setups can
    // ae.newNode("bubbles", ...) etc. using defs compiled during the session.
    if (appCtx && appCtx->engine) {
        engine::copyNodeDefs(appCtx->engine, job->eng.get());
    }

    // Per-render tempo scheduler in manual mode -- the render thread drives
    // it via tickTo, so beat-timed handlers fire on logical audio time.
    auto* nrtvm = appCtx ? appCtx->nrtvm : nullptr;
    job->sched = std::make_unique<ts::NRTTempoScheduler>(nrtvm);
    job->sched->setManualMode(true);
    job->sched->setLatency(0.0);
    job->sched->start();

    // Register before running setup so isRenderDone(h) and friends work
    // immediately.
    int64_t handle;
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lk(r.mtx);
        if (r.shutdown) {
            std::cerr << "renderNRT: registry has been shut down\n";
            return 0;
        }
        handle = r.nextHandle++;
        job->handle = handle;
        job->ctx.engine = job->eng.get();
        job->ctx.scheduler = job->sched.get();
        job->ctx.handle = handle;
        r.jobs[handle] = std::move(job);
    }

    RenderJob* jobPtr = lookup(handle);

    // Stash the setup closure to run on the render thread (NOT here on the
    // calling thread). callCallable resets VM state, so it can't be invoked
    // synchronously from inside another running VM execution.
    jobPtr->setup = std::move(setup);

    // Spawn the render thread and stash its std::thread in the registry's
    // separate threads vector. We avoid storing the thread inside RenderJob
    // because the std::thread move-assignment would race with the running
    // thread reading RenderJob fields.
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lk(r.mtx);
        r.threads.emplace_back(runRenderThread, jobPtr);
    }

    return handle;
}

bool isRenderDone(int64_t handle) {
    auto* job = lookup(handle);
    if (!job) return true;          // unknown / disposed -> treat as done
    return job->done.load();
}

void onRenderDone(int64_t handle, std::function<void()> callback) {
    if (!callback) return;
    auto* job = lookup(handle);
    if (!job) { callback(); return; }
    {
        std::lock_guard<std::mutex> lk(job->cbMtx);
        if (!job->done.load()) {
            // Render still in progress -- queue for the completion sweep.
            job->onDoneCallbacks.emplace_back(std::move(callback));
            return;
        }
    }
    // Already done; fire inline (without holding the lock).
    callback();
}

void stopRender(int64_t handle) {
    auto* job = lookup(handle);
    if (!job) {
        std::cerr << "stopRender: unknown render handle " << handle << "\n";
        return;
    }
    job->stopRequested.store(true);
}

void stopRender(int64_t handle, double tailSeconds) {
    auto* job = lookup(handle);
    if (!job) {
        std::cerr << "stopRender: unknown render handle " << handle << "\n";
        return;
    }
    if (tailSeconds > 0.0) job->tailOverride.store(tailSeconds);
    job->stopRequested.store(true);
}

void requestEndCurrentRender() {
    auto const* ctx = currentRenderContext();
    if (!ctx) {
        std::cerr << "endRender: no active render context on this thread\n";
        return;
    }
    stopRender(ctx->handle);
}

void requestEndCurrentRender(double tailSeconds) {
    auto const* ctx = currentRenderContext();
    if (!ctx) {
        std::cerr << "endRender: no active render context on this thread\n";
        return;
    }
    stopRender(ctx->handle, tailSeconds);
}

void joinAllRenders(int pollMs) {
    if (pollMs < 1) pollMs = 1;
    auto step = std::chrono::milliseconds(pollMs);
    while (true) {
        bool anyAlive = false;
        {
            auto& r = registry();
            std::lock_guard<std::mutex> lk(r.mtx);
            for (auto& kv : r.jobs) {
                if (!kv.second->done.load()) { anyAlive = true; break; }
            }
        }
        if (!anyAlive) return;
        std::this_thread::sleep_for(step);
    }
}

void shutdownRenderRegistry() {
    // Move both threads and jobs out of the registry under the lock, then
    // join+destroy them outside the lock so threads' callbacks can re-enter
    // registry lookups without deadlocking.
    std::vector<std::thread>                threads;
    std::vector<std::unique_ptr<RenderJob>> jobs;
    {
        auto& r = registry();
        std::lock_guard<std::mutex> lk(r.mtx);
        if (r.shutdown) return;
        r.shutdown = true;
        threads = std::move(r.threads);
        r.threads.clear();
        jobs.reserve(r.jobs.size());
        for (auto& kv : r.jobs) jobs.push_back(std::move(kv.second));
        r.jobs.clear();
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    // jobs destructed here, after their threads have joined
}

// ---------------------------------------------------------------------------
// FFI glue
// ---------------------------------------------------------------------------

// All of the following functions go into the audio_engine module. They
// follow the standard CFun signature: (vm, dst, src, argBase).

namespace {

// Wrap a Tzopilotl Fn() Void as a std::function for an ASYNC invocation
// (e.g. an onRenderDone callback fired from the render thread). The wrapper
// takes the NRTVM mutex before re-entering the VM, since the calling thread
// is not the one already executing the script.
static std::function<void()> makeAsyncCallable(ts::VM& vm, ts::Obj* handler) {
    auto* ctx = static_cast<AppContext*>(vm.userData());
    if (!ctx || !ctx->nrtvm || !handler) return {};
    auto* nrtvm = ctx->nrtvm;
    return [nrtvm, handler]() {
        std::lock_guard<std::mutex> lk(nrtvm->mtx);
        nrtvm->vm.makeCurrent();
        nrtvm->vm.callCallable(handler, nullptr, 0);
        nrtvm->vm.gcHeartbeat();
    };
}

// Wrap a Tzopilotl Fn() Void as a std::function for SYNCHRONOUS invocation
// from inside another FFI call. The script's main thread is already
// executing the VM, so re-entering via callCallable is just a nested call.
// Acquiring the NRTVM mutex here would collide with whichever thread holds
// it (or, more often, with the same thread that is mid-execution),
// triggering a deadlock or an "already locked" abort from std::mutex.
static std::function<void()> makeSyncCallable(ts::VM& vm, ts::Obj* handler) {
    if (!handler) return {};
    ts::VM* vmPtr = &vm;
    return [vmPtr, handler]() {
        vmPtr->callCallable(handler, nullptr, 0);
    };
}

static const char* regString(ts::VM& vm, u16 reg) {
    return ts::stringData(vm.reg(reg).o);
}

// fn renderNRT(path String, setup Fn() Void) Int  -- open-ended
static void ffi_renderNRT_open(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = static_cast<AppContext*>(vm.userData());
    RenderJobOpts opts;
    opts.path = regString(vm, argBase);
    // Setup runs on the render thread under the NRTVM lock (async wrapper)
    // so it doesn't clobber the caller's running VM state.
    auto setup = makeAsyncCallable(vm, vm.reg(argBase + 1).o);
    int64_t h = renderNRTAsync(opts, ctx, std::move(setup));
    vm.reg(dst).i = static_cast<i64>(h);
}

// fn renderNRT(path String, durationSeconds Float, setup Fn() Void) Int
static void ffi_renderNRT_dur(ts::VM& vm, u16 dst, u16, u16 argBase) {
    auto* ctx = static_cast<AppContext*>(vm.userData());
    RenderJobOpts opts;
    opts.path = regString(vm, argBase);
    opts.durationSeconds = vm.reg(argBase + 1).f;
    auto setup = makeAsyncCallable(vm, vm.reg(argBase + 2).o);
    int64_t h = renderNRTAsync(opts, ctx, std::move(setup));
    vm.reg(dst).i = static_cast<i64>(h);
}

// fn isRenderDone(h Int) Bool
static void ffi_isRenderDone(ts::VM& vm, u16 dst, u16, u16 argBase) {
    int64_t h = vm.reg(argBase).i;
    vm.reg(dst).i = isRenderDone(h) ? 1 : 0;
}

// fn onRenderDone(h Int, callback Fn() Void) Void
static void ffi_onRenderDone(ts::VM& vm, u16, u16, u16 argBase) {
    int64_t h = vm.reg(argBase).i;
    auto cb = makeAsyncCallable(vm, vm.reg(argBase + 1).o);
    onRenderDone(h, std::move(cb));
}

// fn stopRender(h Int) Void
static void ffi_stopRender(ts::VM& vm, u16, u16, u16 argBase) {
    stopRender(vm.reg(argBase).i);
}

// fn stopRender(h Int, tailSeconds Float) Void
static void ffi_stopRender_tail(ts::VM& vm, u16, u16, u16 argBase) {
    stopRender(vm.reg(argBase).i, vm.reg(argBase + 1).f);
}

// fn endRender() Void  -- targets the current render context
static void ffi_endRender(ts::VM&, u16, u16, u16) {
    requestEndCurrentRender();
}

// fn endRender(tailSeconds Float) Void
static void ffi_endRender_tail(ts::VM& vm, u16, u16, u16 argBase) {
    requestEndCurrentRender(vm.reg(argBase).f);
}

} // namespace

void registerNRTRenderFFI(ts::Compiler& compiler) {
    auto* Void   = compiler.voidType();
    auto* Int    = compiler.intType();
    auto* Float  = compiler.floatType();
    auto* Bool   = compiler.boolType();
    auto* String = compiler.stringType();

    // Fn() Void handler type, used for both setup closures and callbacks.
    ts::Vec<ts::Type*> noArgs;
    ts::Type* FnVoid = reinterpret_cast<ts::Type*>(
        compiler.functionType(noArgs, Void));

    using R = void (*)(ts::VM&, u16, u16, u16);
    auto reg = [&](const char* name, ts::Type* retType,
                   std::vector<ts::Type*> params, R fn) {
        compiler.registerForeignModuleFunction("audio_engine_ffi", name, retType,
                                               std::move(params), fn,
                                               /*pure=*/false, /*rtSafe=*/false);
    };

    reg("renderNRT",    Int,  {String, FnVoid},          ffi_renderNRT_open);
    reg("renderNRT",    Int,  {String, Float, FnVoid},   ffi_renderNRT_dur);
    reg("isRenderDone", Bool, {Int},                     ffi_isRenderDone);
    reg("onRenderDone", Void, {Int, FnVoid},             ffi_onRenderDone);
    reg("stopRender",   Void, {Int},                     ffi_stopRender);
    reg("stopRender",   Void, {Int, Float},              ffi_stopRender_tail);
    reg("endRender",    Void, {},                        ffi_endRender);
    reg("endRender",    Void, {Float},                   ffi_endRender_tail);
}

} // namespace bridge
