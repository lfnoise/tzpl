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

/*
 *  tzpl_dynlib.hpp
 *
 *  Dynamic-library loading for plugins: dlopen on POSIX, LoadLibrary on
 *  Windows. Host-side only (never included by generated plugin code).
 *  Handles are opaque; the refcount semantics match on both platforms, so a
 *  dynlibClose balances every dynlibOpen / dynlibRetainFromAddress.
 */

#ifndef tzpl_dynlib_hpp
#define tzpl_dynlib_hpp

#include <string>

namespace tzpl {

using DynLib = void*;

// Load a plugin. On Windows the plugin's own directory is added to the DLL
// search path for that load, so a DLL staged next to the plugin resolves.
// Returns nullptr on failure; dynlibError() has the reason.
DynLib dynlibOpen(char const* path);

// Same, but symbols are not made available to later loads (RTLD_LOCAL).
// Used for probing plugins without registering them.
DynLib dynlibOpenLocal(char const* path);

// Look up an exported function or data symbol. nullptr if absent.
void* dynlibSym(DynLib lib, char const* name);

// Drop one reference. Safe on nullptr.
void dynlibClose(DynLib lib);

// Human-readable text for the most recent failure on this thread.
std::string dynlibError();

// Retain the library that contains `addr` (bump its reference count) and
// return a handle that dynlibClose releases. nullptr if `addr` is not inside
// a loaded library.
DynLib dynlibRetainFromAddress(void const* addr);

} // namespace tzpl

#endif // tzpl_dynlib_hpp
