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

#pragma once
#include "synthdef_types2.hpp"
#include <memory>


namespace synthdef {
    int encodeRune(u32 utf, char *out);
    string encodeRune(u32 utf);
    u32 decodeRune(const char*& s);

    void spaces(string& s, int n);
    void dotindent(string& s, int n, int dotinterval);
    void tabIndent(string& s, int n);
    
    string ftos(i32 x);
    string ftos(i64 x);
    string ftos(f64 x);
    string ftos(f32 x);
}

