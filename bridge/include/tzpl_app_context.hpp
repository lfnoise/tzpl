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

#include <memory>
#include <string>
#include <vector>

namespace engine { struct Engine; }
namespace ts {
    class VM;
    struct NRTVM;
    struct VMTargetData;
    using VMTarget = std::shared_ptr<VMTargetData>;
    class Compiler;
    class ModuleCompiler;
    class NRTTempoScheduler;
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

// Per-silo RT VM state. Created by attachVM(), destroyed by detachVM().
struct SiloVMState {
    ts::VM* vm = nullptr;
    ts::VMTarget target;
    std::unique_ptr<ts::ModuleCompiler> moduleCompiler;
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
};

} // namespace bridge

#endif /* tzpl_app_context_hpp */
