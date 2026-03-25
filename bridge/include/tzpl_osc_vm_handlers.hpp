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
//  tzpl_osc_vm_handlers.hpp
//  bridge
//
//  OSC handlers for Tzopilotl VM interaction: /tzpl/eval, /tzpl/call,
//  and dynamic user-registered handlers via osc.onMessage().
//

#ifndef tzpl_osc_vm_handlers_hpp
#define tzpl_osc_vm_handlers_hpp

namespace osc { class OscDispatcher; }
namespace bridge {
struct AppContext;

// Register built-in VM OSC handlers (/tzpl/eval, /tzpl/call) on the dispatcher.
// Must be called after AppContext is fully populated.
void registerVMOscHandlers(osc::OscDispatcher& dispatcher, AppContext& appCtx);

} // namespace bridge

#endif /* tzpl_osc_vm_handlers_hpp */
