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

namespace engine { struct Engine; }

namespace osc {
class OscServer;
class OscClient;
class OscDispatcher;
}

namespace bridge {

struct AppContext {
    engine::Engine* engine = nullptr;
    osc::OscServer* oscServer = nullptr;
    osc::OscClient* oscClient = nullptr;
    osc::OscDispatcher* oscDispatcher = nullptr;
};

} // namespace bridge

#endif /* tzpl_app_context_hpp */
