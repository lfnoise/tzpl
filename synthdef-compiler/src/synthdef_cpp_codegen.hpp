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
//  synthdef_cpp_codegen.hpp
//  synthdef-after-rust-cpp-xcode
//
//  Created by James McCartney on 3/7/24.
//

#pragma once

#include "synthdef_synth.hpp"


namespace synthdef {

string cppCodeGen(Synth* synth, int maxSimdWidth = 4);

} // namespace synthdef
