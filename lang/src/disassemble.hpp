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
//  disassemble.hpp
//  lang
//
//  Disassembler for CodeBlock instruction streams
//

#ifndef disassemble_hpp
#define disassemble_hpp

#include "vm.hpp"
#include <cstdio>

namespace ts {

class CodeBlock;

// Disassemble a CodeBlock's instruction stream to the given FILE.
void disassembleCodeBlock(CodeBlock* block, FILE* out);

} // namespace ts

#endif /* disassemble_hpp */
