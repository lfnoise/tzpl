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
//  tzpl_app_context.hpp
//  bridge
//
//  Application context: holds pointers to all subsystems accessible
//  via the VM's userData pointer. Extensible for OSC, NATS, etc.
//

#ifndef tzpl_app_context_hpp
#define tzpl_app_context_hpp

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Complete type needed: SiloVMState holds a unique_ptr<IncrementalCompiler>, so
// every TU that destroys an AppContext must see its (pimpl, lightweight) decl.
#include "incremental_compiler.hpp"
// Lightweight (no vm.hpp): per-VM construction config held by value below.
#include "vm_config.hpp"

namespace engine { struct Engine; }
namespace ts {
    class VM;
    struct NRTVM;
    struct VMTargetData;
    using VMTarget = std::shared_ptr<VMTargetData>;
    class Compiler;
    class ModuleCompiler;
    class NRTTempoScheduler;
    class Symbol;
    using SymbolPtr = const Symbol*;
}

namespace osc {
class OscServer;
class OscClient;
class OscDispatcher;
}

namespace nats {
class NatsClient;
class NatsDispatcher;
}

namespace bridge {

struct UIState;

// Per-silo RT VM state. Created by attachVM(), destroyed by detachVM().
struct SiloVMState {
    ts::VM* vm = nullptr;
    ts::VMTarget target;
    std::unique_ptr<ts::ModuleCompiler> moduleCompiler;
    // Persistent incremental compile context for this silo. Keeps one TypeChecker
    // across siloLoad/siloEval calls so redefining a function reuses its global
    // index and live silo code picks up the new body. Created by attachVM.
    std::unique_ptr<ts::IncrementalCompiler> incCompiler;
    // Global index of the most recently siloLoad'ed module's start() entry
    // (Void, no params), or -1 if none. siloStartAt schedules a call to it.
    int startGlobalIndex = -1;
    // Global index of the cross-VM actor delivery trampoline
    // (tzpl_actor_deliver(Symbol, Bytes) -> sendByName(name, decode(bytes))),
    // injected as a preamble by siloLoad. -1 if absent.
    int deliverGlobalIndex = -1;
    // Owns the per-silo task scheduler (bridge::SiloTaskScheduler). Opaque here
    // to keep this header free of the bridge .cpp's internals; deleted on detach.
    void* taskSched = nullptr;
};

struct AppContext {
    engine::Engine* engine = nullptr;
    ts::NRTVM* nrtvm = nullptr;
    ts::Compiler* compiler = nullptr;
    ts::ModuleCompiler* moduleCompiler = nullptr;
    ts::VMTarget target;
    ts::NRTTempoScheduler* tempoScheduler = nullptr;
    osc::OscServer* oscServer = nullptr;
    osc::OscClient* oscClient = nullptr;
    osc::OscDispatcher* oscDispatcher = nullptr;
    nats::NatsClient* natsClient = nullptr;
    nats::NatsDispatcher* natsDispatcher = nullptr;
    std::string engineName;  // empty = single-instance mode
    std::vector<SiloVMState> siloVMs;  // indexed by silo number

    // Construction config for per-silo RT VMs, applied by attachVM. The app
    // populates this from its language settings at startup; the defaults here
    // are the RT preset (16 MB pool, MMU governor on, tight GC step budget is
    // still driven by rtVMHeartbeat's deadline).
    ts::VMConfig siloVMConfig{.poolSize = 16 * 1024 * 1024, .mmuEnabled = true};

    // Project directory resolved at startup (-P or auto-detected from the
    // opened file); empty if none. Used by the GUI for dialog defaults.
    std::string projectDir;

    // Directories the plugin browser scans for loadable .dylib plugins:
    // the project's synthdefs/dylib plus the synthdef compile cache. Earlier
    // entries shadow later ones for same-named plugins. Set once at startup.
    std::vector<std::string> pluginSearchPaths;

    // Document to open in the GUI once the window is up (absolute path):
    // a .tzd startup argument (notebooks can't be evaluated as source), or
    // a .x argument that was evaluated and should also be visible in a tab.
    std::string startupDocument;

    // Live-control widget registry for the `ui` module (owned by the app;
    // null when no GUI is running).
    UIState* uiState = nullptr;

    // nodeID -> def name for nodes created on the live engine, recorded by
    // audio_engine.newNode so ui.control(node, name) can look up the def's
    // control specs. Guarded by nodeDefNamesMtx (written on eval/scheduler
    // threads, read by the ui FFI).
    std::mutex nodeDefNamesMtx;
    std::unordered_map<std::int64_t, std::string> nodeDefNames;

    // (nodeID, bufID) -> audio file path, recorded by audio_engine.loadBuffer
    // on the live engine so ui.waveform can re-read the file for display.
    // Guarded by bufferPathsMtx.
    std::mutex bufferPathsMtx;
    std::map<std::pair<std::int64_t, std::int64_t>, std::string> bufferPaths;

    // Silo->NRT actor messages: a silo actor targeting an NRT actor (outbox
    // targetSilo < 0) has its encoded Msg stashed here by pumpSiloOutboxes on
    // the main thread. The lang actor server drains it (nrtActorMsgCount /
    // nrtActorMsgName / nrtActorMsgTake) and does decode + sendByName itself --
    // decode/sendByName must run as lang on the main VM, which the C++ pump
    // cannot do without clobbering the running frame. Main-thread only (no lock).
    struct NrtActorMsg {
        ts::SymbolPtr            name = nullptr;
        std::vector<std::uint8_t> bytes;
    };
    std::deque<NrtActorMsg> nrtActorInbox;

    // Optional initializer invoked on every freshly created Engine. The app
    // sets this to register built-in node defs (sinosc etc.) and to load any
    // standard synthdef plugins so per-render NRT engines have the same
    // definitions available as the live engine.
    std::function<void(engine::Engine*)> initEngine;
};

} // namespace bridge

#endif /* tzpl_app_context_hpp */
