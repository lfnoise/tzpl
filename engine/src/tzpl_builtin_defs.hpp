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
//  tzpl_builtin_defs.hpp
//  audio engine
//
//  Fixed-function node defs the engine provides natively, at any channel
//  count, without a plugin compile. Graph plumbing that a client would
//  otherwise have to generate, compile with clang, link and dlopen -- a
//  second or so of latency per def per session -- for what is a memcpy.
//
//    _wire<N>   N-channel pass-through: out = in.
//    _gain<N>   N-channel gain: out = in * vol, `vol` a 1-channel inlet so it
//               can be set as a constant (setInput) or ramped (setInputX).
//
//  The live-coding proxy layer (lang/modules/live) uses _wire as each
//  proxy's anchor and _gain as its monitor. Names carry a leading underscore
//  so a user synthdef cannot supersede them by accident.
//

#ifndef tzpl_builtin_defs_h
#define tzpl_builtin_defs_h

#include "tzpl_client_interface.hpp"

namespace engine {

// Register the def for `chans` on this engine if it is not there yet and
// return its name. Idempotent; the returned string lives for the process.
// NRT-side (takes nrt_lock_ via addNodeDef), so the audio thread may be
// running. chans must be >= 1.
char const* ensureWireDef(Engine* e, int chans);
char const* ensureGainDef(Engine* e, int chans);

} // namespace engine

#endif // tzpl_builtin_defs_h
