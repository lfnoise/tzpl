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
//  synthdef_signal_type.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 2/17/25.
//

#include "synthdef_signal_type.hpp"
#include "synthdef_hash.hpp"

namespace synthdef {

    u64 SignalRate::hash() const {
        return synthdef::hash64(static_cast<u64>(rate), 0x9A5BC05E3548135A);
    }

    const NumType NumType::empty = NumType(0);
    const NumType NumType::any = NumType(I32 | I64 | F32 | F64);
    const NumType NumType::any_int = NumType(I32 | I64);
    const NumType NumType::any_float = NumType(F32 | F64);
    const NumType NumType::i32 = NumType(I32);
    const NumType NumType::i64 = NumType(I64);
    const NumType NumType::f32 = NumType(F32);
    const NumType NumType::f64 = NumType(F64);
    
    u64 NumType::hash() const {
        return synthdef::hash64(static_cast<u64>(flags), 0xBCBD04C91702419A);
    }

    u64 ControlSpec::hash() const {
        return hash_combine(kHashStart, lo, hi, param, warpParam) + hash64(warp);
    }

}
