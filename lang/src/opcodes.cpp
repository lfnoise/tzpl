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
//  opcodes.cpp
//  lang
//
//  Direct-threaded instruction handler implementations
//  Every handler ends with [[clang::musttail]] return to the next opcode.
//

#include "opcodes.hpp"
#include "value.hpp"
#include "type_system.hpp"
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace ts {

// Macro for the common tail-call dispatch pattern
#define DISPATCH(offset) \
    [[clang::musttail]] return (pc + (offset))->op(vm, pc + (offset))

// Cyclic index: wraps index modularly so it's always in [0, size).
// Negative indices wrap around: -1 is the last element, etc.
inline size_t cyclicIndex(i64 idx, size_t size) {
    if (size == 0) {
        throw std::runtime_error("Cannot index an empty array");
    }
    idx = idx % static_cast<i64>(size);
    if (idx < 0) idx += static_cast<i64>(size);
    return static_cast<size_t>(idx);
}

// --- Load/Store ---

// LOAD_INT_CONST Rd, K  (3 words: op, regs, i64)
void op_load_int_const(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).i = pc[2].i;
    DISPATCH(3);
}

// LOAD_FLOAT_CONST Rd, K  (3 words: op, regs, f64)
void op_load_float_const(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).f = pc[2].f;
    DISPATCH(3);
}

// LOAD_BOOL_TRUE Rd  (2 words: op, regs)
void op_load_bool_true(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).i = 1;
    DISPATCH(2);
}

// LOAD_BOOL_FALSE Rd  (2 words: op, regs)
void op_load_bool_false(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).i = 0;
    DISPATCH(2);
}

// LOAD_NIL Rd  (2 words: op, regs)
void op_load_nil(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).i = 0;
    DISPATCH(2);
}

// LOAD_OBJ Rd, idx  (2 words: op, regs{Rd, idx})
// Loads from current CodeBlock's objConstants by index
void op_load_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u16 idx = pc[1].regs[1];
    vm.reg(dst).o = vm.currentCodeBlock()->objConstants[idx];
    DISPATCH(2);
}

// MOV Rd, Ra  (2 words: op, regs)
void op_mov(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u16 src = pc[1].regs[1];
    vm.reg(dst) = vm.reg(src);
    DISPATCH(2);
}

// MOV_N Rd, Ra, N  (3 words: op, regs, i64 nWords)
// Multi-word block copy. Rd and Ra are the first words of the
// destination/source slots; N consecutive Words are copied.
// Used for inline value types (Phase 4) — sizes always multiple of 8 bytes.
void op_move_n(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u16 src = pc[1].regs[1];
    i64 n   = pc[2].i;
    for (i64 i = 0; i < n; ++i) {
        vm.reg((u16)(dst + i)) = vm.reg((u16)(src + i));
    }
    DISPATCH(3);
}

// LOAD_GLOBAL Rd, K  (3 words: op, regs, global_index)
void op_load_global(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    vm.reg(dst) = vm.global(idx);
    DISPATCH(3);
}

// STORE_GLOBAL Ra, K  (3 words: op, regs, global_index)
void op_store_global(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    vm.global(idx) = vm.reg(src);
    DISPATCH(3);
}

// STORE_GLOBAL_OBJ Ra, K  (3 words: op, regs, global_index)
// Like STORE_GLOBAL but retains the new Obj* and releases the old one.
void op_store_global_obj(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    Obj* newVal = vm.reg(src).o;
    Obj* oldVal = vm.global(idx).o;
    if (newVal) newVal->retain();
    vm.global(idx) = vm.reg(src);
    if (oldVal) oldVal->release();
    DISPATCH(3);
}

// INIT_GLOBAL_OBJ Ra, K  (3 words: op, regs, global_index)
// Like STORE_GLOBAL but retains the new Obj*. No release of old value.
// Used for declarations (let/var/const) where the old value may be a different type.
void op_init_global_obj(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    Obj* newVal = vm.reg(src).o;
    if (newVal) newVal->retain();
    vm.global(idx) = vm.reg(src);
    DISPATCH(3);
}

// --- Integer Arithmetic ---

// ADD_INT Rd, Ra, Rb  (2 words: op, regs)
void op_add_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i + vm.reg(b).i;
    DISPATCH(2);
}

// SUB_INT Rd, Ra, Rb  (2 words: op, regs)
void op_sub_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i - vm.reg(b).i;
    DISPATCH(2);
}

// MUL_INT Rd, Ra, Rb  (2 words: op, regs)
void op_mul_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i * vm.reg(b).i;
    DISPATCH(2);
}

// DIV_INT Rd, Ra, Rb  (2 words: op, regs)
void op_div_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i / vm.reg(b).i;
    DISPATCH(2);
}

// MOD_INT Rd, Ra, Rb  (2 words: op, regs)
void op_mod_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i % vm.reg(b).i;
    DISPATCH(2);
}

// NEG_INT Rd, Ra  (2 words: op, regs)
void op_neg_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i = -vm.reg(a).i;
    DISPATCH(2);
}

// --- Float Arithmetic ---

void op_add_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).f = vm.reg(a).f + vm.reg(b).f;
    DISPATCH(2);
}

void op_sub_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).f = vm.reg(a).f - vm.reg(b).f;
    DISPATCH(2);
}

void op_mul_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).f = vm.reg(a).f * vm.reg(b).f;
    DISPATCH(2);
}

void op_div_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).f = vm.reg(a).f / vm.reg(b).f;
    DISPATCH(2);
}

void op_neg_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f = -vm.reg(a).f;
    DISPATCH(2);
}

// --- Fraction Arithmetic (Phase 4f: inline 2 words [numer, denom]) ---

// Helpers operating directly on i64 numer/denom pairs.
static inline void norm_frac(i64& n, i64& d) {
    if (d < 0) { n = -n; d = -d; }
    i64 a = n < 0 ? -n : n, b = d;
    while (b) { i64 t = a % b; a = b; b = t; }
    if (a > 1) { n /= a; d /= a; }
}

void op_add_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    i64 n = an * bd + bn * ad;
    i64 d = ad * bd;
    norm_frac(n, d);
    vm.reg(dst).i         = n;
    vm.reg((u16)(dst+1)).i = d;
    DISPATCH(2);
}

void op_sub_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    i64 n = an * bd - bn * ad;
    i64 d = ad * bd;
    norm_frac(n, d);
    vm.reg(dst).i         = n;
    vm.reg((u16)(dst+1)).i = d;
    DISPATCH(2);
}

void op_mul_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    i64 n = an * bn;
    i64 d = ad * bd;
    norm_frac(n, d);
    vm.reg(dst).i         = n;
    vm.reg((u16)(dst+1)).i = d;
    DISPATCH(2);
}

void op_div_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    i64 n = an * bd;
    i64 d = ad * bn;
    norm_frac(n, d);
    vm.reg(dst).i         = n;
    vm.reg((u16)(dst+1)).i = d;
    DISPATCH(2);
}

void op_neg_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i         = -vm.reg(a).i;
    vm.reg((u16)(dst+1)).i = vm.reg((u16)(a+1)).i;
    DISPATCH(2);
}

// --- Fraction Comparison ---

void op_cmp_eq_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    // Fractions are stored canonically reduced, so direct equality works.
    vm.reg(dst).i = (vm.reg(a).i == vm.reg(b).i
                  && vm.reg((u16)(a+1)).i == vm.reg((u16)(b+1)).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i == vm.reg(b).i
                  && vm.reg((u16)(a+1)).i == vm.reg((u16)(b+1)).i) ? 0 : 1;
    DISPATCH(2);
}

void op_cmp_lt_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    // an/ad < bn/bd  iff  an*bd < bn*ad  (denoms positive after norm)
    vm.reg(dst).i = (an * bd < bn * ad) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_le_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    vm.reg(dst).i = (an * bd <= bn * ad) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_gt_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    vm.reg(dst).i = (an * bd > bn * ad) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ge_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    i64 an = vm.reg(a).i,     ad = vm.reg((u16)(a+1)).i;
    i64 bn = vm.reg(b).i,     bd = vm.reg((u16)(b+1)).i;
    vm.reg(dst).i = (an * bd >= bn * ad) ? 1 : 0;
    DISPATCH(2);
}

// --- Complex Arithmetic (Phase 4f: inline 2 words [real, imag]) ---

void op_add_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    vm.reg(dst).f         = ar + br;
    vm.reg((u16)(dst+1)).f = ai + bi;
    DISPATCH(2);
}

void op_sub_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    vm.reg(dst).f         = ar - br;
    vm.reg((u16)(dst+1)).f = ai - bi;
    DISPATCH(2);
}

void op_mul_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    f64 rr = ar*br - ai*bi;
    f64 ri = ar*bi + ai*br;
    vm.reg(dst).f         = rr;
    vm.reg((u16)(dst+1)).f = ri;
    DISPATCH(2);
}

void op_div_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    f64 denom = br*br + bi*bi;
    f64 rr = (ar*br + ai*bi) / denom;
    f64 ri = (ai*br - ar*bi) / denom;
    vm.reg(dst).f         = rr;
    vm.reg((u16)(dst+1)).f = ri;
    DISPATCH(2);
}

void op_neg_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f         = -vm.reg(a).f;
    vm.reg((u16)(dst+1)).f = -vm.reg((u16)(a+1)).f;
    DISPATCH(2);
}

// --- Complex Comparison ---

void op_cmp_eq_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    bool eq = (vm.reg(a).f == vm.reg(b).f)
           && (vm.reg((u16)(a+1)).f == vm.reg((u16)(b+1)).f);
    vm.reg(dst).i = eq ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    bool eq = (vm.reg(a).f == vm.reg(b).f)
           && (vm.reg((u16)(a+1)).f == vm.reg((u16)(b+1)).f);
    vm.reg(dst).i = eq ? 0 : 1;
    DISPATCH(2);
}

// --- Complex / Fraction Boxing (Phase 4f) ---
// Box: take a 2-word inline slot and allocate a heap Obj.
// Unbox: read a heap Obj and write 2 words to a slot.

void op_box_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    f64 re = vm.reg(src).f, im = vm.reg((u16)(src+1)).f;
    vm.reg(dst).o = new Complex(x64(re, im));
    DISPATCH(2);
}

void op_unbox_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* z = static_cast<Complex*>(vm.reg(src).o);
    vm.reg(dst).f         = z->x.real();
    vm.reg((u16)(dst+1)).f = z->x.imag();
    DISPATCH(2);
}

void op_box_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    i64 n = vm.reg(src).i, d = vm.reg((u16)(src+1)).i;
    vm.reg(dst).o = new Fraction(r64(n, d));
    DISPATCH(2);
}

void op_unbox_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* f = static_cast<Fraction*>(vm.reg(src).o);
    vm.reg(dst).i         = f->r.numer();
    vm.reg((u16)(dst+1)).i = f->r.denom();
    DISPATCH(2);
}

// --- Complex Inline Arithmetic (Phase 4f scaffolding) ---
// Complex represented as 2 consecutive Words: word[0] = real (f64), word[1] = imag (f64).
// Operand and dst regs name the FIRST word of each 2-word slot.
// Not yet emitted by codegen — Complex is still classified as Pointer; these
// handlers exist for the upcoming inline bring-up.

void op_add_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    vm.reg(dst).f         = ar + br;
    vm.reg((u16)(dst+1)).f = ai + bi;
    DISPATCH(2);
}

void op_sub_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    vm.reg(dst).f         = ar - br;
    vm.reg((u16)(dst+1)).f = ai - bi;
    DISPATCH(2);
}

void op_mul_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    // (ar+ai*i)(br+bi*i) = (ar*br - ai*bi) + (ar*bi + ai*br)i
    f64 rr = ar*br - ai*bi;
    f64 ri = ar*bi + ai*br;
    vm.reg(dst).f         = rr;
    vm.reg((u16)(dst+1)).f = ri;
    DISPATCH(2);
}

void op_div_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    f64 ar = vm.reg(a).f,     ai = vm.reg((u16)(a+1)).f;
    f64 br = vm.reg(b).f,     bi = vm.reg((u16)(b+1)).f;
    // (ar+ai*i)/(br+bi*i) = ((ar*br + ai*bi) + (ai*br - ar*bi)i) / (br^2 + bi^2)
    f64 denom = br*br + bi*bi;
    f64 rr = (ar*br + ai*bi) / denom;
    f64 ri = (ai*br - ar*bi) / denom;
    vm.reg(dst).f         = rr;
    vm.reg((u16)(dst+1)).f = ri;
    DISPATCH(2);
}

void op_neg_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f         = -vm.reg(a).f;
    vm.reg((u16)(dst+1)).f = -vm.reg((u16)(a+1)).f;
    DISPATCH(2);
}

void op_cmp_eq_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    bool eq = (vm.reg(a).f == vm.reg(b).f)
           && (vm.reg((u16)(a+1)).f == vm.reg((u16)(b+1)).f);
    vm.reg(dst).i = eq ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_complex_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    bool eq = (vm.reg(a).f == vm.reg(b).f)
           && (vm.reg((u16)(a+1)).f == vm.reg((u16)(b+1)).f);
    vm.reg(dst).i = eq ? 0 : 1;
    DISPATCH(2);
}

// --- Generic Object Comparison ---

void op_cmp_eq_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Obj* oa = vm.reg(a).o;
    Obj* ob = vm.reg(b).o;
    if (!oa || !ob) {
        vm.reg(dst).i = (oa == ob) ? 1 : 0;
        DISPATCH(2);
    }
    WordEqual eq{oa->type_};
    vm.reg(dst).i = eq(vm.reg(a), vm.reg(b)) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Obj* oa = vm.reg(a).o;
    Obj* ob = vm.reg(b).o;
    if (!oa || !ob) {
        vm.reg(dst).i = (oa == ob) ? 0 : 1;
        DISPATCH(2);
    }
    WordEqual eq{oa->type_};
    vm.reg(dst).i = eq(vm.reg(a), vm.reg(b)) ? 0 : 1;
    DISPATCH(2);
}

// --- Conversion ---

void op_int_to_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f = (f64)vm.reg(a).i;
    DISPATCH(2);
}

void op_float_to_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i = (i64)vm.reg(a).f;
    DISPATCH(2);
}

// Phase 4f: Fraction is inline 2 words [numer, denom]; Complex is inline
// 2 words [real, imag]. dst names the FIRST word; caller allocates the slot.

void op_int_to_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i         = vm.reg(a).i;  // numer = n
    vm.reg((u16)(dst+1)).i = 1;            // denom = 1
    DISPATCH(2);
}

void op_int_to_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f         = (f64)vm.reg(a).i;
    vm.reg((u16)(dst+1)).f = 0.0;
    DISPATCH(2);
}

void op_fraction_to_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 n = vm.reg(a).i, d = vm.reg((u16)(a+1)).i;
    vm.reg(dst).f = (f64)n / (f64)d;
    DISPATCH(2);
}

void op_fraction_to_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 n = vm.reg(a).i, d = vm.reg((u16)(a+1)).i;
    vm.reg(dst).f         = (f64)n / (f64)d;
    vm.reg((u16)(dst+1)).f = 0.0;
    DISPATCH(2);
}

void op_float_to_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f         = vm.reg(a).f;
    vm.reg((u16)(dst+1)).f = 0.0;
    DISPATCH(2);
}

void op_fraction_to_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 n = vm.reg(a).i, d = vm.reg((u16)(a+1)).i;
    vm.reg(dst).i = n / d;
    DISPATCH(2);
}

void op_complex_to_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).f = vm.reg(a).f;  // real
    DISPATCH(2);
}

void op_complex_to_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i = (i64)vm.reg(a).f;  // (i64)real
    DISPATCH(2);
}

void op_complex_to_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i         = (i64)vm.reg(a).f;
    vm.reg((u16)(dst+1)).i = 1;
    DISPATCH(2);
}

// --- Construction ---

// op_make_complex Rd, Ra, Rb -- write inline Complex(real=Ra.f, imag=Rb.f)
// to the 2-word slot starting at Rd. (Phase 4f: was heap-allocating; now inline.)
void op_make_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], ra = pc[1].regs[1], rb = pc[1].regs[2];
    f64 re = vm.reg(ra).f, im = vm.reg(rb).f;
    vm.reg(dst).f         = re;
    vm.reg((u16)(dst+1)).f = im;
    DISPATCH(2);
}

void op_int_div(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i / vm.reg(b).i;
    DISPATCH(2);
}

// --- Integer Comparison ---

void op_cmp_eq_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i == vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i != vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_lt_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i < vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_le_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i <= vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_gt_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i > vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ge_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i >= vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

// --- Float Comparison ---

void op_cmp_eq_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f == vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f != vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_lt_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f < vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_le_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f <= vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_gt_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f > vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ge_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).f >= vm.reg(b).f) ? 1 : 0;
    DISPATCH(2);
}

// --- Bitwise Integer ---

void op_bitand_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i & vm.reg(b).i;
    DISPATCH(2);
}

void op_bitor_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i | vm.reg(b).i;
    DISPATCH(2);
}

void op_bitxor_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i ^ vm.reg(b).i;
    DISPATCH(2);
}

void op_bitnot_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i = ~vm.reg(a).i;
    DISPATCH(2);
}

void op_shl_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i << vm.reg(b).i;
    DISPATCH(2);
}

void op_shr_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i >> vm.reg(b).i;
    DISPATCH(2);
}

void op_ushr_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (i64)((u64)vm.reg(a).i >> vm.reg(b).i);
    DISPATCH(2);
}

// --- Logic ---

void op_not_bool(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    vm.reg(dst).i = vm.reg(a).i ? 0 : 1;
    DISPATCH(2);
}

void op_and_bool(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i && vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

void op_or_bool(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i || vm.reg(b).i) ? 1 : 0;
    DISPATCH(2);
}

// --- Control Flow ---

// JUMP L  (2 words: op, Code* target)
void op_jump(VM& vm, Code* pc) {
    Code* target = static_cast<Code*>(pc[1].p);
    [[clang::musttail]] return target->op(vm, target);
}

// JUMP_IF_TRUE Ra, L  (3 words: op, regs, Code* target)
void op_jump_if_true(VM& vm, Code* pc) {
    u16 cond = pc[1].regs[0];
    if (vm.reg(cond).i) {
        Code* target = static_cast<Code*>(pc[2].p);
        [[clang::musttail]] return target->op(vm, target);
    }
    DISPATCH(3);
}

// JUMP_IF_FALSE Ra, L  (3 words: op, regs, Code* target)
void op_jump_if_false(VM& vm, Code* pc) {
    u16 cond = pc[1].regs[0];
    if (!vm.reg(cond).i) {
        Code* target = static_cast<Code*>(pc[2].p);
        [[clang::musttail]] return target->op(vm, target);
    }
    DISPATCH(3);
}

// CALL Rd, callee_global_idx, argc
// Layout: [op] [regs: Rd, argc, argBase] [callee_global_idx as i64] [unused]
// Args must be in consecutive registers starting at argBase
void op_call(VM& vm, Code* pc) {
    u16 resultReg = pc[1].regs[0];
    [[maybe_unused]] u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u32 calleeIdx = (u32)pc[2].i;

    // Get the callee's CodeBlock from the global variable
    if (calleeIdx >= vm.numGlobals()) {
        throw std::runtime_error("op_call: global index " + std::to_string(calleeIdx)
            + " out of range (VM has " + std::to_string(vm.numGlobals()) + " globals)");
    }
    CodeBlock* callee = static_cast<CodeBlock*>(vm.global(calleeIdx).p);
    if (!callee) {
        throw std::runtime_error("op_call: null CodeBlock at global index " + std::to_string(calleeIdx)
            + " (VM has " + std::to_string(vm.numGlobals()) + " globals)");
    }

    // Use flat register file (even when inside a coroutine)
    u32 newBase = vm.baseReg() + argBase;

    // Push call frame (saves return context)
    Code* returnPC = pc + 3;  // instruction after this CALL
    vm.pushFrame(returnPC, callee, newBase, callee->numRegs, resultReg);

    // Jump to the appropriate entry point (handle default arguments)
    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// CALL_PRIMITIVE Rd, argc, argBase, global_idx
// Layout: [op] [regs: Rd, argc, argBase] [global_idx as i64]
// Calls a Primitive's cfun_ directly without pushing a call frame.
void op_call_primitive(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u32 idx = (u32)pc[2].i;
    auto* prim = static_cast<Primitive*>(vm.global(idx).o);
    vm.setCurrentPrimitive(prim);
    prim->cfun_(vm, dst, argc, argBase);
    DISPATCH(3);
}

// RETURN Ra  (2 words: op, regs)
void op_return(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    Word result = vm.reg(src);

    // If this is the top-level frame, just halt
    if (vm.frameCount() == 1) {
        vm.popFrame();
        vm.reg(0) = result;
        vm.setHalted(true);
        return;
    }

    // Pop frame - returns the frame data (restores caller's baseReg)
    CallFrame frame = vm.popFrame();

    // Write result to caller's result register (now using caller's baseReg)
    vm.reg(frame.resultReg) = result;

    // Resume at the caller's return PC
    Code* returnPC = frame.returnPC;
    [[clang::musttail]] return returnPC->op(vm, returnPC);
}

// RETURN_VOID (1 word: op)
void op_return_void(VM& vm, Code* pc) {
    if (vm.frameCount() == 1) {
        vm.popFrame();
        vm.setHalted(true);
        return;
    }

    CallFrame frame = vm.popFrame();
    Code* returnPC = frame.returnPC;
    [[clang::musttail]] return returnPC->op(vm, returnPC);
}

// HALT (1 word: op)
void op_halt(VM& vm, Code* pc) {
    vm.setHalted(true);
    // Simply return - breaks out of the direct-threaded chain
}

// --- Debug/Print ---

void op_print_int(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    std::fprintf(vm.printOutput(), "%lld", (long long)vm.reg(src).i);
    DISPATCH(2);
}

void op_print_float(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    char buf[32];
    formatFloat(vm.reg(src).f, buf, sizeof(buf));
    std::fputs(buf, vm.printOutput());
    DISPATCH(2);
}

void op_print_bool(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    std::fprintf(vm.printOutput(), "%s", vm.reg(src).i ? "true" : "false");
    DISPATCH(2);
}

void op_print_obj(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    Obj* obj = vm.reg(src).o;
    if (obj) {
        VMString s = obj->str();
        std::fprintf(vm.printOutput(), "%.*s", (int)s.size(), s.data());
    } else {
        std::fprintf(vm.printOutput(), "nil");
    }
    DISPATCH(2);
}

void op_print_symbol(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    auto* sym = vm.reg(src).s;
    if (sym) {
        auto sv = sym->str();
        std::fprintf(vm.printOutput(), "%.*s", (int)sv.size(), sv.data());
    }
    DISPATCH(2);
}

void op_println(VM& vm, Code* pc) {
    std::fprintf(vm.printOutput(), "\n");
    std::fflush(vm.printOutput());
    DISPATCH(1);
}

// --- String Operations ---

// CONCAT_STR Rd, Ra, Rb  (2 words: op, regs)
void op_concat_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    auto* result = new StringObj();
    result->s = sa->s;
    result->s += sb->s;
    registerNewObj(result);
    vm.reg(dst).o = result;
    DISPATCH(2);
}

// CMP_EQ_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_eq_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s == sb->s) ? 1 : 0;
    DISPATCH(2);
}

// CMP_NE_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_ne_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s != sb->s) ? 1 : 0;
    DISPATCH(2);
}

// CMP_LT_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_lt_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s < sb->s) ? 1 : 0;
    DISPATCH(2);
}

// CMP_LE_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_le_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s <= sb->s) ? 1 : 0;
    DISPATCH(2);
}

// CMP_GT_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_gt_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s > sb->s) ? 1 : 0;
    DISPATCH(2);
}

// CMP_GE_STR Rd, Ra, Rb  (2 words: op, regs)
void op_cmp_ge_str(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* sa = static_cast<StringObj*>(vm.reg(a).o);
    auto* sb = static_cast<StringObj*>(vm.reg(b).o);
    vm.reg(dst).i = (sa->s >= sb->s) ? 1 : 0;
    DISPATCH(2);
}

// STRING_GET_BYTE Rd, Rs, Ri (2 words: op, regs{dst, str, idx})
void op_string_get_byte(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], strReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    auto* s = static_cast<StringObj*>(vm.reg(strReg).o);
    i64 idx = vm.reg(idxReg).i;
    vm.reg(dst).i = (i64)(unsigned char)s->s[(size_t)idx];
    DISPATCH(2);
}

// --- Array/Tuple Concatenation ---

// CONCAT_ARRAY Rd, Ra, Rb (3 words: op, regs{dst, a, b}, ArrayType*)
void op_concat_array(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arrA = static_cast<PodArray<i64>*>(vm.reg(a).o);
        auto* arrB = static_cast<PodArray<i64>*>(vm.reg(b).o);
        auto* result = new PodArray<i64>(arrayType);
        result->v.reserve(arrA->v.size() + arrB->v.size());
        result->v.insert(result->v.end(), arrA->v.begin(), arrA->v.end());
        result->v.insert(result->v.end(), arrB->v.begin(), arrB->v.end());
        vm.reg(dst).o = result;
    } else if (storesF64(elemType)) {
        auto* arrA = static_cast<PodArray<f64>*>(vm.reg(a).o);
        auto* arrB = static_cast<PodArray<f64>*>(vm.reg(b).o);
        auto* result = new PodArray<f64>(arrayType);
        result->v.reserve(arrA->v.size() + arrB->v.size());
        result->v.insert(result->v.end(), arrA->v.begin(), arrA->v.end());
        result->v.insert(result->v.end(), arrB->v.begin(), arrB->v.end());
        vm.reg(dst).o = result;
    } else {
        auto* arrA = static_cast<ObjArray*>(vm.reg(a).o);
        auto* arrB = static_cast<ObjArray*>(vm.reg(b).o);
        auto* result = new ObjArray(arrayType);
        result->reserve(arrA->size() + arrB->size());
        for (auto* obj : *arrA) result->push(obj);
        for (auto* obj : *arrB) result->push(obj);
        vm.reg(dst).o = result;
    }
    DISPATCH(3);
}

// CONCAT_TUPLE Rd, Ra, Rb (4 words: op, regs{dst, a, b}, resultTupleType*, leftTupleType*)
void op_concat_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* resultType = static_cast<TupleType*>(pc[2].p);
    auto* leftType = static_cast<TupleType*>(pc[3].p);

    auto* tupA = static_cast<Tuple*>(vm.reg(a).o);
    auto* tupB = static_cast<Tuple*>(vm.reg(b).o);

    usize leftLen = leftType->fields_.size();
    usize rightLen = resultType->fields_.size() - leftLen;
    auto* result = Tuple::create(resultType, (u32)(leftLen + rightLen));
    for (usize i = 0; i < leftLen; ++i) {
        result->v[i] = tupA->v[i];
    }
    for (usize i = 0; i < rightLen; ++i) {
        result->v[leftLen + i] = tupB->v[i];
    }
    // Retain Obj* fields
    auto* st = static_cast<StructType*>(static_cast<Type*>(resultType));
    for (auto idx : st->gcFields_) {
        if (result->v[idx].o) result->v[idx].o->retain();
    }
    vm.reg(dst).o = result;
    DISPATCH(4);
}

// CONCAT_LIST Rd, Ra, Rb (3 words: op, regs{dst, a, b}, ListType*)
void op_concat_list(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);
    auto* nodeA = static_cast<ListNode*>(vm.reg(a).o);
    auto* nodeB = static_cast<ListNode*>(vm.reg(b).o);
    if (!nodeA) { vm.reg(dst).o = nodeB; DISPATCH(3); }
    if (!nodeB) { vm.reg(dst).o = nodeA; DISPATCH(3); }
    auto* node = new ListNode(listType);
    auto* gen = new CatListGen(vm.typeType());
    gen->first_ = nodeA; gen->second_ = nodeB;
    gen->inSecond_ = false; gen->listType_ = listType;
    node->generator_ = gen;
    gen->first_->retain();
    gen->second_->retain();
    reinterpret_cast<GCObj*>(gen)->retain();
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// --- Array Destructuring ---

// ARRAY_GET Rd, Ra, idx  (3 words: op, regs{dst, src, idx}, ArrayType*)
// Gets element at compile-time constant index from array
void op_array_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], idx = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = static_cast<PodArray<i64>*>(vm.reg(src).o);
        vm.reg(dst).i = arr->v[cyclicIndex(idx, arr->v.size())];
    } else if (storesF64(elemType)) {
        auto* arr = static_cast<PodArray<f64>*>(vm.reg(src).o);
        vm.reg(dst).f = arr->v[cyclicIndex(idx, arr->v.size())];
    } else {
        auto* arr = static_cast<ObjArray*>(vm.reg(src).o);
        vm.reg(dst).o = arr->get(cyclicIndex(idx, arr->size()));
    }
    DISPATCH(3);
}

// ARRAY_SLICE Rd, Ra, startIdx  (3 words: op, regs{dst, src, startIdx}, ArrayType*)
// Creates new array from arr[startIdx:]
void op_array_slice(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], startIdx = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = static_cast<PodArray<i64>*>(vm.reg(src).o);
        auto* result = new PodArray<i64>(arrayType);
        if (startIdx < arr->v.size()) {
            result->v.assign(arr->v.begin() + startIdx, arr->v.end());
        }
        vm.reg(dst).o = result;
    } else if (storesF64(elemType)) {
        auto* arr = static_cast<PodArray<f64>*>(vm.reg(src).o);
        auto* result = new PodArray<f64>(arrayType);
        if (startIdx < arr->v.size()) {
            result->v.assign(arr->v.begin() + startIdx, arr->v.end());
        }
        vm.reg(dst).o = result;
    } else {
        auto* arr = static_cast<ObjArray*>(vm.reg(src).o);
        auto* result = new ObjArray(arrayType);
        if (startIdx < arr->size()) {
            for (size_t i = startIdx; i < arr->size(); ++i)
                result->push(arr->get(i));
        }
        vm.reg(dst).o = result;
    }
    DISPATCH(3);
}

// ARRAY_LENGTH Rd, Ra  (3 words: op, regs{dst, src}, ArrayType*)
// Gets length of array as integer
void op_array_length(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = static_cast<PodArray<i64>*>(vm.reg(src).o);
        vm.reg(dst).i = (i64)arr->v.size();
    } else if (storesF64(elemType)) {
        auto* arr = static_cast<PodArray<f64>*>(vm.reg(src).o);
        vm.reg(dst).i = (i64)arr->v.size();
    } else {
        auto* arr = static_cast<ObjArray*>(vm.reg(src).o);
        vm.reg(dst).i = (i64)arr->size();
    }
    DISPATCH(3);
}

// --- Composite (Array/Tuple) Arithmetic ---
//
// Operator structs + templated dispatch.
// All type/operator dispatch happens OUTSIDE inner loops.
// The operator is a compile-time template parameter, so op() calls
// are inlined in tight loops with zero per-element branching.

// Operator capability flags
constexpr u32 mathOpIntArgs      = 1;
constexpr u32 mathOpFractionArgs = 2;
constexpr u32 mathOpFloatArgs    = 4;
constexpr u32 mathOpComplexArgs  = 8;

// --- Binary operator structs ---

struct OpAdd {
    static constexpr u32 flags = mathOpIntArgs | mathOpFractionArgs | mathOpFloatArgs | mathOpComplexArgs;
    i64 operator()(i64 a, i64 b) const { return a + b; }
    r64 operator()(r64 a, r64 b) const { return a + b; }
    f64 operator()(f64 a, f64 b) const { return a + b; }
    x64 operator()(x64 a, x64 b) const { return a + b; }
};

struct OpSub {
    static constexpr u32 flags = mathOpIntArgs | mathOpFractionArgs | mathOpFloatArgs | mathOpComplexArgs;
    i64 operator()(i64 a, i64 b) const { return a - b; }
    r64 operator()(r64 a, r64 b) const { return a - b; }
    f64 operator()(f64 a, f64 b) const { return a - b; }
    x64 operator()(x64 a, x64 b) const { return a - b; }
};

struct OpMul {
    static constexpr u32 flags = mathOpIntArgs | mathOpFractionArgs | mathOpFloatArgs | mathOpComplexArgs;
    i64 operator()(i64 a, i64 b) const { return a * b; }
    r64 operator()(r64 a, r64 b) const { return a * b; }
    f64 operator()(f64 a, f64 b) const { return a * b; }
    x64 operator()(x64 a, x64 b) const { return a * b; }
};

struct OpDiv {
    // No mathOpIntArgs — Int/Int division is handled by Fraction promotion
    static constexpr u32 flags = mathOpFractionArgs | mathOpFloatArgs | mathOpComplexArgs;
    r64 operator()(r64 a, r64 b) const { return a / b; }
    f64 operator()(f64 a, f64 b) const { return a / b; }
    x64 operator()(x64 a, x64 b) const { return a / b; }
};

// --- Unary operator struct ---

struct OpNeg {
    static constexpr u32 flags = mathOpIntArgs | mathOpFractionArgs | mathOpFloatArgs | mathOpComplexArgs;
    i64 operator()(i64 a) const { return -a; }
    r64 operator()(r64 a) const { return -a; }
    f64 operator()(f64 a) const { return -a; }
    x64 operator()(x64 a) const { return -a; }
};

struct OpNot {
    static constexpr u32 flags = mathOpIntArgs;
    i64 operator()(i64 a) const { return a ? 0 : 1; }
};

struct OpBitNot {
    static constexpr u32 flags = mathOpIntArgs;
    i64 operator()(i64 a) const { return ~a; }
};

// --- Array helpers ---

static usize arrayLen(Obj* arr, Type* elemType, VM& vm) {
    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType))
        return static_cast<PodArray<i64>*>(arr)->v.size();
    if (elemType == vm.floatType())
        return static_cast<PodArray<f64>*>(arr)->v.size();
    return static_cast<ObjArray*>(arr)->size();
}

static Word readElem(Obj* arr, usize i, Type* elemType, VM& vm) {
    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType))
        return Word(static_cast<PodArray<i64>*>(arr)->v[i]);
    if (elemType == vm.floatType())
        return Word(static_cast<PodArray<f64>*>(arr)->v[i]);
    return Word(static_cast<ObjArray*>(arr)->get(i));
}

static Obj* makeArray(VM& vm, ArrayType* type, usize len) {
    Type* elem = type->elemType_;
    if (elem && !storesObjPtr(elem) && !storesF64(elem)) {
        auto* arr = new PodArray<i64>(type);
        arr->v.resize(len);
        return arr;
    }
    if (storesF64(elem)) {
        auto* arr = new PodArray<f64>(type);
        arr->v.resize(len);
        return arr;
    }
    auto* arr = new ObjArray(type);
    arr->resize(len);
    return arr;
}

static void writeElem(Obj* arr, usize i, Word w, Type* elemType, VM& vm) {
    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType))
        static_cast<PodArray<i64>*>(arr)->v[i] = w.i;
    else if (elemType == vm.floatType())
        static_cast<PodArray<f64>*>(arr)->v[i] = w.f;
    else {
        static_cast<ObjArray*>(arr)->set(i, w.o);
    }
}

// --- Scalar conversion helpers ---

static f64 toF64(VM& vm, Word w, Type* t) {
    if (t == vm.intType() || t == vm.boolType()) return (f64)w.i;
    if (t == vm.floatType()) return w.f;
    if (t == vm.fractionType()) return (f64)static_cast<Fraction*>(w.o)->r;
    return 0.0;
}

static r64 toR64(VM& vm, Word w, Type* t) {
    if (t == vm.intType() || t == vm.boolType()) return r64(w.i);
    if (t == vm.fractionType()) return static_cast<Fraction*>(w.o)->r;
    return r64(0);
}

static x64 toX64(VM& vm, Word w, Type* t) {
    if (t == vm.intType() || t == vm.boolType()) return x64((f64)w.i, 0.0);
    if (t == vm.floatType()) return x64(w.f, 0.0);
    if (t == vm.fractionType()) return x64((f64)static_cast<Fraction*>(w.o)->r, 0.0);
    if (t == vm.complexType()) return static_cast<Complex*>(w.o)->x;
    return x64(0.0, 0.0);
}

// Numeric rank for runtime dispatch
static int numRank(VM& vm, Type* t) {
    if (t == vm.boolType() || t == vm.intType()) return 0;
    if (t == vm.fractionType()) return 1;
    if (t == vm.floatType()) return 2;
    if (t == vm.complexType()) return 3;
    if (dynamic_cast<ArrayType*>(t)) return 4;
    if (dynamic_cast<TupleType*>(t)) return 5;
    if (dynamic_cast<ListType*>(t)) return 6;
    return -1;
}

// --- Templated binary dispatch ---
//
// Op is a compile-time parameter (OpAdd, OpSub, OpMul, OpDiv).
// Type dispatch happens via switch on numRank, and `if constexpr`
// eliminates branches for operators that don't support certain types
// (e.g. OpDiv has no Int case). Inner array loops call op() directly
// with no per-element dispatch.

template<typename Op>
static Word dispatchBinop(VM& vm, Op op, Word a, Word b,
                          Type* aType, Type* bType, Type* resultType);

// Fast-path array-array loop: both arrays have the same POD element type
// as the result. Op is inlined, no per-element branching.
template<typename Op, typename T>
static Obj* podArrayLoop(Op op, PodArray<T>* pa, PodArray<T>* pb, ArrayType* resultAT) {
    usize len = std::min(pa->v.size(), pb->v.size());
    auto* r = new PodArray<T>(resultAT);
    r->v.resize(len);
    for (usize i = 0; i < len; ++i)
        r->v[i] = op(pa->v[i], pb->v[i]);
    return r;
}

// Array-scalar fast-path: POD array with POD scalar broadcast
template<typename Op, typename T>
static Obj* podArrayScalarLoop(Op op, PodArray<T>* pa, T scalar, ArrayType* resultAT) {
    usize len = pa->v.size();
    auto* r = new PodArray<T>(resultAT);
    r->v.resize(len);
    for (usize i = 0; i < len; ++i)
        r->v[i] = op(pa->v[i], scalar);
    return r;
}

// Scalar-array fast-path: POD scalar broadcast with POD array
template<typename Op, typename T>
static Obj* podScalarArrayLoop(Op op, T scalar, PodArray<T>* pb, ArrayType* resultAT) {
    usize len = pb->v.size();
    auto* r = new PodArray<T>(resultAT);
    r->v.resize(len);
    for (usize i = 0; i < len; ++i)
        r->v[i] = op(scalar, pb->v[i]);
    return r;
}

template<typename Op>
static Word dispatchArrayBinop(VM& vm, Op op, Word a, Word b,
                               Type* aType, Type* bType, ArrayType* resultAT) {
    Type* resultElem = resultAT->elemType_;
    auto* aAT = dynamic_cast<ArrayType*>(aType);
    auto* bAT = dynamic_cast<ArrayType*>(bType);

    if (aAT && bAT) {
        // Array op Array
        Type* elemA = aAT->elemType_;
        Type* elemB = bAT->elemType_;

        // Fast path: same element types = result element type (tight POD loops)
        if (elemA == resultElem && elemB == resultElem) {
            if constexpr (bool(Op::flags & mathOpIntArgs)) {
                if (resultElem == vm.intType())
                    return Word(static_cast<Obj*>(podArrayLoop(op,
                        static_cast<PodArray<i64>*>(a.o),
                        static_cast<PodArray<i64>*>(b.o), resultAT)));
            }
            if constexpr (bool(Op::flags & mathOpFloatArgs)) {
                if (resultElem == vm.floatType())
                    return Word(static_cast<Obj*>(podArrayLoop(op,
                        static_cast<PodArray<f64>*>(a.o),
                        static_cast<PodArray<f64>*>(b.o), resultAT)));
            }
        }

        // Generic path: per-element recursive dispatch
        usize lenA = arrayLen(a.o, elemA, vm);
        usize lenB = arrayLen(b.o, elemB, vm);
        usize len = std::min(lenA, lenB);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, elemA, vm);
            Word be = readElem(b.o, i, elemB, vm);
            Word re = dispatchBinop(vm, op, ae, be, elemA, elemB, resultElem);
            writeElem(result, i, re, resultElem, vm);
        }
        return Word(result);
    }

    if (aAT) {
        // Array op Scalar
        Type* elemA = aAT->elemType_;

        // Fast path: POD array with matching scalar type
        if (elemA == resultElem && bType == resultElem) {
            if constexpr (bool(Op::flags & mathOpIntArgs)) {
                if (resultElem == vm.intType())
                    return Word(static_cast<Obj*>(podArrayScalarLoop(op,
                        static_cast<PodArray<i64>*>(a.o), b.i, resultAT)));
            }
            if constexpr (bool(Op::flags & mathOpFloatArgs)) {
                if (resultElem == vm.floatType())
                    return Word(static_cast<Obj*>(podArrayScalarLoop(op,
                        static_cast<PodArray<f64>*>(a.o), b.f, resultAT)));
            }
        }

        // Generic path
        usize len = arrayLen(a.o, elemA, vm);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, elemA, vm);
            Word re = dispatchBinop(vm, op, ae, b, elemA, bType, resultElem);
            writeElem(result, i, re, resultElem, vm);
        }
        return Word(result);
    }

    // Scalar op Array
    Type* elemB = bAT->elemType_;

    // Fast path: POD scalar with matching array type
    if (aType == resultElem && elemB == resultElem) {
        if constexpr (bool(Op::flags & mathOpIntArgs)) {
            if (resultElem == vm.intType())
                return Word(static_cast<Obj*>(podScalarArrayLoop(op,
                    a.i, static_cast<PodArray<i64>*>(b.o), resultAT)));
        }
        if constexpr (bool(Op::flags & mathOpFloatArgs)) {
            if (resultElem == vm.floatType())
                return Word(static_cast<Obj*>(podScalarArrayLoop(op,
                    a.f, static_cast<PodArray<f64>*>(b.o), resultAT)));
        }
    }

    // Generic path
    usize len = arrayLen(b.o, elemB, vm);
    Obj* result = makeArray(vm, resultAT, len);
    for (usize i = 0; i < len; ++i) {
        Word be = readElem(b.o, i, elemB, vm);
        Word re = dispatchBinop(vm, op, a, be, aType, elemB, resultElem);
        writeElem(result, i, re, resultElem, vm);
    }
    return Word(result);
}

template<typename Op>
static Word dispatchTupleBinop(VM& vm, Op op, Word a, Word b,
                               Type* aType, Type* bType, TupleType* resultTT) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    usize n = resultTT->fields_.size();

    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = aTT ? static_cast<Tuple*>(a.o)->v[i] : a;
        Word be = bTT ? static_cast<Tuple*>(b.o)->v[i] : b;
        Type* aet = aTT ? aTT->fields_[i] : aType;
        Type* bet = bTT ? bTT->fields_[i] : bType;
        result->v[i] = dispatchBinop(vm, op, ae, be, aet, bet, resultTT->fields_[i]);
    }
    return Word(static_cast<Obj*>(result));
}

// --- List dispatch ---

template<typename Op>
static Word dispatchListBinop(VM& vm, Op op, Word a, Word b,
                              Type* aType, Type* bType, ListType* resultLT) {
    auto* listA = dynamic_cast<ListType*>(aType);
    auto* listB = dynamic_cast<ListType*>(bType);

    ListNode* srcA = listA ? static_cast<ListNode*>(a.o) : nullptr;
    ListNode* srcB = listB ? static_cast<ListNode*>(b.o) : nullptr;

    // Empty list check: nil list produces nil result
    if (listA && a.o == nullptr)
        return Word(static_cast<Obj*>(nullptr));
    if (listB && b.o == nullptr)
        return Word(static_cast<Obj*>(nullptr));

    // Determine the operation kind for the generator
    BinopListGen::OpKind opKind;
    if constexpr (std::is_same<Op, OpAdd>::value) opKind = BinopListGen::Add;
    else if constexpr (std::is_same<Op, OpSub>::value) opKind = BinopListGen::Sub;
    else if constexpr (std::is_same<Op, OpMul>::value) opKind = BinopListGen::Mul;
    else opKind = BinopListGen::Div;

    // Create lazy node with generator
    auto* node = new ListNode(resultLT);
    auto* gen = new BinopListGen(gCurrentVM->typeType());
    gen->opKind_ = opKind;
    gen->resultListType_ = resultLT;
    gen->resultElemType_ = resultLT->elemType_;

    if (srcA && srcB) {
        // List op List (zip)
        gen->leftList_ = srcA;
        gen->rightList_ = srcB;
        gen->leftElemType_ = listA->elemType_;
        gen->rightElemType_ = listB->elemType_;
        gen->broadcastIsLeft_ = false;
        gen->broadcastValIsObj_ = false;
    } else if (srcA) {
        // List op Scalar
        gen->leftList_ = srcA;
        gen->rightList_ = nullptr;
        gen->leftElemType_ = listA->elemType_;
        gen->rightElemType_ = bType;
        gen->broadcastVal_ = b;
        gen->broadcastIsLeft_ = false;  // scalar is on the right
        gen->broadcastValIsObj_ = storesObjPtr(bType);
    } else {
        // Scalar op List
        gen->leftList_ = nullptr;
        gen->rightList_ = srcB;
        gen->leftElemType_ = aType;
        gen->rightElemType_ = listB->elemType_;
        gen->broadcastVal_ = a;
        gen->broadcastIsLeft_ = true;   // scalar is on the left
        gen->broadcastValIsObj_ = storesObjPtr(aType);
    }

    node->generator_ = gen;
    if (gen->leftList_) gen->leftList_->retain();
    if (gen->rightList_) gen->rightList_->retain();
    if (gen->broadcastValIsObj_ && gen->broadcastVal_.o) gen->broadcastVal_.o->retain();
    reinterpret_cast<GCObj*>(gen)->retain();
    return Word(static_cast<Obj*>(node));
}

template<typename Op>
static Word dispatchBinop(VM& vm, Op op, Word a, Word b,
                          Type* aType, Type* bType, Type* resultType) {
    int rank = numRank(vm, resultType);
    switch (rank) {
        case 0: // Int/Bool
            if constexpr (bool(Op::flags & mathOpIntArgs)) {
                return Word(op(a.i, b.i));
            }
            break;
        case 1: // Fraction
            if constexpr (bool(Op::flags & mathOpFractionArgs)) {
                r64 ar = toR64(vm, a, aType);
                r64 br = toR64(vm, b, bType);
                return Word(static_cast<Obj*>(new Fraction(op(ar, br))));
            }
            break;
        case 2: // Float
            if constexpr (bool(Op::flags & mathOpFloatArgs)) {
                f64 af = toF64(vm, a, aType);
                f64 bf = toF64(vm, b, bType);
                return Word(op(af, bf));
            }
            break;
        case 3: // Complex
            if constexpr (bool(Op::flags & mathOpComplexArgs)) {
                x64 ax = toX64(vm, a, aType);
                x64 bx = toX64(vm, b, bType);
                return Word(static_cast<Obj*>(new Complex(op(ax, bx))));
            }
            break;
        case 4: // Array
            return dispatchArrayBinop(vm, op, a, b, aType, bType,
                                      static_cast<ArrayType*>(resultType));
        case 5: // Tuple
            return dispatchTupleBinop(vm, op, a, b, aType, bType,
                                     static_cast<TupleType*>(resultType));
        case 6: // List
            return dispatchListBinop(vm, op, a, b, aType, bType,
                                     static_cast<ListType*>(resultType));
    }
    return Word((i64)0);
}

// --- Templated unary dispatch ---

template<typename Op>
static Word dispatchUnaryOp(VM& vm, Op op, Word a, Type* aType, Type* resultType);

template<typename Op>
static Word dispatchArrayUnaryOp(VM& vm, Op op, Word a, Type* aType, ArrayType* resultAT) {
    Type* resultElem = resultAT->elemType_;
    auto* aAT = static_cast<ArrayType*>(aType);
    Type* elemA = aAT->elemType_;
    usize len = arrayLen(a.o, elemA, vm);

    // Fast path: same POD element type (tight loop, op inlined)
    if (elemA == resultElem) {
        if constexpr (bool(Op::flags & mathOpIntArgs)) {
            if (resultElem == vm.intType()) {
                auto* pa = static_cast<PodArray<i64>*>(a.o);
                auto* r = new PodArray<i64>(resultAT);
                r->v.resize(len);
                for (usize i = 0; i < len; ++i)
                    r->v[i] = op(pa->v[i]);
                return Word(static_cast<Obj*>(r));
            }
        }
        if constexpr (bool(Op::flags & mathOpFloatArgs)) {
            if (resultElem == vm.floatType()) {
                auto* pa = static_cast<PodArray<f64>*>(a.o);
                auto* r = new PodArray<f64>(resultAT);
                r->v.resize(len);
                for (usize i = 0; i < len; ++i)
                    r->v[i] = op(pa->v[i]);
                return Word(static_cast<Obj*>(r));
            }
        }
    }

    // Generic path: per-element recursive dispatch
    Obj* result = makeArray(vm, resultAT, len);
    for (usize i = 0; i < len; ++i) {
        Word ae = readElem(a.o, i, elemA, vm);
        Word re = dispatchUnaryOp(vm, op, ae, elemA, resultElem);
        writeElem(result, i, re, resultElem, vm);
    }
    return Word(result);
}

template<typename Op>
static Word dispatchTupleUnaryOp(VM& vm, Op op, Word a, Type* aType, TupleType* resultTT) {
    auto* aTT = static_cast<TupleType*>(aType);
    usize n = resultTT->fields_.size();
    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = static_cast<Tuple*>(a.o)->v[i];
        result->v[i] = dispatchUnaryOp(vm, op, ae, aTT->fields_[i], resultTT->fields_[i]);
    }
    return Word(static_cast<Obj*>(result));
}

template<typename Op>
static Word dispatchListUnaryOp(VM& vm, Op op, Word a, Type* aType, ListType* resultLT) {
    auto* listA = dynamic_cast<ListType*>(aType);
    ListNode* srcA = listA ? static_cast<ListNode*>(a.o) : nullptr;

    // Empty list check
    if (listA && a.o == nullptr)
        return Word(static_cast<Obj*>(nullptr));

    // Determine the operation kind for the generator
    UnaryListGen::OpKind opKind;
    if constexpr (std::is_same<Op, OpNeg>::value) opKind = UnaryListGen::Neg;
    else if constexpr (std::is_same<Op, OpNot>::value) opKind = UnaryListGen::Not;
    else if constexpr (std::is_same<Op, OpBitNot>::value) opKind = UnaryListGen::BitNot;
    else opKind = UnaryListGen::Neg;

    // Create lazy node with generator
    auto* node = new ListNode(resultLT);
    auto* gen = new UnaryListGen(gCurrentVM->typeType());
    gen->opKind_ = opKind;
    gen->source_ = srcA;
    gen->sourceElemType_ = listA->elemType_;
    gen->resultElemType_ = resultLT->elemType_;
    gen->resultListType_ = resultLT;

    node->generator_ = gen;
    gen->source_->retain();
    reinterpret_cast<GCObj*>(gen)->retain();
    return Word(static_cast<Obj*>(node));
}

template<typename Op>
static Word dispatchUnaryOp(VM& vm, Op op, Word a, Type* aType, Type* resultType) {
    int rank = numRank(vm, resultType);
    switch (rank) {
        case 0: // Int/Bool
            if constexpr (bool(Op::flags & mathOpIntArgs)) {
                return Word(op(a.i));
            }
            break;
        case 1: // Fraction
            if constexpr (bool(Op::flags & mathOpFractionArgs)) {
                r64 ar = toR64(vm, a, aType);
                return Word(static_cast<Obj*>(new Fraction(op(ar))));
            }
            break;
        case 2: // Float
            if constexpr (bool(Op::flags & mathOpFloatArgs)) {
                f64 af = toF64(vm, a, aType);
                return Word(op(af));
            }
            break;
        case 3: // Complex
            if constexpr (bool(Op::flags & mathOpComplexArgs)) {
                x64 ax = toX64(vm, a, aType);
                return Word(static_cast<Obj*>(new Complex(op(ax))));
            }
            break;
        case 4: // Array
            return dispatchArrayUnaryOp(vm, op, a, aType,
                                        static_cast<ArrayType*>(resultType));
        case 5: // Tuple
            return dispatchTupleUnaryOp(vm, op, a, aType,
                                       static_cast<TupleType*>(resultType));
        case 6: // List
            return dispatchListUnaryOp(vm, op, a, aType,
                                       static_cast<ListType*>(resultType));
    }
    return Word((i64)0);
}

// --- Composite Arithmetic Opcodes ---
// Each opcode instantiates the dispatch template with the specific operator struct.
// The compiler generates separate, optimized code for each operator.

// ADD_COMPOSITE Rd, Ra, Rb (5 words: op, regs, resultType*, aType*, bType*)
void op_add_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = dispatchBinop(vm, OpAdd{}, vm.reg(a), vm.reg(b), aType, bType, resultType);
    DISPATCH(5);
}

void op_sub_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = dispatchBinop(vm, OpSub{}, vm.reg(a), vm.reg(b), aType, bType, resultType);
    DISPATCH(5);
}

void op_mul_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = dispatchBinop(vm, OpMul{}, vm.reg(a), vm.reg(b), aType, bType, resultType);
    DISPATCH(5);
}

void op_div_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = dispatchBinop(vm, OpDiv{}, vm.reg(a), vm.reg(b), aType, bType, resultType);
    DISPATCH(5);
}

// NEG_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_neg_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = dispatchUnaryOp(vm, OpNeg{}, vm.reg(a), aType, resultType);
    DISPATCH(4);
}

// NOT_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_not_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = dispatchUnaryOp(vm, OpNot{}, vm.reg(a), aType, resultType);
    DISPATCH(4);
}

// BITNOT_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_bitnot_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = dispatchUnaryOp(vm, OpBitNot{}, vm.reg(a), aType, resultType);
    DISPATCH(4);
}

// --- Composite Comparison Opcodes ---
// Unlike arithmetic dispatch (which dispatches on result type), comparison dispatch
// dispatches on INPUT types at the scalar level since result is always Bool.

struct OpCmpEq { bool operator()(i64 a, i64 b) const { return a == b; } bool operator()(f64 a, f64 b) const { return a == b; } bool operator()(r64 a, r64 b) const { return a == b; } };
struct OpCmpNe { bool operator()(i64 a, i64 b) const { return a != b; } bool operator()(f64 a, f64 b) const { return a != b; } bool operator()(r64 a, r64 b) const { return a != b; } };
struct OpCmpLt { bool operator()(i64 a, i64 b) const { return a < b; } bool operator()(f64 a, f64 b) const { return a < b; } bool operator()(r64 a, r64 b) const { return a < b; } };
struct OpCmpLe { bool operator()(i64 a, i64 b) const { return a <= b; } bool operator()(f64 a, f64 b) const { return a <= b; } bool operator()(r64 a, r64 b) const { return a <= b; } };
struct OpCmpGt { bool operator()(i64 a, i64 b) const { return a > b; } bool operator()(f64 a, f64 b) const { return a > b; } bool operator()(r64 a, r64 b) const { return a > b; } };
struct OpCmpGe { bool operator()(i64 a, i64 b) const { return a >= b; } bool operator()(f64 a, f64 b) const { return a >= b; } bool operator()(r64 a, r64 b) const { return a >= b; } };

template<typename CmpOp>
static Word dispatchCmpBinop(VM& vm, CmpOp op, Word a, Word b,
                             Type* aType, Type* bType, Type* resultType);

template<typename CmpOp>
static Word dispatchCmpTupleBinop(VM& vm, CmpOp op, Word a, Word b,
                                  Type* aType, Type* bType, TupleType* resultTT) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    usize n = resultTT->fields_.size();
    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = aTT ? static_cast<Tuple*>(a.o)->v[i] : a;
        Word be = bTT ? static_cast<Tuple*>(b.o)->v[i] : b;
        Type* aet = aTT ? aTT->fields_[i] : aType;
        Type* bet = bTT ? bTT->fields_[i] : bType;
        result->v[i] = dispatchCmpBinop(vm, op, ae, be, aet, bet, resultTT->fields_[i]);
    }
    return Word(static_cast<Obj*>(result));
}

template<typename CmpOp>
static Word dispatchCmpArrayBinop(VM& vm, CmpOp op, Word a, Word b,
                                  Type* aType, Type* bType, ArrayType* resultAT) {
    Type* resultElem = resultAT->elemType_;
    auto* aAT = dynamic_cast<ArrayType*>(aType);
    auto* bAT = dynamic_cast<ArrayType*>(bType);

    if (aAT && bAT) {
        usize len = std::min(arrayLen(a.o, aAT->elemType_, vm), arrayLen(b.o, bAT->elemType_, vm));
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, aAT->elemType_, vm);
            Word be = readElem(b.o, i, bAT->elemType_, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, ae, be, aAT->elemType_, bAT->elemType_, resultElem), resultElem, vm);
        }
        return Word(result);
    }
    if (aAT) {
        usize len = arrayLen(a.o, aAT->elemType_, vm);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, aAT->elemType_, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, ae, b, aAT->elemType_, bType, resultElem), resultElem, vm);
        }
        return Word(result);
    }
    if (bAT) {
        usize len = arrayLen(b.o, bAT->elemType_, vm);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word be = readElem(b.o, i, bAT->elemType_, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, a, be, aType, bAT->elemType_, resultElem), resultElem, vm);
        }
        return Word(result);
    }
    return Word((i64)0);
}

template<typename CmpOp>
static Word dispatchCmpBinop(VM& vm, CmpOp op, Word a, Word b,
                             Type* aType, Type* bType, Type* resultType) {
    if (auto* resultAT = dynamic_cast<ArrayType*>(resultType))
        return dispatchCmpArrayBinop(vm, op, a, b, aType, bType, resultAT);
    if (auto* resultTT = dynamic_cast<TupleType*>(resultType))
        return dispatchCmpTupleBinop(vm, op, a, b, aType, bType, resultTT);
    // Scalar: dispatch on max input rank
    int rA = numRank(vm, aType);
    int rB = numRank(vm, bType);
    int maxR = std::max(rA, rB);
    switch (maxR) {
        case 0: return Word((i64)(op(a.i, b.i) ? 1 : 0));
        case 1: return Word((i64)(op(toR64(vm, a, aType), toR64(vm, b, bType)) ? 1 : 0));
        case 2: return Word((i64)(op(toF64(vm, a, aType), toF64(vm, b, bType)) ? 1 : 0));
        default: return Word((i64)0);
    }
}

// CMP_XX_COMPOSITE Rd, Ra, Rb (5 words: op, regs, resultType*, aType*, bType*)
void op_cmp_eq_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpEq{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_ne_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpNe{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_lt_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpLt{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_le_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpLe{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_gt_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpGt{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_ge_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = dispatchCmpBinop(vm, OpCmpGe{}, vm.reg(a), vm.reg(b),
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}

// --- List Generator Implementations ---

// Helper: dispatch a binary op by OpKind enum at runtime
static Word dispatchBinopByKind(VM& vm, BinopListGen::OpKind kind,
                                Word a, Word b, Type* aType, Type* bType, Type* resultType) {
    switch (kind) {
        case BinopListGen::Add: return dispatchBinop(vm, OpAdd{}, a, b, aType, bType, resultType);
        case BinopListGen::Sub: return dispatchBinop(vm, OpSub{}, a, b, aType, bType, resultType);
        case BinopListGen::Mul: return dispatchBinop(vm, OpMul{}, a, b, aType, bType, resultType);
        case BinopListGen::Div: return dispatchBinop(vm, OpDiv{}, a, b, aType, bType, resultType);
    }
    return Word((i64)0);
}

void BinopListGen::generate(VM& vm, ListNode* owner) {
    // Force source nodes if they are lazy
    if (leftList_) leftList_->force(vm);
    if (rightList_) rightList_->force(vm);

    // Determine head operands
    Word leftVal, rightVal;
    Type* leftType = leftElemType_;
    Type* rightType = rightElemType_;

    if (leftList_ && rightList_) {
        // Zip: both are lists
        leftVal = leftList_->head_;
        rightVal = rightList_->head_;
    } else if (leftList_) {
        // List op Scalar: list is left, scalar is broadcast (right)
        leftVal = leftList_->head_;
        rightVal = broadcastVal_;
        rightType = rightElemType_;
    } else {
        // Scalar op List: scalar is broadcast (left), list is right
        leftVal = broadcastVal_;
        rightVal = rightList_->head_;
        leftType = leftElemType_;
    }

    // Compute the head value
    owner->head_ = dispatchBinopByKind(vm, opKind_, leftVal, rightVal,
                                        leftType, rightType, resultElemType_);

    // Compute the tail: advance list source(s)
    ListNode* nextLeft = leftList_ ? leftList_->tail_ : nullptr;
    ListNode* nextRight = rightList_ ? rightList_->tail_ : nullptr;

    // Check if we've reached the end of any list
    bool atEnd = false;
    if (leftList_ && rightList_) {
        // Zip: end when either list ends
        atEnd = (nextLeft == nullptr || nextRight == nullptr);
    } else if (leftList_) {
        atEnd = (nextLeft == nullptr);
    } else {
        atEnd = (nextRight == nullptr);
    }

    if (storesObjPtr(resultElemType_) && owner->head_.o) owner->head_.o->retain();

    if (atEnd) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = new ListNode(resultListType_);
        // Retain new, release old for field mutations
        if (nextLeft) nextLeft->retain();
        if (leftList_) leftList_->release();
        leftList_ = nextLeft;
        if (nextRight) nextRight->retain();
        if (rightList_) rightList_->release();
        rightList_ = nextRight;
        // Retain generator and tail for owner
        tailNode->generator_ = this;
        reinterpret_cast<GCObj*>(this)->retain();
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

void UnaryListGen::generate(VM& vm, ListNode* owner) {
    // Force source node if lazy
    source_->force(vm);

    // Compute head via the appropriate unary operation
    switch (opKind_) {
        case Neg:
            owner->head_ = dispatchUnaryOp(vm, OpNeg{}, source_->head_,
                                            sourceElemType_, resultElemType_);
            break;
        case Not:
            owner->head_ = dispatchUnaryOp(vm, OpNot{}, source_->head_,
                                            sourceElemType_, resultElemType_);
            break;
        case BitNot:
            owner->head_ = dispatchUnaryOp(vm, OpBitNot{}, source_->head_,
                                            sourceElemType_, resultElemType_);
            break;
    }
    if (storesObjPtr(resultElemType_) && owner->head_.o) owner->head_.o->retain();

    // Create lazy tail
    ListNode* nextSource = source_->tail_;
    if (nextSource == nullptr) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = new ListNode(resultListType_);
        if (nextSource) nextSource->retain();
        if (source_) source_->release();
        source_ = nextSource;
        tailNode->generator_ = this;
        reinterpret_cast<GCObj*>(this)->retain();
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

void RangeListGen::generate(VM& vm, ListNode* owner) {
    // Check if we've gone past the end (for finite ranges)
    bool pastEnd = false;
    if (!isInfinite_) {
        if (step_ > 0) pastEnd = (current_ > end_);
        else if (step_ < 0) pastEnd = (current_ < end_);
        else pastEnd = true;  // step == 0: degenerate
    }

    if (pastEnd) {
        // Shouldn't normally reach here because we don't create a generator
        // node past the end, but handle gracefully
        owner->head_.i = 0;
        owner->tail_ = nullptr;
        return;
    }

    // Set head to current value
    owner->head_.i = current_;

    // Compute next value and check if there will be more elements
    i64 next = current_ + step_;
    bool nextPastEnd = false;
    if (!isInfinite_) {
        if (step_ > 0) nextPastEnd = (next > end_);
        else if (step_ < 0) nextPastEnd = (next < end_);
        else nextPastEnd = true;
    }

    if (nextPastEnd) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = new ListNode(listType_);
        current_ = next;
        tailNode->generator_ = this;
        reinterpret_cast<GCObj*>(this)->retain();
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

void FractionRangeListGen::generate(VM& vm, ListNode* owner) {
    r64 cur = current_->r;
    r64 stp = step_->r;

    // Check if we've gone past the end (for finite ranges)
    bool pastEnd = false;
    if (!isInfinite_) {
        r64 e = end_->r;
        if (stp > r64(0)) pastEnd = (cur > e);
        else if (stp < r64(0)) pastEnd = (cur < e);
        else pastEnd = true;
    }

    if (pastEnd) {
        owner->head_.o = nullptr;
        owner->tail_ = nullptr;
        return;
    }

    // Set head to current value
    owner->head_.o = current_;
    if (current_) current_->retain();

    // Compute next value and check if there will be more elements
    r64 next = cur + stp;
    bool nextPastEnd = false;
    if (!isInfinite_) {
        r64 e = end_->r;
        if (stp > r64(0)) nextPastEnd = (next > e);
        else if (stp < r64(0)) nextPastEnd = (next < e);
        else nextPastEnd = true;
    }

    if (nextPastEnd) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = new ListNode(listType_);
        auto* oldCurrent = current_;
        current_ = new Fraction(next);
        current_->retain();
        if (oldCurrent) oldCurrent->release();
        tailNode->generator_ = this;
        reinterpret_cast<GCObj*>(this)->retain();
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

// --- Tuple Access/Construction ---

// MAKE_ARRAY Rd, firstSrc, numElems (3 words: op, regs{dst, firstSrc, numElems}, ArrayType*)
void op_make_array(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numElems = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = new PodArray<i64>(arrayType);
        arr->v.resize(numElems);
        for (u16 i = 0; i < numElems; ++i)
            arr->v[i] = vm.reg(firstSrc + i).i;
        vm.reg(dst).o = arr;
    } else if (storesF64(elemType)) {
        auto* arr = new PodArray<f64>(arrayType);
        arr->v.resize(numElems);
        for (u16 i = 0; i < numElems; ++i)
            arr->v[i] = vm.reg(firstSrc + i).f;
        vm.reg(dst).o = arr;
    } else {
        auto* arr = new ObjArray(arrayType);
        arr->reserve(numElems);
        for (u16 i = 0; i < numElems; ++i) {
            arr->push(vm.reg(firstSrc + i).o);
        }
        vm.reg(dst).o = arr;
    }
    DISPATCH(3);
}

// TUPLE_GET Rd, Ra, fieldIdx (2 words: op, regs{dst, src, fieldIdx})
void op_tuple_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* tuple = static_cast<Tuple*>(vm.reg(src).o);
    vm.reg(dst) = tuple->v[fieldIdx];
    DISPATCH(2);
}

// TUPLE_SLICE Rd, Ra, startIdx (3 words: op, regs{dst, src, startIdx}, TupleType*)
// Creates a new tuple from elements [startIdx..end) of the source tuple
void op_tuple_slice(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], startIdx = pc[1].regs[2];
    auto* resultType = static_cast<TupleType*>(pc[2].p);
    auto* srcTuple = static_cast<Tuple*>(vm.reg(src).o);
    u32 count = srcTuple->numFields_ - startIdx;
    auto* newTuple = Tuple::create(resultType, count);
    for (size_t i = 0; i < count; ++i) {
        newTuple->v[i] = srcTuple->v[startIdx + i];
    }
    // Retain Obj* fields
    for (auto idx : resultType->gcFields_) {
        if (newTuple->v[idx].o) newTuple->v[idx].o->retain();
    }
    vm.reg(dst).o = newTuple;
    DISPATCH(3);
}

// MAKE_TUPLE Rd, firstSrc, numFields (3 words: op, regs{dst, firstSrc, numFields}, TupleType*)
void op_make_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numFields = pc[1].regs[2];
    auto* tupleType = static_cast<TupleType*>(pc[2].p);
    auto* tuple = Tuple::create(tupleType, numFields);
    for (u16 i = 0; i < numFields; ++i) {
        tuple->v[i] = vm.reg(firstSrc + i);
    }
    // Retain Obj* fields so they survive the auto-release pool drain
    for (auto idx : tupleType->gcFields_) {
        if (tuple->v[idx].o) tuple->v[idx].o->retain();
    }
    vm.reg(dst).o = tuple;
    DISPATCH(3);
}

// MAKE_STRUCT Rd, firstSrc, numFields (3 words: op, regs{dst, firstSrc, numFields}, StructType*)
void op_make_struct(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numFields = pc[1].regs[2];
    auto* structType = static_cast<StructType*>(pc[2].p);
    auto* s = Struct::create(structType, numFields);
    for (u16 i = 0; i < numFields; ++i) {
        s->v[i] = vm.reg(firstSrc + i);
    }
    // Retain Obj* fields so they survive the auto-release pool drain
    for (auto idx : structType->gcFields_) {
        if (s->v[idx].o) s->v[idx].o->retain();
    }
    vm.reg(dst).o = s;
    DISPATCH(3);
}

// STRUCT_GET Rd, Ra, fieldIdx (2 words: op, regs{dst, src, fieldIdx})
void op_struct_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* s = static_cast<Struct*>(vm.reg(src).o);
    vm.reg(dst) = s->v[fieldIdx];
    DISPATCH(2);
}

// MAKE_ENUM Rd, valSrc, caseIdx (3 words: op, regs{dst, valSrc, caseIdx}, EnumType*)
void op_make_enum(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valSrc = pc[1].regs[1], caseIdx = pc[1].regs[2];
    auto* enumType = static_cast<EnumType*>(pc[2].p);
    auto* e = new Enum(enumType);
    e->which_ = caseIdx;
    e->word_ = vm.reg(valSrc);
    if (enumType->gcCases_[caseIdx] && e->word_.o) e->word_.o->retain();
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// MAKE_ENUM_NODATA Rd, caseIdx (3 words: op, regs{dst, caseIdx}, EnumType*)
void op_make_enum_nodata(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], caseIdx = pc[1].regs[1];
    auto* enumType = static_cast<EnumType*>(pc[2].p);
    auto* e = new Enum(enumType);
    e->which_ = caseIdx;
    e->word_.i = 0;
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// ENUM_GET_WHICH Rd, Ra (2 words: op, regs{dst, src})
void op_enum_get_which(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* e = static_cast<Enum*>(vm.reg(src).o);
    vm.reg(dst).i = e->which_;
    DISPATCH(2);
}

// ENUM_GET_VALUE Rd, Ra (2 words: op, regs{dst, src})
void op_enum_get_value(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* e = static_cast<Enum*>(vm.reg(src).o);
    vm.reg(dst) = e->word_;
    DISPATCH(2);
}

// --- Dynamic Array Operations (for auto-mapping) ---

// ARRAY_ALLOC Rd, Rn (3 words: op, regs{dst, len_reg}, ArrayType*)
// Allocate an array of runtime-determined length, initialized to zero
void op_array_alloc(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], lenReg = pc[1].regs[1];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;
    i64 len = vm.reg(lenReg).i;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = new PodArray<i64>(arrayType);
        arr->v.resize(len);
        vm.reg(dst).o = arr;
    } else if (storesF64(elemType)) {
        auto* arr = new PodArray<f64>(arrayType);
        arr->v.resize(len);
        vm.reg(dst).o = arr;
    } else {
        auto* arr = new ObjArray(arrayType);
        arr->resize(len);
        vm.reg(dst).o = arr;
    }
    DISPATCH(3);
}

// ARRAY_SET Ra, Rb_idx, Rc_val (3 words: op, regs{arr, idx_reg, val_reg}, ArrayType*)
// Set array element at runtime index
void op_array_set(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;
    i64 idx = vm.reg(idxReg).i;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = static_cast<PodArray<i64>*>(vm.reg(arrReg).o);
        arr->v[cyclicIndex(idx, arr->v.size())] = vm.reg(valReg).i;
    } else if (storesF64(elemType)) {
        auto* arr = static_cast<PodArray<f64>*>(vm.reg(arrReg).o);
        arr->v[cyclicIndex(idx, arr->v.size())] = vm.reg(valReg).f;
    } else {
        auto* arr = static_cast<ObjArray*>(vm.reg(arrReg).o);
        arr->set(cyclicIndex(idx, arr->size()), vm.reg(valReg).o);
    }
    DISPATCH(3);
}

// ARRAY_GET_DYN Rd, Ra, Rb_idx (3 words: op, regs{dst, arr, idx_reg}, ArrayType*)
// Get array element at runtime index (index from register, not immediate)
void op_array_get_dyn(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;
    i64 idx = vm.reg(idxReg).i;

    if (elemType && !storesObjPtr(elemType) && !storesF64(elemType)) {
        auto* arr = static_cast<PodArray<i64>*>(vm.reg(arrReg).o);
        vm.reg(dst).i = arr->v[cyclicIndex(idx, arr->v.size())];
    } else if (storesF64(elemType)) {
        auto* arr = static_cast<PodArray<f64>*>(vm.reg(arrReg).o);
        vm.reg(dst).f = arr->v[cyclicIndex(idx, arr->v.size())];
    } else {
        auto* arr = static_cast<ObjArray*>(vm.reg(arrReg).o);
        vm.reg(dst).o = arr->get(cyclicIndex(idx, arr->size()));
    }
    DISPATCH(3);
}

// --- List ---

// CONS Rd, Rhead, Rtail (3 words: op, regs{dst, head, tail}, ListType*)
void op_cons(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], head = pc[1].regs[1], tail = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);

    auto* node = new ListNode(listType);
    node->head_ = vm.reg(head);
    node->tail_ = static_cast<ListNode*>(vm.reg(tail).o);
    // Retain stored Obj* fields
    if (storesObjPtr(listType->elemType_) && node->head_.o) node->head_.o->retain();
    if (node->tail_) node->tail_->retain();
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// MAKE_LIST Rd, firstSrc, count (3 words: op, regs{dst, firstSrc, count}, ListType*)
// Builds list from N consecutive registers (cons from right to left)
void op_make_list(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], count = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);

    bool elemIsObj = storesObjPtr(listType->elemType_);
    ListNode* result = nullptr;
    for (int i = (int)count - 1; i >= 0; --i) {
        auto* node = new ListNode(listType);
        node->head_ = vm.reg(firstSrc + (u16)i);
        node->tail_ = result;
        if (elemIsObj && node->head_.o) node->head_.o->retain();
        if (node->tail_) node->tail_->retain();
        result = node;
    }
    vm.reg(dst).o = result;
    DISPATCH(3);
}

// LIST_HEAD Rd, Ra (2 words)
void op_list_head(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* node = static_cast<ListNode*>(vm.reg(src).o);
    node->force(vm);
    vm.reg(dst) = node->head_;
    DISPATCH(2);
}

// LIST_TAIL Rd, Ra (2 words)
void op_list_tail(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* node = static_cast<ListNode*>(vm.reg(src).o);
    node->force(vm);
    vm.reg(dst).o = node->tail_;
    DISPATCH(2);
}

// LIST_IS_NIL Rd, Ra (2 words) - sets dst to 1 if src is null, 0 otherwise
void op_list_is_nil(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (vm.reg(src).o == nullptr) ? 1 : 0;
    DISPATCH(2);
}

// --- Lambda ---

// MAKE_LAMBDA Rd, captureBase, numFreeVars (3 words: op, regs, LambdaType*)
void op_make_lambda(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], captureBase = pc[1].regs[1], numFreeVars = pc[1].regs[2];
    auto* lambdaType = static_cast<LambdaType*>(pc[2].p);

    auto* lambda = Lambda::create(lambdaType, numFreeVars);
    // codeBlock_ already set by Lambda constructor from lambdaType->codeBlock_
    for (u16 i = 0; i < numFreeVars; ++i) {
        lambda->freeVars_[i] = vm.reg(captureBase + i);
    }
    // Retain Obj* free variables so they survive the auto-release pool drain
    const auto& gcFreeVars = lambda->getGCFreeVars();
    for (auto idx : gcFreeVars) {
        if (lambda->freeVars_[idx].o) lambda->freeVars_[idx].o->retain();
    }
    vm.reg(dst).o = lambda;
    DISPATCH(3);
}

// CALL_LAMBDA Rd, argc, argBase, calleeReg (2 words: op, regs)
void op_call_lambda(VM& vm, Code* pc) {
    u16 resultReg = pc[1].regs[0];
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u16 calleeReg = pc[1].regs[3];

    // Read Lambda before pushFrame changes the base register
    auto* lambda = static_cast<Lambda*>(vm.reg(calleeReg).o);
    CodeBlock* callee = lambda->codeBlock_;

    u32 newBase = vm.baseReg() + argBase;

    // Push call frame
    Code* returnPC = pc + 2;
    vm.pushFrame(returnPC, callee, newBase, callee->numRegs, resultReg);

    // Copy free vars into callee's registers right after the args
    // Use numArgs (total param count) instead of argc so defaults don't clobber free vars
    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        vm.reg(callee->numArgs + i) = lambda->freeVars_[i];
    }

    // Jump to the appropriate entry point (handle default arguments)
    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// FUNC_REF Rd (3 words: op, regs, LambdaType*)
// Wraps a named function's CodeBlock in a Lambda with 0 captures
void op_func_ref(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    auto* lambdaType = static_cast<LambdaType*>(pc[2].p);

    auto* lambda = Lambda::create(lambdaType, 0);
    // codeBlock_ already set by Lambda constructor from lambdaType->codeBlock_
    vm.reg(dst).o = lambda;
    DISPATCH(3);
}

// --- Template Lambda ---

// MAKE_TEMPLATE_LAMBDA Rd, captureBase, numFreeVars (3 words: op, regs, TemplateLambdaType*)
void op_make_template_lambda(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], captureBase = pc[1].regs[1], numFreeVars = pc[1].regs[2];
    auto* tmplType = static_cast<TemplateLambdaType*>(pc[2].p);

    auto* lambda = Lambda::create(tmplType, numFreeVars);
    for (u16 i = 0; i < numFreeVars; ++i) {
        lambda->freeVars_[i] = vm.reg(captureBase + i);
    }
    // Retain Obj* free variables so they survive the auto-release pool drain
    const auto& gcFreeVars = lambda->getGCFreeVars();
    for (auto idx : gcFreeVars) {
        if (lambda->freeVars_[idx].o) lambda->freeVars_[idx].o->retain();
    }
    vm.reg(dst).o = lambda;
    DISPATCH(3);
}

// CALL_TEMPLATE_LAMBDA Rd, argc, argBase, calleeReg (3 words: op, regs, CodeBlock*)
// Like op_call_lambda but reads CodeBlock from instruction stream instead of Lambda object
void op_call_template_lambda(VM& vm, Code* pc) {
    u16 resultReg = pc[1].regs[0];
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u16 calleeReg = pc[1].regs[3];

    auto* lambda = static_cast<Lambda*>(vm.reg(calleeReg).o);
    CodeBlock* callee = static_cast<CodeBlock*>(pc[2].p);  // from instruction, NOT lambda

    u32 newBase = vm.baseReg() + argBase;
    Code* returnPC = pc + 3;
    vm.pushFrame(returnPC, callee, newBase, callee->numRegs, resultReg);

    // Copy free vars into callee's registers after the args
    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        vm.reg(callee->numArgs + i) = lambda->freeVars_[i];
    }

    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// TAIL_CALL_TEMPLATE_LAMBDA unused, argc, argBase, calleeReg (3 words: op, regs, CodeBlock*)
void op_tail_call_template_lambda(VM& vm, Code* pc) {
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u16 calleeReg = pc[1].regs[3];

    auto* lambda = static_cast<Lambda*>(vm.reg(calleeReg).o);
    CodeBlock* callee = static_cast<CodeBlock*>(pc[2].p);

    for (u16 i = 0; i < argc; i++) {
        vm.reg(i) = vm.reg(argBase + i);
    }

    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        vm.reg(callee->numArgs + i) = lambda->freeVars_[i];
    }

    vm.updateCurrentCodeBlock(callee);
    vm.growCurrentFrameNumRegs(callee->numRegs);

    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// SPECIALIZE_LAMBDA Rd, srcReg (3 words: op, regs, LambdaType*)
// Creates a concrete Lambda by copying captures from a template Lambda
void op_specialize_lambda(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u16 src = pc[1].regs[1];
    auto* newType = static_cast<LambdaType*>(pc[2].p);

    auto* srcLambda = static_cast<Lambda*>(vm.reg(src).o);
    auto* newLambda = Lambda::create(newType, srcLambda->numFreeVars_);
    for (u16 i = 0; i < srcLambda->numFreeVars_; ++i) {
        newLambda->freeVars_[i] = srcLambda->freeVars_[i];
    }
    // Retain Obj* free variables so they survive the auto-release pool drain
    const auto& gcFreeVars = newLambda->getGCFreeVars();
    for (auto idx : gcFreeVars) {
        if (newLambda->freeVars_[idx].o) newLambda->freeVars_[idx].o->retain();
    }
    vm.reg(dst).o = newLambda;
    DISPATCH(3);
}

// --- Tail Calls ---

// TAIL_CALL unused, argc, argBase, callee_global (3 words: op, regs, global_idx)
// Reuses the current call frame: copies args to r0..r(argc-1), jumps to callee.
void op_tail_call(VM& vm, Code* pc) {
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u32 calleeIdx = (u32)pc[2].i;

    CodeBlock* callee = static_cast<CodeBlock*>(vm.global(calleeIdx).p);

    // Copy args to r0..r(argc-1) — forward copy is safe since argBase >= argc
    for (u16 i = 0; i < argc; i++) {
        vm.reg(i) = vm.reg(argBase + i);
    }

    // Update current frame's codeBlock (for GC reachability and op_load_obj).
    // Also grow numRegs to fit the callee's needs -- otherwise calling a
    // larger callee from a smaller caller writes into adjacent frame slots.
    vm.updateCurrentCodeBlock(callee);
    vm.growCurrentFrameNumRegs(callee->numRegs);

    // Jump to the appropriate entry point (handle default arguments)
    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// TAIL_CALL_LAMBDA unused, argc, argBase, calleeReg (2 words: op, regs)
// Tail-calls a lambda: copies args and free vars, reuses current frame.
void op_tail_call_lambda(VM& vm, Code* pc) {
    u16 argc = pc[1].regs[1];
    u16 argBase = pc[1].regs[2];
    u16 calleeReg = pc[1].regs[3];

    // Read lambda before copying args (calleeReg may be overwritten)
    auto* lambda = static_cast<Lambda*>(vm.reg(calleeReg).o);
    CodeBlock* callee = lambda->codeBlock_;

    // Copy args to r0..r(argc-1)
    for (u16 i = 0; i < argc; i++) {
        vm.reg(i) = vm.reg(argBase + i);
    }

    // Copy free vars after the params
    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        vm.reg(callee->numArgs + i) = lambda->freeVars_[i];
    }

    // Update current frame's codeBlock and ensure its reg window fits the callee
    vm.updateCurrentCodeBlock(callee);
    vm.growCurrentFrameNumRegs(callee->numRegs);

    // Jump to the appropriate entry point
    Code* entry;
    if (!callee->defaultEntryOffsets.empty()) {
        u16 idx = argc - callee->minArity;
        entry = callee->code.data() + callee->defaultEntryOffsets[idx];
    } else {
        entry = callee->code.data();
    }
    [[clang::musttail]] return entry->op(vm, entry);
}

// --- Range ---

// MAKE_RANGE Rd, Rstart, Rend, Rstep (4 words: op, regs, RangeType*, flags)
// flags.i: bit 0 = isInfinite, bit 1 = isInt
void op_make_range(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], startReg = pc[1].regs[1];
    u16 endReg = pc[1].regs[2], stepReg = pc[1].regs[3];
    auto* rangeType = static_cast<RangeType*>(pc[2].p);
    i64 flags = pc[3].i;
    bool isInfinite = (flags & 1) != 0;
    bool isInt = (flags & 2) != 0;

    auto* range = new RangeObj(rangeType);
    range->isInfinite_ = isInfinite;
    range->isInt_ = isInt;
    range->start_ = vm.reg(startReg);
    range->step_ = vm.reg(stepReg);
    if (!isInfinite) {
        range->end_ = vm.reg(endReg);
    }
    // Retain Obj* fields for non-Int ranges (e.g. Fraction ranges)
    if (!isInt) {
        if (range->start_.o) range->start_.o->retain();
        if (range->step_.o) range->step_.o->retain();
        if (!isInfinite && range->end_.o) range->end_.o->retain();
    }
    vm.reg(dst).o = range;
    DISPATCH(4);
}

// --- List Print Limit ---

// GET_LIST_PRINT_LIMIT Rd (2 words: op, regs)
void op_get_list_print_limit(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    vm.reg(dst).i = vm.listPrintLimit();
    DISPATCH(2);
}

// SET_LIST_PRINT_LIMIT Rd, Ra (2 words: op, regs) - returns old value in Rd
void op_set_list_print_limit(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    i64 oldVal = vm.listPrintLimit();
    vm.setListPrintLimit(vm.reg(src).i);
    vm.reg(dst).i = oldVal;
    DISPATCH(2);
}

// --- Lazy auto-map ---

// MAKE_LAZY_AUTOMAP Rd, RsrcList, RbroadcastBase, numBroadcast
// (3 words: op, regs{dst, srcList, broadcastBase, numBroadcast}, AutoMapCallInfo*)
void op_make_lazy_automap(VM& vm, Code* pc) {
    u16 dst          = pc[1].regs[0];
    u16 srcListReg   = pc[1].regs[1];
    u16 broadcastBase= pc[1].regs[2];
    u16 numBroadcast = pc[1].regs[3];
    auto* info       = static_cast<AutoMapCallInfo*>(pc[2].p);

    auto* srcList = static_cast<ListNode*>(vm.reg(srcListReg).o);
    if (!srcList) {
        vm.reg(dst).o = nullptr;
        DISPATCH(3);
    }

    auto* node = new ListNode(info->resultListType);
    auto* gen  = new AutoMapListGen(info->resultListType);
    gen->source_       = srcList;
    gen->info_         = info;
    gen->numBroadcast_ = numBroadcast;
    for (u16 i = 0; i < numBroadcast && i < AutoMapListGen::kMaxBroadcast; ++i) {
        gen->broadcastVals_[i] = vm.reg(broadcastBase + i);
    }
    node->generator_ = gen;
    // Retain Obj* fields stored in generator
    if (srcList) srcList->retain();
    gen->info_->retain();
    for (u16 i = 0; i < numBroadcast && i < AutoMapListGen::kMaxBroadcast; ++i) {
        if (i < info->broadcastArgs.size() && info->broadcastArgs[i].isObj && gen->broadcastVals_[i].o)
            gen->broadcastVals_[i].o->retain();
    }
    reinterpret_cast<GCObj*>(gen)->retain();
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// --- Map ---

// MAKE_MAP Rd, firstKeyReg, numPairs (3 words: op, regs{dst, firstKeyReg, numPairs}, MapType*)
void op_make_map(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstKV = pc[1].regs[1], numPairs = pc[1].regs[2];
    auto* mapType = static_cast<MapType*>(pc[2].p);

    auto* map = new MapObj(mapType);
    bool keyIsObj = storesObjPtr(mapType->keyType_);
    bool valIsObj = storesObjPtr(mapType->valueType_);
    for (u16 i = 0; i < numPairs; ++i) {
        Word key = vm.reg(firstKV + i * 2);
        Word val = vm.reg(firstKV + i * 2 + 1);
        map->entries_[key] = val;
        if (keyIsObj && key.o) key.o->retain();
        if (valIsObj && val.o) val.o->retain();
    }
    vm.reg(dst).o = map;
    DISPATCH(3);
}

// MAP_GET Rd, Ra(map), Rb(key) (3 words: op, regs{dst, map, key}, MapType*)
void op_map_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], mapReg = pc[1].regs[1], keyReg = pc[1].regs[2];
    auto* map = static_cast<MapObj*>(vm.reg(mapReg).o);

    auto it = map->entries_.find(vm.reg(keyReg));
    if (it != map->entries_.end()) {
        vm.reg(dst) = it->second;
    } else {
        vm.reg(dst).i = 0;  // default zero
    }
    DISPATCH(3);
}

// MAP_GET_OPTION Rd, Ra(map), Rb(key) (3 words: op, regs{dst, map, key}, EnumType*)
void op_map_get_option(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], mapReg = pc[1].regs[1], keyReg = pc[1].regs[2];
    auto* map = static_cast<MapObj*>(vm.reg(mapReg).o);
    auto* optType = static_cast<EnumType*>(pc[2].p);

    auto it = map->entries_.find(vm.reg(keyReg));

    // Phase 3: NullablePtrEnum -- store as nullable Obj* directly.
    if (optType->repr_ == Type::Repr::NullablePtrEnum) {
        if (it != map->entries_.end()) {
            Word v = it->second;
            if (v.o) v.o->retain();
            vm.reg(dst).o = v.o;
        } else {
            vm.reg(dst).o = nullptr;
        }
        DISPATCH(3);
    }

    auto* e = new Enum(optType);
    if (it != map->entries_.end()) {
        e->which_ = 0;  // some
        e->word_ = it->second;
        if (optType->gcCases_[0] && e->word_.o) e->word_.o->retain();
    } else {
        e->which_ = 1;  // none
        e->word_.i = 0;
    }
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// --- Set ---

// MAKE_SET Rd, firstSrc, numElems (3 words: op, regs{dst, firstSrc, numElems}, SetType*)
void op_make_set(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numElems = pc[1].regs[2];
    auto* setType = static_cast<SetType*>(pc[2].p);

    auto* set = new SetObj(setType);
    bool elemIsObj = storesObjPtr(setType->elemType_);
    for (u16 i = 0; i < numElems; ++i) {
        Word w = vm.reg(firstSrc + i);
        set->entries_.insert(w);
        if (elemIsObj && w.o) w.o->retain();
    }
    vm.reg(dst).o = set;
    DISPATCH(3);
}

// --- Ref ---

// MAKE_REF Rd, Rval (3 words: op, regs{dst, val}, RefType*)
void op_make_ref(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valReg = pc[1].regs[1];
    auto* refType = static_cast<RefType*>(pc[2].p);

    auto* ref = new RefValue(refType);
    ref->value_ = vm.reg(valReg);
    if (storesObjPtr(refType->elemType_) && ref->value_.o) ref->value_.o->retain();
    vm.reg(dst).o = ref;
    DISPATCH(3);
}

// REF_GET Rd, Ra (2 words: op, regs{dst, ref})
void op_ref_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], refReg = pc[1].regs[1];
    auto* ref = static_cast<RefValue*>(vm.reg(refReg).o);
    vm.reg(dst) = ref->value_;
    DISPATCH(2);
}

// REF_SET Rd, Ra, Rb (3 words: op, regs{dst, ref, val}, RefType*)
// Sets the ref's value and returns the assigned value in dst.
void op_ref_set(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], refReg = pc[1].regs[1], valReg = pc[1].regs[2];
    auto* ref = static_cast<RefValue*>(vm.reg(refReg).o);
    auto* refType = static_cast<RefType*>(ref->type_);
    Word newVal = vm.reg(valReg);
    if (storesObjPtr(refType->elemType_)) {
        if (newVal.o) newVal.o->retain();
        if (ref->value_.o) ref->value_.o->release();
    }
    ref->value_ = newVal;
    vm.reg(dst) = newVal;

    DISPATCH(3);
}

// --- Coroutines ---

// CORO_CREATE Rd, argBase, argc (4 words: op, regs{dst, argBase, argc}, global_idx, CoroutineType*)
void op_coro_create(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], argBase = pc[1].regs[1], argc = pc[1].regs[2];
    u32 globalIdx = (u32)pc[2].i;
    auto* codeBlock = static_cast<CodeBlock*>(vm.global(globalIdx).p);
    auto* coroType = static_cast<CoroutineType*>(pc[3].p);

    // Get function type from the code block
    auto* funcType = static_cast<FunctionType*>(codeBlock->funcType);

    // Allocate CoroutineObj with space for args
    auto* coro = CoroutineObj::create(coroType, funcType, codeBlock, argc);

    // Copy arguments into coroutine
    for (u16 i = 0; i < argc; ++i) {
        coro->args_[i] = vm.reg(argBase + i);
    }
    // Retain Obj* args so they survive the auto-release pool drain
    if (coro->funcType_) {
        for (u16 i = 0; i < coro->numArgs_ && i < coro->funcType_->argTypes_.size(); ++i) {
            if (storesObjPtr(coro->funcType_->argTypes_[i]) && coro->args_[i].o)
                coro->args_[i].o->retain();
        }
    }

    vm.reg(dst).o = coro;
    DISPATCH(4);
}

// CORO_CREATE_LAMBDA Rd, argBase, argc, lambdaReg (3 words: op, regs, CoroutineType*)
// Like op_coro_create but reads CodeBlock from a Lambda object and copies free vars.
void op_coro_create_lambda(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], argBase = pc[1].regs[1];
    u16 argc = pc[1].regs[2], lambdaReg = pc[1].regs[3];
    auto* coroType = static_cast<CoroutineType*>(pc[2].p);

    auto* lambda = static_cast<Lambda*>(vm.reg(lambdaReg).o);
    CodeBlock* codeBlock = lambda->codeBlock_;
    auto* funcType = static_cast<FunctionType*>(codeBlock->funcType);

    // Total stored values: args + free vars
    u16 totalArgs = argc + lambda->numFreeVars_;
    auto* coro = CoroutineObj::create(coroType, funcType, codeBlock, totalArgs);

    // Copy function arguments
    for (u16 i = 0; i < argc; ++i) {
        coro->args_[i] = vm.reg(argBase + i);
    }
    // Copy free variables from Lambda
    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        coro->args_[argc + i] = lambda->freeVars_[i];
    }

    // Retain Obj* args and free vars
    if (funcType) {
        for (u16 i = 0; i < argc && i < funcType->argTypes_.size(); ++i) {
            if (storesObjPtr(funcType->argTypes_[i]) && coro->args_[i].o)
                coro->args_[i].o->retain();
        }
    }
    // Free vars are Obj types — retain them
    auto* lambdaType = static_cast<LambdaType*>(lambda->type_);
    for (u16 i = 0; i < lambda->numFreeVars_; ++i) {
        if (storesObjPtr(lambdaType->freeVarTypes_[i]) && coro->args_[argc + i].o)
            coro->args_[argc + i].o->retain();
    }

    vm.reg(dst).o = coro;
    DISPATCH(3);
}

// CORO_RESUME Rd, Rcoro (2 words: op, regs{dst, coroReg})
void op_coro_resume(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], coroReg = pc[1].regs[1];
    auto* coro = static_cast<CoroutineObj*>(vm.reg(coroReg).o);

    if (coro->state_ == CoroutineObj::Done) {
        // Already done — value in dst is unused (caller checks state)
        vm.reg(dst).i = 0;
        DISPATCH(2);
    }

    // Save caller context into CoroutineObj
    coro->callerReturnPC_ = pc + 2;
    coro->callerResultReg_ = dst;
    coro->callerBaseReg_ = vm.baseReg();
    coro->callerFrameCount_ = vm.frameCount();
    coro->callerCoroFrame_ = vm.currentCoroFrame();
    coro->callerCoroutine_ = vm.currentCoroutine();

    // Activate coroutine
    vm.setCurrentCoroutine(coro);
    coro->state_ = CoroutineObj::Running;

    // Place coro body's registers after the caller's register window
    u32 newBase = vm.baseReg() + vm.currentFrameNumRegs();

    if (!coro->topFrame_) {
        // Created state: create CoroutineFrame as save slot for yield/resume
        CodeBlock* callee = coro->entryBlock_;
        auto* coroType = static_cast<CoroutineType*>(coro->type_);
        auto* frame = CoroutineFrame::create(coroType, callee, callee->numRegs);
        vm.setCurrentCoroFrame(frame);

        // Copy args from CoroutineObj into flat register file
        for (u16 i = 0; i < coro->numArgs_; ++i) {
            vm.regsBase()[newBase + i] = coro->args_[i];
        }

        // Push flat frame for coro body
        vm.pushFrame(nullptr, callee, newBase, callee->numRegs, 0);

        // Jump to entry
        Code* entry;
        if (!callee->defaultEntryOffsets.empty()) {
            u16 idx = coro->numArgs_ - callee->minArity;
            entry = callee->code.data() + callee->defaultEntryOffsets[idx];
        } else {
            entry = callee->code.data();
        }
        [[clang::musttail]] return entry->op(vm, entry);
    } else {
        // Suspended state: restore registers from CoroutineFrame save slot
        auto* frame = coro->topFrame_;
        vm.setCurrentCoroFrame(frame);

        // Copy registers from CoroutineFrame to flat register file
        for (u16 i = 0; i < frame->numRegs_; ++i) {
            vm.regsBase()[newBase + i] = frame->regs_[i];
        }

        // Release the yield-retained Obj* refs now that they're back in the
        // live register file.  No pool drain can happen between here and the
        // next yield (we're mid-execution), so the objects stay alive.
        CodeBlock* cb = frame->codeBlock_;
        u16 gmi = frame->gcMapIndex_;
        if (cb && gmi < cb->coroGCMaps_.size()) {
            for (u16 idx : cb->coroGCMaps_[gmi]) {
                if (idx < frame->numRegs_ && frame->regs_[idx].o)
                    frame->regs_[idx].o->release();
            }
        }
        frame->gcMapIndex_ = UINT16_MAX;  // nothing retained until next yield

        // Push flat frame for coro body
        vm.pushFrame(nullptr, frame->codeBlock_, newBase, frame->numRegs_, 0);

        Code* resumePC = coro->resumePC_;
        [[clang::musttail]] return resumePC->op(vm, resumePC);
    }
}

// YIELD Rsrc, gcMapIndex (2 words: op, regs{src, gcMapIdx})
void op_yield(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0], gcMapIdx = pc[1].regs[1];

    Word yieldedValue = vm.reg(src);

    auto* coro = vm.currentCoroutine();
    auto* frame = vm.currentCoroFrame();

    // Copy registers from flat register file to CoroutineFrame save slot
    Word* flatRegs = vm.regsBase() + vm.baseReg();
    for (u16 i = 0; i < frame->numRegs_; ++i) {
        frame->regs_[i] = flatRegs[i];
    }

    // Save coroutine state
    coro->resumePC_ = pc + 2;  // resume after yield
    frame->gcMapIndex_ = gcMapIdx;
    if (!coro->topFrame_) frame->retain();  // first yield: retain for coro ownership
    coro->topFrame_ = frame;
    coro->state_ = CoroutineObj::Suspended;

    // Retain Obj* values in saved registers so they survive auto-release pool
    // draining between scheduler invocations.  The matching release happens in
    // op_coro_resume (Suspended path) and CoroutineFrame::releaseChildren().
    // DeferredDeleteQueue::processN() skips objects whose refcount was bumped
    // back above 0 after enqueue, making retain-after-release-to-zero safe.
    CodeBlock* cb = frame->codeBlock_;
    if (cb && gcMapIdx < cb->coroGCMaps_.size()) {
        for (u16 idx : cb->coroGCMaps_[gcMapIdx]) {
            if (idx < frame->numRegs_ && frame->regs_[idx].o)
                frame->regs_[idx].o->retain();
        }
    }

    // Restore caller context
    vm.setBaseReg(coro->callerBaseReg_);
    vm.setFrameCount(coro->callerFrameCount_);
    vm.setCurrentCoroFrame(coro->callerCoroFrame_);
    vm.setCurrentCoroutine(coro->callerCoroutine_);
    vm.setCurrentRegs(vm.regsBase() + vm.baseReg());

    // Write yielded value directly to caller's destination register (no Enum wrapper)
    vm.reg(coro->callerResultReg_) = yieldedValue;

    // Clear stale caller refs
    coro->callerCoroFrame_ = nullptr;
    coro->callerCoroutine_ = nullptr;

    Code* returnPC = coro->callerReturnPC_;
    [[clang::musttail]] return returnPC->op(vm, returnPC);
}

// CORO_DONE (1 word: op)
void op_coro_done(VM& vm, Code* pc) {
    auto* coro = vm.currentCoroutine();

    // Mark as done
    coro->topFrame_ = nullptr;
    coro->state_ = CoroutineObj::Done;

    // Restore caller context
    vm.setBaseReg(coro->callerBaseReg_);
    vm.setFrameCount(coro->callerFrameCount_);
    vm.setCurrentCoroFrame(coro->callerCoroFrame_);
    vm.setCurrentCoroutine(coro->callerCoroutine_);
    vm.setCurrentRegs(vm.regsBase() + vm.baseReg());

    // Write zero to caller's destination register (caller checks state, not this value)
    vm.reg(coro->callerResultReg_).i = 0;

    // Clear stale caller refs
    coro->callerCoroFrame_ = nullptr;
    coro->callerCoroutine_ = nullptr;

    Code* returnPC = coro->callerReturnPC_;
    [[clang::musttail]] return returnPC->op(vm, returnPC);
}

// CORO_IS_DONE Rd, Rcoro (2 words: op, regs{dst, coroReg})
void op_coro_is_done(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], coroReg = pc[1].regs[1];
    auto* coro = static_cast<CoroutineObj*>(vm.reg(coroReg).o);
    vm.reg(dst).i = (coro->state_ == CoroutineObj::Done) ? 1 : 0;
    DISPATCH(2);
}

// CORO_WRAP_OPTION Rd, Rval, Rcoro (3 words: op, regs{dst, val, coro}, optionType*)
void op_coro_wrap_option(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valSrc = pc[1].regs[1], coroReg = pc[1].regs[2];
    auto* optType = static_cast<EnumType*>(pc[2].p);
    auto* coro = static_cast<CoroutineObj*>(vm.reg(coroReg).o);

    // Phase 3: NullablePtrEnum -- store as nullable Obj* directly.
    if (optType->repr_ == Type::Repr::NullablePtrEnum) {
        if (coro->state_ == CoroutineObj::Done) {
            vm.reg(dst).o = nullptr;
        } else {
            Word v = vm.reg(valSrc);
            if (v.o) v.o->retain();
            vm.reg(dst).o = v.o;
        }
        DISPATCH(3);
    }

    auto* e = new Enum(optType);
    if (coro->state_ == CoroutineObj::Done) {
        e->which_ = 1;  // none
        e->word_.i = 0;
    } else {
        e->which_ = 0;  // some
        e->word_ = vm.reg(valSrc);
        if (optType->gcCases_[0] && e->word_.o) e->word_.o->retain();
    }
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// --- Any ---

// MAKE_ANY Rd, Rsrc, isObj (3 words: op, regs{dst, src, isObj}, Type* wrappedType)
void op_make_any(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    bool isObj = pc[1].regs[2] != 0;
    auto* wrappedType = static_cast<Type*>(pc[2].p);
    auto* any = new AnyObj(vm.anyType());
    any->value_ = vm.reg(src);
    any->wrappedType_ = wrappedType;
    any->isObjType_ = isObj;
    if (isObj && any->value_.o) any->value_.o->retain();
    vm.reg(dst).o = any;
    DISPATCH(3);
}

// ANY_GET_VALUE Rd, Ra (2 words)
void op_any_get_value(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* any = static_cast<AnyObj*>(vm.reg(src).o);
    vm.reg(dst) = any->value_;
    DISPATCH(2);
}

// ANY_GET_TYPE_PTR Rd, Ra (2 words)
void op_any_get_type_ptr(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* any = static_cast<AnyObj*>(vm.reg(src).o);
    vm.reg(dst).p = any->wrappedType_;
    DISPATCH(2);
}

// --- Dynamic Scope ---

// LOAD_DYNAMIC Rd, K (3 words: op, regs, dynvar_index)
void op_load_dynamic(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    vm.reg(dst) = vm.dynVar(idx);
    DISPATCH(3);
}

// STORE_DYNAMIC Ra, K (3 words: op, regs, dynvar_index)
void op_store_dynamic(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    vm.dynVar(idx) = vm.reg(src);
    DISPATCH(3);
}

// STORE_DYNAMIC_OBJ Ra, K (3 words: op, regs, dynvar_index)
// Like STORE_DYNAMIC but retains the new Obj* and releases the old one.
void op_store_dynamic_obj(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    Obj* newVal = vm.reg(src).o;
    Obj* oldVal = vm.dynVar(idx).o;
    if (newVal) newVal->retain();
    vm.dynVar(idx) = vm.reg(src);
    if (oldVal) oldVal->release();
    DISPATCH(3);
}

// INIT_DYNAMIC_OBJ Ra, K (3 words: op, regs, dynvar_index)
// Like STORE_DYNAMIC but retains the new Obj*. No release of old value.
void op_init_dynamic_obj(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    Obj* newVal = vm.reg(src).o;
    if (newVal) newVal->retain();
    vm.dynVar(idx) = vm.reg(src);
    DISPATCH(3);
}

// DYNSCOPE_PUSH Ra, K (3 words: op, regs, dynvar_index)
// Save current value of dynvar[K], set dynvar[K] = reg(Ra)
void op_dynscope_push(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    vm.dynScopePush(idx, vm.reg(src));
    DISPATCH(3);
}

} // namespace ts
