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
//  tzpl_dsp/tzpl_dsp.hpp
//  The block-engine runtime support library (block engine plan §5), header
//  only, in namespace tzpl::dsp:
//   vec_types.hpp    4-lane vectors of i32, i64, f32, f64; traits; transpose4;
//                    select, truth, min, max, reductions
//   vmath.hpp        math on scalars and vectors: Apple simd, Sleef or libm
//   scan.hpp         lane prefix scans (+, max, min, xor, wrapped +, affine)
//   state_space.hpp  block state space for affine cycles
//   random.hpp       xoroshiro128++ streams and uniform values
//

#ifndef tzpl_dsp_hpp
#define tzpl_dsp_hpp

#include "random.hpp"
#include "scan.hpp"
#include "state_space.hpp"
#include "vec_types.hpp"
#include "vmath.hpp"

#endif  // tzpl_dsp_hpp
