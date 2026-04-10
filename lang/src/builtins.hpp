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
//  builtins.hpp
//  lang
//
//  Built-in math function registration
//

#ifndef builtins_hpp
#define builtins_hpp

#include "type_checker.hpp"

namespace ts {

// Register all built-in math functions into the function table
void registerBuiltinFunctions(Compiler& compiler,
    std::unordered_map<std::string, std::deque<FuncInfo>>& functions);

} // namespace ts

#endif /* builtins_hpp */
