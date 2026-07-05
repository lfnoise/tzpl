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
//  pretty_print.hpp
//  lang
//
//  Width-aware value pretty printer (Wadler, "A prettier printer"): builds
//  a Doc IR from a value and lays it out against a target line width --
//  each container group prints on one line when it fits, or breaks one
//  element per line with 2-space indent when it does not.
//
//  The token vocabulary is exactly the flat printer's: at unbounded width
//  the output equals toString for acyclic values. Cycles print as ^n^
//  through the same PrintCycleScope ancestor chain as the flat printer.
//

#ifndef pretty_print_hpp
#define pretty_print_hpp

#include "value.hpp"

namespace ts {

inline constexpr i32 kPrettyDefaultWidth = 80;

// Render the (possibly multi-word) value at `base` of static type `type`.
// `width` is the target line width in characters; huge widths give flat
// output identical to toString.
VMString prettyString(Word const* base, Type* type, i32 width);

} // namespace ts

#endif /* pretty_print_hpp */
