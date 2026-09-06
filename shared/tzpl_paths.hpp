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
 *  tzpl_paths.hpp
 *
 *  Where things live on this platform: the running executable, the
 *  distribution folder around it, the user's home / config directories, and
 *  the separator used in PATH-style lists. Everything else derives its
 *  locations from these so the per-OS rules exist in exactly one place.
 */

#ifndef tzpl_paths_hpp
#define tzpl_paths_hpp

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace tzpl {

// Separator between entries of a PATH-style list ($TZPL_PATH, -I a:b).
// ':' on POSIX, ';' on Windows (drive letters contain colons).
#ifdef _WIN32
inline constexpr char kPathListSep = ';';
#else
inline constexpr char kPathListSep = ':';
#endif

// Split a PATH-style list on kPathListSep, dropping empty entries.
std::vector<std::string> splitPathList(std::string_view list);

// Strings in the language are UTF-8 bytes; std::filesystem::path::string()
// is the ANSI code page on Windows. These convert losslessly on every
// platform. pathToUtf8 keeps the native separator; pathToUtf8Generic uses
// '/', which is what .x code (std.path) expects.
std::filesystem::path pathFromUtf8(std::string_view utf8);
std::string pathToUtf8(std::filesystem::path const& p);
std::string pathToUtf8Generic(std::filesystem::path const& p);

// Absolute path of the running executable with symlinks resolved. Empty on
// failure.
std::filesystem::path executablePath();

// The distribution folder: the nearest ancestor of the executable (up to six
// levels, enough for Tzopilotl.app/Contents/MacOS inside it) that contains a
// modules/ directory. Empty for a build-tree binary.
std::filesystem::path distRoot();

// The user's home directory ($HOME, or %USERPROFILE% on Windows). Empty if
// unknown.
std::filesystem::path homeDir();

// Per-user configuration directory for this application:
//   macOS    ~/Library/Application Support/Tzopilotl
//   Linux    $XDG_CONFIG_HOME/tzpl or ~/.config/tzpl
//   Windows  %APPDATA%\Tzopilotl
// Empty if it cannot be determined. Not created.
std::filesystem::path userConfigDir();

// Per-user directory for large regenerable data (the runtime plugin build
// cache): ~/tzpl-build on POSIX, %LOCALAPPDATA%\tzpl-build on Windows,
// falling back to the system temp directory. Not created.
std::filesystem::path defaultBuildDir();

} // namespace tzpl

#endif // tzpl_paths_hpp
