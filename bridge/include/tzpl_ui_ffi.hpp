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
//  tzpl_ui_ffi.hpp
//  bridge
//
//  FFI bridge for the `ui` lang module: live control widgets bound to
//  engine parameters or lang callbacks. The widget registry (UIState)
//  hangs off the AppContext; the app's GUI renders it.
//

#ifndef tzpl_ui_ffi_hpp
#define tzpl_ui_ffi_hpp

namespace ts {
    class Compiler;
    struct NRTVM;
}

namespace bridge {

struct UIState;

// Register the ui_ffi module functions with the compiler.
void registerUIFFI(ts::Compiler& compiler);

// Register a GC root scanner on the NRT VM that keeps widget onChange
// closures alive. Call once at startup, after creating the UIState.
// `ui` must outlive `nrtvm`.
void registerUIRootScanner(ts::NRTVM& nrtvm, UIState& ui);

} // namespace bridge

#endif /* tzpl_ui_ffi_hpp */
