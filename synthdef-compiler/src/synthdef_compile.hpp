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
//  synthdef_compile.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 8/5/24.
//

#ifndef synthdef_compile_hpp
#define synthdef_compile_hpp

#include "synthdef_types2.hpp"
#include "synthdef_compile_link.hpp"

namespace synthdef {
    void test(string synthName, int seconds, std::function<void()> f);

    string codegen(string synthName, std::function<void()> f);

    void runInternalAudioEngine(string dir, string synthName, int seconds);

}

#endif /* synthdef_compile_hpp */
