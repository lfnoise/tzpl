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
 *  tzpl_process.hpp
 *
 *  Run a child process and stream its output, without a shell. Used for the
 *  runtime plugin compile/link steps. Arguments are passed as a vector, so
 *  paths with spaces need no quoting by the caller (the Windows side builds
 *  a command line that CommandLineToArgvW parses back to the same argv).
 *  Not usable with popen(): _popen fails from a GUI-subsystem process.
 */

#ifndef tzpl_process_hpp
#define tzpl_process_hpp

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tzpl {

// Run argv[0] (searched on PATH unless it contains a directory separator)
// with the remaining arguments. stdout and stderr are merged and delivered a
// line at a time (without the trailing newline) to `onLine` as they arrive.
// Returns the process exit code, or -1 if it could not be started (in which
// case onLine receives the reason).
int runProcess(std::vector<std::string> const& argv,
               std::function<void(std::string_view line)> const& onLine);

// The argv joined for display in logs (quoted where needed).
std::string commandLineForDisplay(std::vector<std::string> const& argv);

} // namespace tzpl

#endif // tzpl_process_hpp
