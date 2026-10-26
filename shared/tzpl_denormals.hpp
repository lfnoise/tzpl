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
//  tzpl_denormals.hpp
//
//  Flush-to-zero (and, on x86, denormals-are-zero) for the calling thread:
//  every audio thread sets it, so a decaying filter or envelope never runs
//  on subnormal floats, which cost up to 100x per operation on x86. The
//  generated plugins are dylibs, so -ffast-math's startup FTZ (crtfastmath,
//  executables only) never applies to them.
//

#pragma once

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <cstdint>

namespace tzpl {

inline void flushDenormals() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t(1) << 24);          // FZ: inputs and results
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

} // namespace tzpl
