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
//  tzpl_nats_vm_handlers.hpp
//  bridge
//
//  NATS handlers for Tzopilotl VM interaction: tzpl.eval, tzpl.call,
//  and dynamic user-registered handlers via nats.onMessage().
//

#ifndef tzpl_nats_vm_handlers_hpp
#define tzpl_nats_vm_handlers_hpp

#include <string>

namespace nats { class NatsDispatcher; }
namespace bridge {
struct AppContext;

// Register built-in VM NATS handlers (tzpl.eval, tzpl.call) on the dispatcher.
// If engineName is non-empty, also registers under engines.{name}.tzpl.*
// and engines.all.tzpl.* for namespaced and broadcast addressing.
// Must be called after AppContext is fully populated.
void registerVMNatsHandlers(nats::NatsDispatcher& dispatcher, AppContext& appCtx,
                             const std::string& engineName = "");

} // namespace bridge

#endif /* tzpl_nats_vm_handlers_hpp */
