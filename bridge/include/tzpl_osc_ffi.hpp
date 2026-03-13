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
//  tzpl_osc_ffi.hpp
//  bridge
//
//  FFI bridge: registers OSC functions as callable foreign functions
//  in the Tzopilotl compiler/VM.
//

#ifndef tzpl_osc_ffi_hpp
#define tzpl_osc_ffi_hpp

namespace ts { class Compiler; }

namespace bridge {

// Register all OSC FFI functions with the Tzopilotl compiler.
// Must be called BEFORE compiling any Tzopilotl source that references these functions.
void registerOscFFI(ts::Compiler& compiler);

} // namespace bridge

#endif /* tzpl_osc_ffi_hpp */
