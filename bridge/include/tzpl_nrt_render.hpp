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
//  tzpl_nrt_render.hpp
//  bridge
//
//  Asynchronous non-real-time (offline) audio rendering. Each render owns
//  its own engine, tempo scheduler, and background thread; the live engine
//  (if any) and other renders are unaffected. Multiple renders can run
//  concurrently. The script never blocks waiting for a render to finish --
//  use isRenderDone(h) to poll, or onRenderDone(h, cb) for a callback.
//

#ifndef tzpl_nrt_render_hpp
#define tzpl_nrt_render_hpp

#include <cstdint>
#include <functional>
#include <string>

namespace ts {
    class Compiler;
    class NRTTempoScheduler;
    class Obj;
}

namespace engine { struct Engine; }

namespace bridge {

struct AppContext;

// Options for an NRT render. All fields have sensible defaults.
struct RenderJobOpts {
    std::string path;             // output WAV path
    double sampleRate = 44100.0;
    int    channels = 2;
    int    bufferFrames = 512;
    // Hard cap. If <= 0, the render is open-ended; it stops when the script
    // calls endRender()/stopRender() or the tempo scheduler goes idle.
    double durationSeconds = 0.0;
    // Tail rendered after the stop signal so reverb/decay tails are not truncated.
    double tailSeconds = 1.0;
    // Open-ended mode safety cap. Render aborts if it elapses with no other
    // stop signal (script bug, runaway loop). Stderr warning is emitted.
    double safetyCapSeconds = 3600.0;
    // Number of parallel silos (worker threads). Same as the live engine
    // default; lower for tiny renders where thread setup overhead dominates.
    int    numSilos = 4;
};

// Per-thread context describing the render currently being driven on this
// thread. FFI functions consult this so commands route to the render's
// engine/scheduler instead of the live engine. nullptr when no render is
// active on this thread.
struct RenderContext {
    engine::Engine*         engine = nullptr;
    ts::NRTTempoScheduler*  scheduler = nullptr;
    int64_t                 handle = 0;
};

// Returns the render context for the calling thread, or nullptr if none.
RenderContext const* currentRenderContext();

// RAII helper: installs a render context on the calling thread and restores
// the previous one on destruction. Use around any code that should run "in
// the render's context" (the setup closure, scheduler callbacks, etc.).
class ScopedRenderContext {
public:
    explicit ScopedRenderContext(RenderContext const* ctx);
    ~ScopedRenderContext();
    ScopedRenderContext(ScopedRenderContext const&) = delete;
    ScopedRenderContext& operator=(ScopedRenderContext const&) = delete;
private:
    RenderContext const* prev_;
};

// Start an NRT render. Returns a handle for status / control. The setup
// callback runs synchronously on the calling thread under the VM lock with
// the render's context installed. Once it returns, a background thread
// drives the render until it completes.
//
// Lifetime: the RenderJob is owned by an internal registry and freed only
// when the render is done AND no callbacks are pending. Currently the
// registry never garbage-collects completed jobs (acceptable for typical
// usage since renders are infrequent); a future revision may add explicit
// disposal.
int64_t renderNRTAsync(RenderJobOpts const& opts,
                       AppContext* appCtx,
                       std::function<void()> setup);

// Non-blocking status query. Invalid handles return true (treated as
// "completed long ago").
bool isRenderDone(int64_t handle);

// Register a one-shot completion callback. If the render is already done,
// the callback runs immediately (synchronously, on the calling thread).
// Otherwise it is fired on the live engine's NRT tempo scheduler thread
// after the render thread terminates. Multiple callbacks can be registered
// (FIFO order).
void onRenderDone(int64_t handle, std::function<void()> callback);

// Ask the render to stop after the current block; render the renderer's
// default tail (or `tailSeconds` if positive) before closing the WAV. The
// first stop request wins; subsequent calls are ignored.
void stopRender(int64_t handle);
void stopRender(int64_t handle, double tailSeconds);

// Within a render's setup closure (or a scheduler handler running in this
// render's context), ask THIS render to stop. No-op outside any render
// context.
void requestEndCurrentRender();
void requestEndCurrentRender(double tailSeconds);

// Register the renderNRT / isRenderDone / onRenderDone / stopRender /
// endRender FFI functions in the audio_engine module. Must be called after
// registerAudioEngineFFI.
void registerNRTRenderFFI(ts::Compiler& compiler);

// Block the calling C++ thread until every render in the registry has
// finished. Polls at the supplied interval. Intended for host shutdown
// (e.g. the CLI's --nrt flow waits with this before exit). Not exposed to
// Tzopilotl -- the language never blocks on a render.
void joinAllRenders(int pollMs = 50);

// Tear down the render registry: joins all render threads (after waiting
// for them to finish naturally), then clears the job table. Must be called
// before the live NRTVM and AppContext are destroyed, since each render
// captures pointers into them. Subsequent renderNRTAsync calls fail (return
// 0). Safe to call multiple times.
void shutdownRenderRegistry();

} // namespace bridge

#endif /* tzpl_nrt_render_hpp */
