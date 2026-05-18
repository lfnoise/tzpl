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

// --- Inline-composite global ops (Phase 4g.5) ---
// The global occupies sizeWords_ consecutive Word slots starting at K. Ops
// mem-copy the payload between regs and globals_, and walk the layout for
// ARC on writes (release embedded Obj* in old payload, retain in new).

// LOAD_GLOBAL_I Rd, K (4 words: op, regs{dst}, global_index, Type*)
void op_load_global_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.global(idx + i);
    DISPATCH(4);
}

// STORE_GLOBAL_I Ra, K (4 words: op, regs{src}, global_index, Type*)
// Release embedded Obj* in the old payload, copy new payload in, retain new.
void op_store_global_inline(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    inlineWalkPointers(&vm.global(idx), type, /*release_=*/true);
    for (u32 i = 0; i < n; ++i) vm.global(idx + i) = vm.reg((u16)(src + i));
    inlineWalkPointers(&vm.global(idx), type, /*release_=*/false);
    DISPATCH(4);
}

// INIT_GLOBAL_I Ra, K (4 words: op, regs{src}, global_index, Type*)
// Like STORE_GLOBAL_I but no release of old payload (declaration site).
void op_init_global_inline(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    for (u32 i = 0; i < n; ++i) vm.global(idx + i) = vm.reg((u16)(src + i));
    inlineWalkPointers(&vm.global(idx), type, /*release_=*/false);
    DISPATCH(4);
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

// --- Integer arithmetic with i16 immediate in regs[2] ---
// The immediate is sign-extended from the u16 slot. Emitted by the codegen
// peephole when a binary op has an IntLiteral on the RHS that fits in i16
// (and, for commutative ops, also when it appears on the LHS).

void op_add_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i + imm;
    DISPATCH(2);
}

void op_sub_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i - imm;
    DISPATCH(2);
}

void op_mul_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = vm.reg(a).i * imm;
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

// Phase 4g.2: generic boxing for inline structs / tuples. Storage boundaries
// (globals, ObjArray elements, Map/Set keys/values, etc.) still expect a
// 1-word Obj* slot. These ops allocate a Struct/Tuple whose v[] memory is
// sized to fit the inline footprint (sizeWords_ Words) and copy the inline
// payload across so a layout_-driven retain walk works the same as for a
// natively-built Heap composite.
void op_box_struct(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* st = static_cast<StructType*>(pc[2].p);
    vm.reg(dst).o = boxInlineDeep(vm, st, src);
    DISPATCH(3);
}

void op_unbox_struct(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* st = static_cast<StructType*>(pc[2].p);
    unboxInlineDeep(vm, st, vm.reg(src).o, dst);
    DISPATCH(3);
}

void op_box_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* tt = static_cast<TupleType*>(pc[2].p);
    vm.reg(dst).o = boxInlineDeep(vm, tt, src);
    DISPATCH(3);
}

void op_unbox_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* tt = static_cast<TupleType*>(pc[2].p);
    unboxInlineDeep(vm, tt, vm.reg(src).o, dst);
    DISPATCH(3);
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

// CMP_EQ_OBJ / CMP_NE_OBJ Rd, Ra, Rb (3 words: op, regs, operandType*)
//
// Phase 4g.16: operand type is now passed explicitly so the handler can
// compare Inline composite operands natively via wordsEqual on the multi-
// word slot data -- no boxing required. For single-Word operands (heap
// Obj* or atom), the slot's [0] word is the value; wordsEqual handles
// both uniformly. Null Obj* operands are short-circuited by pointer
// identity since WordEqual derefs Obj* for Pointer/Heap types.
static inline bool cmpEqObjImpl(VM& vm, u16 a, u16 b, Type* t) {
    if (t && t->isObjType() && t->repr_ != Type::Repr::Inline) {
        Obj* oa = vm.reg(a).o;
        Obj* ob = vm.reg(b).o;
        if (!oa || !ob) return oa == ob;
    }
    return wordsEqual(&vm.reg(a), &vm.reg(b), t);
}

void op_cmp_eq_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* t = static_cast<Type*>(pc[2].p);
    vm.reg(dst).i = cmpEqObjImpl(vm, a, b, t) ? 1 : 0;
    DISPATCH(3);
}

void op_cmp_ne_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* t = static_cast<Type*>(pc[2].p);
    vm.reg(dst).i = cmpEqObjImpl(vm, a, b, t) ? 0 : 1;
    DISPATCH(3);
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

// --- Integer comparisons with i16 immediate in regs[2] ---
// Form is always `Ra OP imm`. When the literal appeared on the LHS the codegen
// canonicalizes by flipping the comparison (e.g. `5 < n` -> `n > 5`).

void op_cmp_eq_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i == imm) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ne_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i != imm) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_lt_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i < imm) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_le_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i <= imm) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_gt_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i > imm) ? 1 : 0;
    DISPATCH(2);
}

void op_cmp_ge_int_imm(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    i64 imm = (i16)pc[1].regs[2];
    vm.reg(dst).i = (vm.reg(a).i >= imm) ? 1 : 0;
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

// SAFEPOINT  (1 word: op)
// Polled before every backward jump. Hot path: one relaxed load + one branch.
// When vm.gcRequested() is set, drain the deferred-delete queue under a
// bounded budget. Future phases will run mark/sweep work here too.
void op_safepoint(VM& vm, Code* pc) {
    if (vm.gcRequested_.load(std::memory_order_relaxed)) [[unlikely]] {
        vm.safepointPoll();
    }
    DISPATCH(1);
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

// RETURN Ra, nWords  (2 words: op, regs{src, nWords})
// Phase 4d: nWords carries the slot size of the return type so multi-word
// inline value types (Complex / Fraction) flow back to the caller's slot
// natively, without the box-on-return + unbox-after-call dance Phase 4f
// originally needed.
void op_return(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u16 nWords = pc[1].regs[1];

    // Snapshot the return value words from the callee's frame before popping.
    // Up to 4 words is the inline value-type ceiling per the plan.
    Word tmp[4];
    for (u16 i = 0; i < nWords; ++i) tmp[i] = vm.reg(src + i);

    // If this is the top-level frame, just halt -- result lands in regs 0..n-1.
    if (vm.frameCount() == 1) {
        vm.popFrame();
        for (u16 i = 0; i < nWords; ++i) vm.reg(i) = tmp[i];
        vm.setHalted(true);
        return;
    }

    // Pop frame - returns the frame data (restores caller's baseReg)
    CallFrame frame = vm.popFrame();

    // Write nWords to caller's result slot (resultReg is the first word).
    for (u16 i = 0; i < nWords; ++i) vm.reg(frame.resultReg + i) = tmp[i];

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
template <typename T>
static void podArrayConcat(PodArray<T>* a, PodArray<T>* b, ArrayType* arrayType, Word& out) {
    auto* result = new PodArray<T>(arrayType);
    result->v.reserve(a->v.size() + b->v.size());
    result->v.insert(result->v.end(), a->v.begin(), a->v.end());
    result->v.insert(result->v.end(), b->v.begin(), b->v.end());
    out.o = result;
}

void op_concat_array(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex:
            podArrayConcat(static_cast<PodArray<x64>*>(vm.reg(a).o),
                           static_cast<PodArray<x64>*>(vm.reg(b).o), arrayType, vm.reg(dst));
            break;
        case ArrayBackend::Fraction:
            podArrayConcat(static_cast<PodArray<r64>*>(vm.reg(a).o),
                           static_cast<PodArray<r64>*>(vm.reg(b).o), arrayType, vm.reg(dst));
            break;
        case ArrayBackend::Float:
            podArrayConcat(static_cast<PodArray<f64>*>(vm.reg(a).o),
                           static_cast<PodArray<f64>*>(vm.reg(b).o), arrayType, vm.reg(dst));
            break;
        case ArrayBackend::Int:
            podArrayConcat(static_cast<PodArray<i64>*>(vm.reg(a).o),
                           static_cast<PodArray<i64>*>(vm.reg(b).o), arrayType, vm.reg(dst));
            break;
        case ArrayBackend::Inline: {
            auto* arrA = static_cast<InlineArray*>(vm.reg(a).o);
            auto* arrB = static_cast<InlineArray*>(vm.reg(b).o);
            auto* result = new InlineArray(arrayType);
            result->reserve(arrA->size() + arrB->size());
            for (size_t i = 0; i < arrA->size(); ++i) result->pushSlot(arrA->slot(i));
            for (size_t i = 0; i < arrB->size(); ++i) result->pushSlot(arrB->slot(i));
            vm.reg(dst).o = result;
            break;
        }
        case ArrayBackend::Obj: {
            auto* arrA = static_cast<ObjArray*>(vm.reg(a).o);
            auto* arrB = static_cast<ObjArray*>(vm.reg(b).o);
            auto* result = new ObjArray(arrayType);
            result->reserve(arrA->size() + arrB->size());
            for (auto* obj : *arrA) result->push(obj);
            for (auto* obj : *arrB) result->push(obj);
            vm.reg(dst).o = result;
            break;
        }
    }
    DISPATCH(3);
}

// CONCAT_TUPLE Rd, Ra, Rb (5 words: op, regs{dst, a, b}, resultType*, leftType*, rightType*)
//
// Phase 4g.13: layout-aware multi-word copy. Source A's full layout footprint
// starts at v[0]; source B fills the remaining tail of the result layout.
// Phase 4g.16: accept Inline operands natively -- the operand base is either
// &vm.reg(slot) (Inline) or &tup->v[0] (heap). Result is built into either
// the multi-word dst slot (Inline) or a heap Tuple (Heap).
void op_concat_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* resultType = static_cast<TupleType*>(pc[2].p);
    auto* leftType   = static_cast<TupleType*>(pc[3].p);
    auto* rightType  = static_cast<TupleType*>(pc[4].p);

    auto baseOf = [&](u16 slot, TupleType* t) -> Word const* {
        if (t->repr_ == Type::Repr::Inline) return &vm.reg(slot);
        return &static_cast<Tuple*>(vm.reg(slot).o)->v[0];
    };
    Word const* aBase = baseOf(a, leftType);
    Word const* bBase = baseOf(b, rightType);

    u32 aWords = 0;
    for (auto const& f : leftType->layout_) aWords += f.sizeWords;
    u32 bWords = 0;
    for (auto const& f : rightType->layout_) bWords += f.sizeWords;
    u32 totalWords = aWords + bWords;

    Word* outBase;
    Tuple* heapResult = nullptr;
    if (resultType->repr_ == Type::Repr::Inline) {
        outBase = &vm.reg(dst);
    } else {
        heapResult = Tuple::create(resultType, (u32)resultType->fields_.size());
        outBase = &heapResult->v[0];
    }
    for (u32 i = 0; i < aWords; ++i) outBase[i] = aBase[i];
    for (u32 i = 0; i < bWords; ++i) outBase[aWords + i] = bBase[i];
    inlineWalkPointers(outBase, resultType, /*release_=*/false);
    if (heapResult) vm.reg(dst).o = heapResult;
    (void)totalWords;
    DISPATCH(5);
}

// CONCAT_LIST Rd, Ra, Rb (3 words: op, regs{dst, a, b}, ListType*)
void op_concat_list(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);
    auto* nodeA = static_cast<ListNode*>(vm.reg(a).o);
    auto* nodeB = static_cast<ListNode*>(vm.reg(b).o);
    if (!nodeA) { vm.reg(dst).o = nodeB; DISPATCH(3); }
    if (!nodeB) { vm.reg(dst).o = nodeA; DISPATCH(3); }
    auto* node = ListNode::create(listType);
    auto* gen = new CatListGen(vm.typeType());
    gen->first_ = nodeA; gen->second_ = nodeB;
    gen->inSecond_ = false; gen->listType_ = listType;
    node->installGenerator(gen);
    gen->first_->retain();
    gen->second_->retain();
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// --- Array Destructuring ---

// ARRAY_GET Rd, Ra, idx  (3 words: op, regs{dst, src, idx}, ArrayType*)
// Gets element at compile-time constant index from array.
// Phase 4e: Complex and Fraction land as 2-word inline values in dst..dst+1.
void op_array_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], idx = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex: {
            auto* arr = static_cast<PodArray<x64>*>(vm.reg(src).o);
            x64 const& v = arr->v[cyclicIndex(idx, arr->v.size())];
            vm.reg(dst).f = v.real();
            vm.reg((u16)(dst + 1)).f = v.imag();
            break;
        }
        case ArrayBackend::Fraction: {
            auto* arr = static_cast<PodArray<r64>*>(vm.reg(src).o);
            r64 const& v = arr->v[cyclicIndex(idx, arr->v.size())];
            vm.reg(dst).i = v.numer();
            vm.reg((u16)(dst + 1)).i = v.denom();
            break;
        }
        case ArrayBackend::Float: {
            auto* arr = static_cast<PodArray<f64>*>(vm.reg(src).o);
            vm.reg(dst).f = arr->v[cyclicIndex(idx, arr->v.size())];
            break;
        }
        case ArrayBackend::Int: {
            auto* arr = static_cast<PodArray<i64>*>(vm.reg(src).o);
            vm.reg(dst).i = arr->v[cyclicIndex(idx, arr->v.size())];
            break;
        }
        case ArrayBackend::Inline: {
            auto* arr = static_cast<InlineArray*>(vm.reg(src).o);
            size_t i = cyclicIndex(idx, arr->size());
            arr->getSlot(i, &vm.reg(dst));
            break;
        }
        case ArrayBackend::Obj: {
            auto* arr = static_cast<ObjArray*>(vm.reg(src).o);
            vm.reg(dst).o = arr->get(cyclicIndex(idx, arr->size()));
            break;
        }
    }
    DISPATCH(3);
}

// ARRAY_SLICE Rd, Ra, startIdx  (3 words: op, regs{dst, src, startIdx}, ArrayType*)
// Creates new array from arr[startIdx:]
template <typename T>
static void podArraySliceFrom(PodArray<T>* arr, ArrayType* arrayType, u16 startIdx, Word& out) {
    auto* result = new PodArray<T>(arrayType);
    if (startIdx < arr->v.size()) {
        result->v.assign(arr->v.begin() + startIdx, arr->v.end());
    }
    out.o = result;
}

void op_array_slice(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], startIdx = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex:
            podArraySliceFrom(static_cast<PodArray<x64>*>(vm.reg(src).o), arrayType, startIdx, vm.reg(dst));
            break;
        case ArrayBackend::Fraction:
            podArraySliceFrom(static_cast<PodArray<r64>*>(vm.reg(src).o), arrayType, startIdx, vm.reg(dst));
            break;
        case ArrayBackend::Float:
            podArraySliceFrom(static_cast<PodArray<f64>*>(vm.reg(src).o), arrayType, startIdx, vm.reg(dst));
            break;
        case ArrayBackend::Int:
            podArraySliceFrom(static_cast<PodArray<i64>*>(vm.reg(src).o), arrayType, startIdx, vm.reg(dst));
            break;
        case ArrayBackend::Inline: {
            auto* arr = static_cast<InlineArray*>(vm.reg(src).o);
            auto* result = new InlineArray(arrayType);
            if (startIdx < arr->size()) {
                result->reserve(arr->size() - startIdx);
                for (size_t i = startIdx; i < arr->size(); ++i)
                    result->pushSlot(arr->slot(i));
            }
            vm.reg(dst).o = result;
            break;
        }
        case ArrayBackend::Obj: {
            auto* arr = static_cast<ObjArray*>(vm.reg(src).o);
            auto* result = new ObjArray(arrayType);
            if (startIdx < arr->size()) {
                for (size_t i = startIdx; i < arr->size(); ++i)
                    result->push(arr->get(i));
            }
            vm.reg(dst).o = result;
            break;
        }
    }
    DISPATCH(3);
}

// ARRAY_LENGTH Rd, Ra  (3 words: op, regs{dst, src}, ArrayType*)
//
// Phase 4g.28: one specialization per array backend. The element type is
// statically known at codegen time, so the codegen emits the right variant
// directly and the runtime path is branch-free.
void op_array_length_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<PodArray<i64>*>(vm.reg(src).o)->v.size();
    DISPATCH(3);
}
void op_array_length_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<PodArray<f64>*>(vm.reg(src).o)->v.size();
    DISPATCH(3);
}
void op_array_length_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<PodArray<x64>*>(vm.reg(src).o)->v.size();
    DISPATCH(3);
}
void op_array_length_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<PodArray<r64>*>(vm.reg(src).o)->v.size();
    DISPATCH(3);
}
void op_array_length_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<InlineArray*>(vm.reg(src).o)->size();
    DISPATCH(3);
}
void op_array_length_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    vm.reg(dst).i = (i64)static_cast<ObjArray*>(vm.reg(src).o)->size();
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
//
// Backend-aware accessors used by the generic array dispatch paths. Element
// reads return a single Word: PodArray<x64>/PodArray<r64>/InlineArray slots
// are boxed into heap Complex/Fraction/Tuple/Struct so the rest of
// dispatchBinop can broadcast them as ordinary scalars; element writes unbox
// the heap result back into the native slot.

// Backend-templated element accessors. The `if constexpr` chain collapses
// to a single backend's body when called with a constant ArrayBackend, so
// loops that hoist `arrayBackendFor` outside and pass the result in get
// fully-specialized read/write paths with no per-iteration dispatch.

template<ArrayBackend B>
inline usize arrayLen_t(Obj* arr) {
    if constexpr (B == ArrayBackend::Int)      return static_cast<PodArray<i64>*>(arr)->v.size();
    else if constexpr (B == ArrayBackend::Float)    return static_cast<PodArray<f64>*>(arr)->v.size();
    else if constexpr (B == ArrayBackend::Complex)  return static_cast<PodArray<x64>*>(arr)->v.size();
    else if constexpr (B == ArrayBackend::Fraction) return static_cast<PodArray<r64>*>(arr)->v.size();
    else if constexpr (B == ArrayBackend::Inline)   return static_cast<InlineArray*>(arr)->size();
    else if constexpr (B == ArrayBackend::Obj)      return static_cast<ObjArray*>(arr)->size();
}

template<ArrayBackend B>
inline Word readElem_t(Obj* arr, usize i, Type* elemType, VM& vm) {
    if constexpr (B == ArrayBackend::Int) {
        return Word(static_cast<PodArray<i64>*>(arr)->v[i]);
    } else if constexpr (B == ArrayBackend::Float) {
        return Word(static_cast<PodArray<f64>*>(arr)->v[i]);
    } else if constexpr (B == ArrayBackend::Complex) {
        x64 const& x = static_cast<PodArray<x64>*>(arr)->v[i];
        auto* c = new Complex(x);
        c->retain();
        return Word(static_cast<Obj*>(c));
    } else if constexpr (B == ArrayBackend::Fraction) {
        r64 const& r = static_cast<PodArray<r64>*>(arr)->v[i];
        auto* fr = new Fraction(r);
        fr->retain();
        return Word(static_cast<Obj*>(fr));
    } else if constexpr (B == ArrayBackend::Inline) {
        Word const* slot = static_cast<InlineArray*>(arr)->slot(i);
        return boxPayload(vm, elemType, slot);
    } else if constexpr (B == ArrayBackend::Obj) {
        return Word(static_cast<ObjArray*>(arr)->get(i));
    }
}

// Runtime-dispatched wrappers used when the backend isn't statically
// known at the call site. Switch is on the small ArrayBackend enum, not
// on Type* identity — compilers turn this into a jump table.

inline usize arrayLen(Obj* arr, ArrayBackend b) {
    switch (b) {
        case ArrayBackend::Int:      return arrayLen_t<ArrayBackend::Int>(arr);
        case ArrayBackend::Float:    return arrayLen_t<ArrayBackend::Float>(arr);
        case ArrayBackend::Complex:  return arrayLen_t<ArrayBackend::Complex>(arr);
        case ArrayBackend::Fraction: return arrayLen_t<ArrayBackend::Fraction>(arr);
        case ArrayBackend::Inline:   return arrayLen_t<ArrayBackend::Inline>(arr);
        case ArrayBackend::Obj:      return arrayLen_t<ArrayBackend::Obj>(arr);
    }
    return 0;
}

inline Word readElem(Obj* arr, usize i, ArrayBackend b, Type* elemType, VM& vm) {
    switch (b) {
        case ArrayBackend::Int:      return readElem_t<ArrayBackend::Int>(arr, i, elemType, vm);
        case ArrayBackend::Float:    return readElem_t<ArrayBackend::Float>(arr, i, elemType, vm);
        case ArrayBackend::Complex:  return readElem_t<ArrayBackend::Complex>(arr, i, elemType, vm);
        case ArrayBackend::Fraction: return readElem_t<ArrayBackend::Fraction>(arr, i, elemType, vm);
        case ArrayBackend::Inline:   return readElem_t<ArrayBackend::Inline>(arr, i, elemType, vm);
        case ArrayBackend::Obj:      return readElem_t<ArrayBackend::Obj>(arr, i, elemType, vm);
    }
    return Word{};
}

static Obj* makeArray(VM& vm, ArrayType* type, usize len) {
    Type* elem = type->elemType_;
    switch (arrayBackendFor(elem)) {
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(type);
            arr->v.resize(len);
            return arr;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(type);
            arr->v.resize(len);
            return arr;
        }
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(type);
            arr->v.resize(len);
            return arr;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(type);
            arr->v.resize(len);
            return arr;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(type);
            arr->resize(len);
            return arr;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(type);
            arr->resize(len);
            return arr;
        }
    }
    return nullptr;
}

template<ArrayBackend B>
inline void writeElem_t(Obj* arr, usize i, Word w, Type* elemType, VM& vm) {
    if constexpr (B == ArrayBackend::Int) {
        static_cast<PodArray<i64>*>(arr)->v[i] = w.i;
    } else if constexpr (B == ArrayBackend::Float) {
        static_cast<PodArray<f64>*>(arr)->v[i] = w.f;
    } else if constexpr (B == ArrayBackend::Complex) {
        static_cast<PodArray<x64>*>(arr)->v[i] = static_cast<Complex*>(w.o)->x;
    } else if constexpr (B == ArrayBackend::Fraction) {
        static_cast<PodArray<r64>*>(arr)->v[i] = static_cast<Fraction*>(w.o)->r;
    } else if constexpr (B == ArrayBackend::Inline) {
        Word* dst = static_cast<InlineArray*>(arr)->slot(i);
        unboxInlineDeepTo(vm, elemType, w.o, dst);
    } else if constexpr (B == ArrayBackend::Obj) {
        static_cast<ObjArray*>(arr)->set(i, w.o);
    }
}

inline void writeElem(Obj* arr, usize i, Word w, ArrayBackend b, Type* elemType, VM& vm) {
    switch (b) {
        case ArrayBackend::Int:      writeElem_t<ArrayBackend::Int>(arr, i, w, elemType, vm); return;
        case ArrayBackend::Float:    writeElem_t<ArrayBackend::Float>(arr, i, w, elemType, vm); return;
        case ArrayBackend::Complex:  writeElem_t<ArrayBackend::Complex>(arr, i, w, elemType, vm); return;
        case ArrayBackend::Fraction: writeElem_t<ArrayBackend::Fraction>(arr, i, w, elemType, vm); return;
        case ArrayBackend::Inline:   writeElem_t<ArrayBackend::Inline>(arr, i, w, elemType, vm); return;
        case ArrayBackend::Obj:      writeElem_t<ArrayBackend::Obj>(arr, i, w, elemType, vm); return;
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
            // Phase 4g.32: Complex/Fraction fast paths -- avoid the per-element
            // heap Complex/Fraction allocation the generic loop would do.
            if constexpr (bool(Op::flags & mathOpComplexArgs)) {
                if (resultElem == vm.complexType())
                    return Word(static_cast<Obj*>(podArrayLoop(op,
                        static_cast<PodArray<x64>*>(a.o),
                        static_cast<PodArray<x64>*>(b.o), resultAT)));
            }
            if constexpr (bool(Op::flags & mathOpFractionArgs)) {
                if (resultElem == vm.fractionType())
                    return Word(static_cast<Obj*>(podArrayLoop(op,
                        static_cast<PodArray<r64>*>(a.o),
                        static_cast<PodArray<r64>*>(b.o), resultAT)));
            }
        }

        // Generic path: per-element recursive dispatch. Hoist backend
        // lookups out of the loop so readElem/writeElem dispatch on a
        // small enum instead of recomputing arrayBackendFor each iter.
        ArrayBackend ba = arrayBackendFor(elemA);
        ArrayBackend bb = arrayBackendFor(elemB);
        ArrayBackend br = arrayBackendFor(resultElem);
        usize lenA = arrayLen(a.o, ba);
        usize lenB = arrayLen(b.o, bb);
        usize len = std::min(lenA, lenB);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, ba, elemA, vm);
            Word be = readElem(b.o, i, bb, elemB, vm);
            Word re = dispatchBinop(vm, op, ae, be, elemA, elemB, resultElem);
            writeElem(result, i, re, br, resultElem, vm);
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
            // Phase 4g.32: Complex/Fraction array-scalar fast paths. The
            // boxed scalar b carries the value as a heap Complex*/Fraction*;
            // extract once into x64/r64 and run a tight POD loop instead of
            // allocating a fresh heap Complex/Fraction per element.
            if constexpr (bool(Op::flags & mathOpComplexArgs)) {
                if (resultElem == vm.complexType())
                    return Word(static_cast<Obj*>(podArrayScalarLoop(op,
                        static_cast<PodArray<x64>*>(a.o),
                        static_cast<Complex*>(b.o)->x, resultAT)));
            }
            if constexpr (bool(Op::flags & mathOpFractionArgs)) {
                if (resultElem == vm.fractionType())
                    return Word(static_cast<Obj*>(podArrayScalarLoop(op,
                        static_cast<PodArray<r64>*>(a.o),
                        static_cast<Fraction*>(b.o)->r, resultAT)));
            }
        }

        // Generic path
        ArrayBackend ba = arrayBackendFor(elemA);
        ArrayBackend br = arrayBackendFor(resultElem);
        usize len = arrayLen(a.o, ba);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, ba, elemA, vm);
            Word re = dispatchBinop(vm, op, ae, b, elemA, bType, resultElem);
            writeElem(result, i, re, br, resultElem, vm);
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
        // Phase 4g.32: Complex/Fraction scalar-array fast paths.
        if constexpr (bool(Op::flags & mathOpComplexArgs)) {
            if (resultElem == vm.complexType())
                return Word(static_cast<Obj*>(podScalarArrayLoop(op,
                    static_cast<Complex*>(a.o)->x,
                    static_cast<PodArray<x64>*>(b.o), resultAT)));
        }
        if constexpr (bool(Op::flags & mathOpFractionArgs)) {
            if (resultElem == vm.fractionType())
                return Word(static_cast<Obj*>(podScalarArrayLoop(op,
                    static_cast<Fraction*>(a.o)->r,
                    static_cast<PodArray<r64>*>(b.o), resultAT)));
        }
    }

    // Generic path
    ArrayBackend bb = arrayBackendFor(elemB);
    ArrayBackend br = arrayBackendFor(resultElem);
    usize len = arrayLen(b.o, bb);
    Obj* result = makeArray(vm, resultAT, len);
    for (usize i = 0; i < len; ++i) {
        Word be = readElem(b.o, i, bb, elemB, vm);
        Word re = dispatchBinop(vm, op, a, be, aType, elemB, resultElem);
        writeElem(result, i, re, br, resultElem, vm);
    }
    return Word(result);
}

// Phase 4g.13/4g.16: helpers for tuple binop/cmp/unary. Read fields from a
// Word const* base, which is &tup->v[0] for heap-Tuple operands or
// &vm.reg(slot) for native Inline composite operands (same field layout
// either way, by Phase 4g.13 design). Multi-word inline composite fields
// are boxed into a single Word for the scalar dispatchBinop call; outputs
// are unboxed back into native multi-word storage by writeTupleField.
static Word readTupleField(VM& vm, Word const* base, TupleType* tt, size_t i) {
    auto const& f = tt->layout_[i];
    if (f.sizeWords > 1) return boxPayload(vm, f.type, &base[f.wordOffset]);
    return base[f.wordOffset];
}

// Compute the field-read base pointer for a composite operand.
//   - If aType is Inline composite at the top level, the operand is the
//     multi-word slot starting at vm.reg(aSlot).
//   - Otherwise the operand is a heap Tuple/Struct held in vm.reg(aSlot).o;
//     use &obj->v[0]. Tuple and Struct share the v[] layout offset.
//   - Scalar/non-composite operands return null (caller falls back to scalar).
static Word const* compositeOperandBase(VM& vm, u16 aSlot, Type* aType) {
    if (!aType) return nullptr;
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* aST = dynamic_cast<StructType*>(aType);
    if (!aTT && !aST) return nullptr;
    if (aType->repr_ == Type::Repr::Inline) return &vm.reg(aSlot);
    Obj* o = vm.reg(aSlot).o;
    if (!o) return nullptr;
    if (aTT) return &static_cast<Tuple*>(o)->v[0];
    return &static_cast<Struct*>(o)->v[0];
}

static void writeTupleField(VM& vm, Tuple* result, TupleType* rtt, size_t i, Word src) {
    auto const& f = rtt->layout_[i];
    Type* ft = f.type;
    if (f.sizeWords > 1) {
        // Unbox the returned heap Obj* into native multi-word storage.
        if (ft == gCurrentVM->complexType()) {
            auto* c = static_cast<Complex*>(src.o);
            result->v[f.wordOffset].f     = c->x.real();
            result->v[f.wordOffset + 1].f = c->x.imag();
        } else if (ft == gCurrentVM->fractionType()) {
            auto* fr = static_cast<Fraction*>(src.o);
            result->v[f.wordOffset].i     = fr->r.numer();
            result->v[f.wordOffset + 1].i = fr->r.denom();
        } else {
            unboxInlineDeepTo(vm, ft, src.o, &result->v[f.wordOffset]);
        }
    } else {
        result->v[f.wordOffset] = src;
    }
}

// Phase 4g.16: dispatch tuple binop given precomputed field-read bases.
// aBase/bBase are non-null when the operand is a Tuple (either heap or
// Inline); aScalar/bScalar provide the broadcast value for non-tuple
// operands. aType/bType carry the operand type for scalar broadcasts.
template<typename Op>
static Word dispatchTupleBinopBase(VM& vm, Op op,
                                   Word const* aBase, Word const* bBase,
                                   Word aScalar, Word bScalar,
                                   Type* aType, Type* bType, TupleType* resultTT) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    usize n = resultTT->fields_.size();
    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = aTT ? readTupleField(vm, aBase, aTT, i) : aScalar;
        Word be = bTT ? readTupleField(vm, bBase, bTT, i) : bScalar;
        Type* aet = aTT ? aTT->fields_[i] : aType;
        Type* bet = bTT ? bTT->fields_[i] : bType;
        Word r = dispatchBinop(vm, op, ae, be, aet, bet, resultTT->fields_[i]);
        writeTupleField(vm, result, resultTT, i, r);
    }
    inlineWalkPointers(&result->v[0], resultTT, /*release_=*/false);
    return Word(static_cast<Obj*>(result));
}

template<typename Op>
static Word dispatchTupleBinop(VM& vm, Op op, Word a, Word b,
                               Type* aType, Type* bType, TupleType* resultTT) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    Word const* aBase = aTT ? &static_cast<Tuple*>(a.o)->v[0] : nullptr;
    Word const* bBase = bTT ? &static_cast<Tuple*>(b.o)->v[0] : nullptr;
    return dispatchTupleBinopBase(vm, op, aBase, bBase, a, b, aType, bType, resultTT);
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
    auto* node = ListNode::create(resultLT);
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

    node->installGenerator(gen);
    if (gen->leftList_) gen->leftList_->retain();
    if (gen->rightList_) gen->rightList_->retain();
    if (gen->broadcastValIsObj_ && gen->broadcastVal_.o) gen->broadcastVal_.o->retain();
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
    ArrayBackend ba = arrayBackendFor(elemA);
    usize len = arrayLen(a.o, ba);

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
        // Phase 4g.32: Complex/Fraction unary fast paths.
        if constexpr (bool(Op::flags & mathOpComplexArgs)) {
            if (resultElem == vm.complexType()) {
                auto* pa = static_cast<PodArray<x64>*>(a.o);
                auto* r = new PodArray<x64>(resultAT);
                r->v.resize(len);
                for (usize i = 0; i < len; ++i)
                    r->v[i] = op(pa->v[i]);
                return Word(static_cast<Obj*>(r));
            }
        }
        if constexpr (bool(Op::flags & mathOpFractionArgs)) {
            if (resultElem == vm.fractionType()) {
                auto* pa = static_cast<PodArray<r64>*>(a.o);
                auto* r = new PodArray<r64>(resultAT);
                r->v.resize(len);
                for (usize i = 0; i < len; ++i)
                    r->v[i] = op(pa->v[i]);
                return Word(static_cast<Obj*>(r));
            }
        }
    }

    // Generic path: per-element recursive dispatch
    ArrayBackend br = arrayBackendFor(resultElem);
    Obj* result = makeArray(vm, resultAT, len);
    for (usize i = 0; i < len; ++i) {
        Word ae = readElem(a.o, i, ba, elemA, vm);
        Word re = dispatchUnaryOp(vm, op, ae, elemA, resultElem);
        writeElem(result, i, re, br, resultElem, vm);
    }
    return Word(result);
}

template<typename Op>
static Word dispatchTupleUnaryOpBase(VM& vm, Op op, Word const* aBase,
                                     Type* aType, TupleType* resultTT) {
    auto* aTT = static_cast<TupleType*>(aType);
    usize n = resultTT->fields_.size();
    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = readTupleField(vm, aBase, aTT, i);
        Word r = dispatchUnaryOp(vm, op, ae, aTT->fields_[i], resultTT->fields_[i]);
        writeTupleField(vm, result, resultTT, i, r);
    }
    inlineWalkPointers(&result->v[0], resultTT, /*release_=*/false);
    return Word(static_cast<Obj*>(result));
}

template<typename Op>
static Word dispatchTupleUnaryOp(VM& vm, Op op, Word a, Type* aType, TupleType* resultTT) {
    return dispatchTupleUnaryOpBase(vm, op, &static_cast<Tuple*>(a.o)->v[0], aType, resultTT);
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
    auto* node = ListNode::create(resultLT);
    auto* gen = new UnaryListGen(gCurrentVM->typeType());
    gen->opKind_ = opKind;
    gen->source_ = srcA;
    gen->sourceElemType_ = listA->elemType_;
    gen->resultElemType_ = resultLT->elemType_;
    gen->resultListType_ = resultLT;

    node->installGenerator(gen);
    gen->source_->retain();
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

// Phase 4g.16: top-level entry for heap-result composite binop.
//   - Tuple result + Inline-Tuple operand(s): use base-pointer dispatcher
//     to read operand fields natively from the register slot (zero boxing).
//   - Array/List result + Inline-Tuple operand: the inner dispatchers
//     (dispatchArrayBinop, dispatchListBinop) expect single-Word operands
//     and broadcast/zip element-wise; box the Inline operand once on entry
//     so the operand looks like a heap Tuple* to those paths.
//   - Phase 4g.33: Array/List result with Inline Complex/Fraction operand
//     -- extract the native x64/r64 from the register slot and route
//     directly to the POD fast paths, no heap mirror.
//   - Otherwise: fall through to dispatchBinop unchanged.
template<typename Op>
static inline Word compositeBinopTopLevel(VM& vm, Op op, u16 a, u16 b,
                                          Type* aType, Type* bType, Type* resultType) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    bool aTupleInline = aTT && aType->repr_ == Type::Repr::Inline;
    bool bTupleInline = bTT && bType->repr_ == Type::Repr::Inline;
    if (rTT && (aTupleInline || bTupleInline)) {
        Word const* aBase = aTT ? (aTupleInline ? &vm.reg(a)
                                                : &static_cast<Tuple*>(vm.reg(a).o)->v[0])
                                : nullptr;
        Word const* bBase = bTT ? (bTupleInline ? &vm.reg(b)
                                                : &static_cast<Tuple*>(vm.reg(b).o)->v[0])
                                : nullptr;
        return dispatchTupleBinopBase(vm, op, aBase, bBase, vm.reg(a), vm.reg(b),
                                      aType, bType, rTT);
    }

    // Phase 4g.33/4g.34: bypass the heap-Complex/Fraction operand mirror
    // for Array result + Complex/Fraction broadcast. The Inline operand is
    // already at &vm.reg(a)/&vm.reg(b) as a 2-word native value; read it
    // directly, convert each source array element to x64/r64 in the loop,
    // and write straight to PodArray<x64>/<r64>. Covers both same-type
    // (Array<Complex>+Complex) and mixed-type (Array<Int|Float|Fraction>
    // +Complex -> Array<Complex>) cases without any heap allocations
    // beyond the result PodArray itself.
    auto* rAT = dynamic_cast<ArrayType*>(resultType);
    if (rAT) {
        Type* resultElem = rAT->elemType_;
        auto* aAT = dynamic_cast<ArrayType*>(aType);
        auto* bAT = dynamic_cast<ArrayType*>(bType);

        // Helper: read scalar x64 / r64 directly from register slot.
        auto readComplexFromReg = [&](u16 r) -> x64 {
            return x64(vm.reg(r).f, vm.reg(r+1).f);
        };
        auto readFractionFromReg = [&](u16 r) -> r64 {
            return r64(vm.reg(r).i, vm.reg(r+1).i, true);
        };

        // ----- Array<X> +/-/*// Complex scalar -> Array<Complex> -----
        if constexpr (bool(Op::flags & mathOpComplexArgs)) {
            if (resultElem == vm.complexType()) {
                if (aAT && bType == vm.complexType()) {
                    x64 bx = readComplexFromReg(b);
                    Type* elemA = aAT->elemType_;
                    Obj* arrA = vm.reg(a).o;
                    auto* r = new PodArray<x64>(rAT);
                    if (elemA == vm.complexType()) {
                        auto* pa = static_cast<PodArray<x64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(pa->v[i], bx);
                    } else if (elemA == vm.intType() || elemA == vm.boolType()) {
                        auto* pa = static_cast<PodArray<i64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(x64((f64)pa->v[i], 0.0), bx);
                    } else if (elemA == vm.floatType()) {
                        auto* pa = static_cast<PodArray<f64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(x64(pa->v[i], 0.0), bx);
                    } else if (elemA == vm.fractionType()) {
                        auto* pa = static_cast<PodArray<r64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(x64((f64)pa->v[i], 0.0), bx);
                    } else {
                        delete r; goto complexScalarFallthrough;
                    }
                    return Word(static_cast<Obj*>(r));
                }
                if (aType == vm.complexType() && bAT) {
                    x64 ax = readComplexFromReg(a);
                    Type* elemB = bAT->elemType_;
                    Obj* arrB = vm.reg(b).o;
                    auto* r = new PodArray<x64>(rAT);
                    if (elemB == vm.complexType()) {
                        auto* pb = static_cast<PodArray<x64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ax, pb->v[i]);
                    } else if (elemB == vm.intType() || elemB == vm.boolType()) {
                        auto* pb = static_cast<PodArray<i64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ax, x64((f64)pb->v[i], 0.0));
                    } else if (elemB == vm.floatType()) {
                        auto* pb = static_cast<PodArray<f64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ax, x64(pb->v[i], 0.0));
                    } else if (elemB == vm.fractionType()) {
                        auto* pb = static_cast<PodArray<r64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ax, x64((f64)pb->v[i], 0.0));
                    } else {
                        delete r; goto complexScalarFallthrough;
                    }
                    return Word(static_cast<Obj*>(r));
                }
            }
        }
        complexScalarFallthrough:;

        // ----- Array<X> +/-/*// Fraction scalar -> Array<Fraction> -----
        if constexpr (bool(Op::flags & mathOpFractionArgs)) {
            if (resultElem == vm.fractionType()) {
                if (aAT && bType == vm.fractionType()) {
                    r64 br = readFractionFromReg(b);
                    Type* elemA = aAT->elemType_;
                    Obj* arrA = vm.reg(a).o;
                    auto* r = new PodArray<r64>(rAT);
                    if (elemA == vm.fractionType()) {
                        auto* pa = static_cast<PodArray<r64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(pa->v[i], br);
                    } else if (elemA == vm.intType() || elemA == vm.boolType()) {
                        auto* pa = static_cast<PodArray<i64>*>(arrA);
                        r->v.resize(pa->v.size());
                        for (usize i = 0; i < pa->v.size(); ++i) r->v[i] = op(r64(pa->v[i]), br);
                    } else {
                        delete r; goto fractionScalarFallthrough;
                    }
                    return Word(static_cast<Obj*>(r));
                }
                if (aType == vm.fractionType() && bAT) {
                    r64 ar = readFractionFromReg(a);
                    Type* elemB = bAT->elemType_;
                    Obj* arrB = vm.reg(b).o;
                    auto* r = new PodArray<r64>(rAT);
                    if (elemB == vm.fractionType()) {
                        auto* pb = static_cast<PodArray<r64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ar, pb->v[i]);
                    } else if (elemB == vm.intType() || elemB == vm.boolType()) {
                        auto* pb = static_cast<PodArray<i64>*>(arrB);
                        r->v.resize(pb->v.size());
                        for (usize i = 0; i < pb->v.size(); ++i) r->v[i] = op(ar, r64(pb->v[i]));
                    } else {
                        delete r; goto fractionScalarFallthrough;
                    }
                    return Word(static_cast<Obj*>(r));
                }
            }
        }
        fractionScalarFallthrough:;
    }

    // Phase 4g.35: List + Inline Complex/Fraction broadcast -- construct
    // BinopListGen directly with the native scalar in broadcastSlots_, no
    // heap mirror. Per-cell math also bypasses dispatchBinop and writes
    // x64/r64 straight into the list cell's payload.
    auto* rLT = dynamic_cast<ListType*>(resultType);
    if (rLT) {
        Type* resultElem = rLT->elemType_;
        auto* aLT = dynamic_cast<ListType*>(aType);
        auto* bLT = dynamic_cast<ListType*>(bType);

        auto buildInlineBcast = [&](BinopListGen::BroadcastForm form,
                                    Word w0, Word w1) -> Word {
            BinopListGen::OpKind opKind;
            if constexpr (std::is_same<Op, OpAdd>::value) opKind = BinopListGen::Add;
            else if constexpr (std::is_same<Op, OpSub>::value) opKind = BinopListGen::Sub;
            else if constexpr (std::is_same<Op, OpMul>::value) opKind = BinopListGen::Mul;
            else opKind = BinopListGen::Div;

            ListNode* listSrc;
            Type* listElemType;
            Type* scalarType;
            bool scalarOnLeft;
            if (aLT) {
                listSrc = static_cast<ListNode*>(vm.reg(a).o);
                listElemType = aLT->elemType_;
                scalarType = bType;
                scalarOnLeft = false;
            } else {
                listSrc = static_cast<ListNode*>(vm.reg(b).o);
                listElemType = bLT->elemType_;
                scalarType = aType;
                scalarOnLeft = true;
            }
            if (listSrc == nullptr) return Word(static_cast<Obj*>(nullptr));

            auto* node = ListNode::create(rLT);
            auto* gen = new BinopListGen(vm.typeType());
            gen->opKind_ = opKind;
            gen->broadcastForm_ = form;
            gen->resultListType_ = rLT;
            gen->resultElemType_ = resultElem;
            gen->broadcastSlots_[0] = w0;
            gen->broadcastSlots_[1] = w1;
            gen->broadcastIsLeft_ = scalarOnLeft;
            gen->broadcastValIsObj_ = false;
            if (scalarOnLeft) {
                gen->leftList_ = nullptr;
                gen->rightList_ = listSrc;
                gen->leftElemType_ = scalarType;
                gen->rightElemType_ = listElemType;
            } else {
                gen->leftList_ = listSrc;
                gen->rightList_ = nullptr;
                gen->leftElemType_ = listElemType;
                gen->rightElemType_ = scalarType;
            }
            listSrc->retain();
            node->installGenerator(gen);
            return Word(static_cast<Obj*>(node));
        };

        if constexpr (bool(Op::flags & mathOpComplexArgs)) {
            if (resultElem == vm.complexType()) {
                if (aLT && bType == vm.complexType()) {
                    return buildInlineBcast(BinopListGen::BFComplex,
                                            vm.reg(b), vm.reg(b+1));
                }
                if (aType == vm.complexType() && bLT) {
                    return buildInlineBcast(BinopListGen::BFComplex,
                                            vm.reg(a), vm.reg(a+1));
                }
            }
        }
        if constexpr (bool(Op::flags & mathOpFractionArgs)) {
            if (resultElem == vm.fractionType()) {
                if (aLT && bType == vm.fractionType()) {
                    return buildInlineBcast(BinopListGen::BFFraction,
                                            vm.reg(b), vm.reg(b+1));
                }
                if (aType == vm.fractionType() && bLT) {
                    return buildInlineBcast(BinopListGen::BFFraction,
                                            vm.reg(a), vm.reg(a+1));
                }
            }
        }
    }

    // Array/List result -- box any remaining Inline operand (Tuple/Struct/
    // Enum, or mixed-type Complex/Fraction cases not caught above) so
    // dispatchBinop's single-Word path can broadcast/zip it scalar-style.
    bool aInline = aType && aType->repr_ == Type::Repr::Inline;
    bool bInline = bType && bType->repr_ == Type::Repr::Inline;
    Word aw = vm.reg(a), bw = vm.reg(b);
    if (aInline) aw = boxPayload(vm, aType, &vm.reg(a));
    if (bInline) bw = boxPayload(vm, bType, &vm.reg(b));
    return dispatchBinop(vm, op, aw, bw, aType, bType, resultType);
}

template<typename Op>
static inline Word compositeUnaryOpTopLevel(VM& vm, Op op, u16 a,
                                            Type* aType, Type* resultType) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    bool aInline = aTT && aType->repr_ == Type::Repr::Inline;
    if (rTT && aInline) {
        return dispatchTupleUnaryOpBase(vm, op, &vm.reg(a), aType, rTT);
    }
    Word aw = vm.reg(a);
    bool aFullInline = aType && aType->repr_ == Type::Repr::Inline;
    if (aFullInline) aw = boxPayload(vm, aType, &vm.reg(a));
    return dispatchUnaryOp(vm, op, aw, aType, resultType);
}

// ADD_COMPOSITE Rd, Ra, Rb (5 words: op, regs, resultType*, aType*, bType*)
void op_add_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = compositeBinopTopLevel(vm, OpAdd{}, a, b, aType, bType, resultType);
    DISPATCH(5);
}

void op_sub_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = compositeBinopTopLevel(vm, OpSub{}, a, b, aType, bType, resultType);
    DISPATCH(5);
}

void op_mul_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = compositeBinopTopLevel(vm, OpMul{}, a, b, aType, bType, resultType);
    DISPATCH(5);
}

void op_div_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    Type* bType = static_cast<Type*>(pc[4].p);
    vm.reg(dst) = compositeBinopTopLevel(vm, OpDiv{}, a, b, aType, bType, resultType);
    DISPATCH(5);
}

// NEG_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_neg_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = compositeUnaryOpTopLevel(vm, OpNeg{}, a, aType, resultType);
    DISPATCH(4);
}

// NOT_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_not_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = compositeUnaryOpTopLevel(vm, OpNot{}, a, aType, resultType);
    DISPATCH(4);
}

// BITNOT_COMPOSITE Rd, Ra (4 words: op, regs, resultType*, aType*)
void op_bitnot_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1];
    Type* resultType = static_cast<Type*>(pc[2].p);
    Type* aType = static_cast<Type*>(pc[3].p);
    vm.reg(dst) = compositeUnaryOpTopLevel(vm, OpBitNot{}, a, aType, resultType);
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
    Word const* aBase = aTT ? &static_cast<Tuple*>(a.o)->v[0] : nullptr;
    Word const* bBase = bTT ? &static_cast<Tuple*>(b.o)->v[0] : nullptr;
    return dispatchCmpTupleBinopBase(vm, op, aBase, bBase, a, b, aType, bType, resultTT);
}

template<typename CmpOp>
static Word dispatchCmpTupleBinopBase(VM& vm, CmpOp op,
                                      Word const* aBase, Word const* bBase,
                                      Word aScalar, Word bScalar,
                                      Type* aType, Type* bType, TupleType* resultTT) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    usize n = resultTT->fields_.size();
    auto* result = Tuple::create(resultTT, (u32)n);
    for (usize i = 0; i < n; ++i) {
        Word ae = aTT ? readTupleField(vm, aBase, aTT, i) : aScalar;
        Word be = bTT ? readTupleField(vm, bBase, bTT, i) : bScalar;
        Type* aet = aTT ? aTT->fields_[i] : aType;
        Type* bet = bTT ? bTT->fields_[i] : bType;
        Word r = dispatchCmpBinop(vm, op, ae, be, aet, bet, resultTT->fields_[i]);
        writeTupleField(vm, result, resultTT, i, r);
    }
    inlineWalkPointers(&result->v[0], resultTT, /*release_=*/false);
    return Word(static_cast<Obj*>(result));
}

template<typename CmpOp>
static Word dispatchCmpArrayBinop(VM& vm, CmpOp op, Word a, Word b,
                                  Type* aType, Type* bType, ArrayType* resultAT) {
    Type* resultElem = resultAT->elemType_;
    auto* aAT = dynamic_cast<ArrayType*>(aType);
    auto* bAT = dynamic_cast<ArrayType*>(bType);

    if (aAT && bAT) {
        Type* elemA = aAT->elemType_;
        Type* elemB = bAT->elemType_;
        ArrayBackend ba = arrayBackendFor(elemA);
        ArrayBackend bb = arrayBackendFor(elemB);
        ArrayBackend br = arrayBackendFor(resultElem);
        usize len = std::min(arrayLen(a.o, ba), arrayLen(b.o, bb));
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, ba, elemA, vm);
            Word be = readElem(b.o, i, bb, elemB, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, ae, be, elemA, elemB, resultElem), br, resultElem, vm);
        }
        return Word(result);
    }
    if (aAT) {
        Type* elemA = aAT->elemType_;
        ArrayBackend ba = arrayBackendFor(elemA);
        ArrayBackend br = arrayBackendFor(resultElem);
        usize len = arrayLen(a.o, ba);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word ae = readElem(a.o, i, ba, elemA, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, ae, b, elemA, bType, resultElem), br, resultElem, vm);
        }
        return Word(result);
    }
    if (bAT) {
        Type* elemB = bAT->elemType_;
        ArrayBackend bb = arrayBackendFor(elemB);
        ArrayBackend br = arrayBackendFor(resultElem);
        usize len = arrayLen(b.o, bb);
        Obj* result = makeArray(vm, resultAT, len);
        for (usize i = 0; i < len; ++i) {
            Word be = readElem(b.o, i, bb, elemB, vm);
            writeElem(result, i, dispatchCmpBinop(vm, op, a, be, aType, elemB, resultElem), br, resultElem, vm);
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

// Phase 4g.16: top-level entry for heap-result composite cmp. Routes
// Inline-Tuple operands to the base-pointer dispatcher; non-Tuple operands
// fall through to dispatchCmpBinop (which handles Array/scalar paths).
template<typename CmpOp>
static inline Word compositeCmpTopLevel(VM& vm, CmpOp op, u16 a, u16 b,
                                        Type* aType, Type* bType, Type* resultType) {
    auto* aTT = dynamic_cast<TupleType*>(aType);
    auto* bTT = dynamic_cast<TupleType*>(bType);
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    bool aInline = aTT && aType->repr_ == Type::Repr::Inline;
    bool bInline = bTT && bType->repr_ == Type::Repr::Inline;
    if (rTT && (aInline || bInline)) {
        Word const* aBase = aTT ? (aInline ? &vm.reg(a)
                                           : &static_cast<Tuple*>(vm.reg(a).o)->v[0])
                                : nullptr;
        Word const* bBase = bTT ? (bInline ? &vm.reg(b)
                                           : &static_cast<Tuple*>(vm.reg(b).o)->v[0])
                                : nullptr;
        return dispatchCmpTupleBinopBase(vm, op, aBase, bBase, vm.reg(a), vm.reg(b),
                                         aType, bType, rTT);
    }
    // Array result -- box any Inline tuple operand for the Word-based path.
    Word aw = vm.reg(a), bw = vm.reg(b);
    if (aInline) aw = boxPayload(vm, aType, &vm.reg(a));
    if (bInline) bw = boxPayload(vm, bType, &vm.reg(b));
    return dispatchCmpBinop(vm, op, aw, bw, aType, bType, resultType);
}

// CMP_XX_COMPOSITE Rd, Ra, Rb (5 words: op, regs, resultType*, aType*, bType*)
void op_cmp_eq_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpEq{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_ne_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpNe{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_lt_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpLt{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_le_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpLe{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_gt_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpGt{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}
void op_cmp_ge_composite(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];
    vm.reg(dst) = compositeCmpTopLevel(vm, OpCmpGe{}, a, b,
        static_cast<Type*>(pc[3].p), static_cast<Type*>(pc[4].p), static_cast<Type*>(pc[2].p));
    DISPATCH(5);
}

// --- Inline-storage composite arithmetic (Phase 4g.7) ---
// Walk an Inline result type's layout, dispatching scalar ops directly into
// the destination register slot. Operands may be Inline (read at layout
// offsets), heap Tuples (read via .o->v[i]), or scalars (broadcast).
template<typename Op>
static void dispatchInlineBinopWalk(VM& vm, Op op,
    Word* dst, Word const* aSlot, Word const* bSlot,
    Type* resultType, Type* aType, Type* bType)
{
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    if (rTT && rTT->repr_ == ts::Type::Repr::Inline) {
        auto* aTT = dynamic_cast<TupleType*>(aType);
        auto* bTT = dynamic_cast<TupleType*>(bType);
        u32 n = (u32)rTT->fields_.size();
        for (u32 i = 0; i < n; ++i) {
            Type* rft = rTT->fields_[i];
            u32 rOff = rTT->layout_[i].wordOffset;

            Type* aft; Word const* aSub;
            if (aTT && aTT->repr_ == ts::Type::Repr::Inline) {
                aft = aTT->fields_[i];
                aSub = aSlot + aTT->layout_[i].wordOffset;
            } else if (aTT) {
                aft = aTT->fields_[i];
                aSub = &static_cast<Tuple*>(aSlot[0].o)->v[aTT->layout_[i].wordOffset];
            } else {
                aft = aType;
                aSub = aSlot;
            }
            Type* bft; Word const* bSub;
            if (bTT && bTT->repr_ == ts::Type::Repr::Inline) {
                bft = bTT->fields_[i];
                bSub = bSlot + bTT->layout_[i].wordOffset;
            } else if (bTT) {
                bft = bTT->fields_[i];
                bSub = &static_cast<Tuple*>(bSlot[0].o)->v[bTT->layout_[i].wordOffset];
            } else {
                bft = bType;
                bSub = bSlot;
            }
            dispatchInlineBinopWalk(vm, op, dst + rOff, aSub, bSub, rft, aft, bft);
        }
        return;
    }
    // Leaf: scalar dispatch. dispatchBinop handles Int/Float/Bool/Fraction/Complex.
    Word r = dispatchBinop(vm, op, *aSlot, *bSlot, aType, bType, resultType);
    dst[0] = r;
    // Retain newly-allocated Obj* leaves (Fraction/Complex) to cancel the
    // auto-release pool's pending release -- mirrors dispatchTupleBinop's
    // postprocess (Phase 4g.5).
    if (storesObjPtr(resultType) && r.o) {
        r.o->retain();
    }
}

template<typename Op>
static void dispatchInlineUnaryWalk(VM& vm, Op op,
    Word* dst, Word const* aSlot,
    Type* resultType, Type* aType)
{
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    if (rTT && rTT->repr_ == ts::Type::Repr::Inline) {
        auto* aTT = dynamic_cast<TupleType*>(aType);
        u32 n = (u32)rTT->fields_.size();
        for (u32 i = 0; i < n; ++i) {
            Type* rft = rTT->fields_[i];
            u32 rOff = rTT->layout_[i].wordOffset;
            Type* aft; Word const* aSub;
            if (aTT && aTT->repr_ == ts::Type::Repr::Inline) {
                aft = aTT->fields_[i];
                aSub = aSlot + aTT->layout_[i].wordOffset;
            } else if (aTT) {
                aft = aTT->fields_[i];
                aSub = &static_cast<Tuple*>(aSlot[0].o)->v[aTT->layout_[i].wordOffset];
            } else {
                aft = aType;
                aSub = aSlot;
            }
            dispatchInlineUnaryWalk(vm, op, dst + rOff, aSub, rft, aft);
        }
        return;
    }
    Word r = dispatchUnaryOp(vm, op, *aSlot, aType, resultType);
    dst[0] = r;
    if (storesObjPtr(resultType) && r.o) {
        r.o->retain();
    }
}

template<typename CmpOp>
static void dispatchInlineCmpBinopWalk(VM& vm, CmpOp op,
    Word* dst, Word const* aSlot, Word const* bSlot,
    Type* resultType, Type* aType, Type* bType)
{
    auto* rTT = dynamic_cast<TupleType*>(resultType);
    if (rTT && rTT->repr_ == ts::Type::Repr::Inline) {
        auto* aTT = dynamic_cast<TupleType*>(aType);
        auto* bTT = dynamic_cast<TupleType*>(bType);
        u32 n = (u32)rTT->fields_.size();
        for (u32 i = 0; i < n; ++i) {
            Type* rft = rTT->fields_[i];
            u32 rOff = rTT->layout_[i].wordOffset;

            Type* aft; Word const* aSub;
            if (aTT && aTT->repr_ == ts::Type::Repr::Inline) {
                aft = aTT->fields_[i];
                aSub = aSlot + aTT->layout_[i].wordOffset;
            } else if (aTT) {
                aft = aTT->fields_[i];
                aSub = &static_cast<Tuple*>(aSlot[0].o)->v[aTT->layout_[i].wordOffset];
            } else {
                aft = aType;
                aSub = aSlot;
            }
            Type* bft; Word const* bSub;
            if (bTT && bTT->repr_ == ts::Type::Repr::Inline) {
                bft = bTT->fields_[i];
                bSub = bSlot + bTT->layout_[i].wordOffset;
            } else if (bTT) {
                bft = bTT->fields_[i];
                bSub = &static_cast<Tuple*>(bSlot[0].o)->v[bTT->layout_[i].wordOffset];
            } else {
                bft = bType;
                bSub = bSlot;
            }
            dispatchInlineCmpBinopWalk(vm, op, dst + rOff, aSub, bSub, rft, aft, bft);
        }
        return;
    }
    Word r = dispatchCmpBinop(vm, op, *aSlot, *bSlot, aType, bType, resultType);
    dst[0] = r;
    if (storesObjPtr(resultType) && r.o) {
        r.o->retain();
    }
}

#define INLINE_BINOP_OP(NAME, OP)                                                   \
    void NAME(VM& vm, Code* pc) {                                                   \
        u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];              \
        Type* resultType = static_cast<Type*>(pc[2].p);                             \
        Type* aType = static_cast<Type*>(pc[3].p);                                  \
        Type* bType = static_cast<Type*>(pc[4].p);                                  \
        dispatchInlineBinopWalk(vm, OP{}, &vm.reg(dst), &vm.reg(a), &vm.reg(b),     \
                                resultType, aType, bType);                          \
        DISPATCH(5);                                                                \
    }

INLINE_BINOP_OP(op_add_composite_inline, OpAdd)
INLINE_BINOP_OP(op_sub_composite_inline, OpSub)
INLINE_BINOP_OP(op_mul_composite_inline, OpMul)
INLINE_BINOP_OP(op_div_composite_inline, OpDiv)

#undef INLINE_BINOP_OP

#define INLINE_UNARY_OP(NAME, OP)                                                   \
    void NAME(VM& vm, Code* pc) {                                                   \
        u16 dst = pc[1].regs[0], a = pc[1].regs[1];                                 \
        Type* resultType = static_cast<Type*>(pc[2].p);                             \
        Type* aType = static_cast<Type*>(pc[3].p);                                  \
        dispatchInlineUnaryWalk(vm, OP{}, &vm.reg(dst), &vm.reg(a),                 \
                                resultType, aType);                                 \
        DISPATCH(4);                                                                \
    }

INLINE_UNARY_OP(op_neg_composite_inline, OpNeg)
INLINE_UNARY_OP(op_not_composite_inline, OpNot)
INLINE_UNARY_OP(op_bitnot_composite_inline, OpBitNot)

#undef INLINE_UNARY_OP

#define INLINE_CMP_OP(NAME, OP)                                                     \
    void NAME(VM& vm, Code* pc) {                                                   \
        u16 dst = pc[1].regs[0], a = pc[1].regs[1], b = pc[1].regs[2];              \
        Type* resultType = static_cast<Type*>(pc[2].p);                             \
        Type* aType = static_cast<Type*>(pc[3].p);                                  \
        Type* bType = static_cast<Type*>(pc[4].p);                                  \
        dispatchInlineCmpBinopWalk(vm, OP{}, &vm.reg(dst), &vm.reg(a), &vm.reg(b),  \
                                   resultType, aType, bType);                       \
        DISPATCH(5);                                                                \
    }

INLINE_CMP_OP(op_cmp_eq_composite_inline, OpCmpEq)
INLINE_CMP_OP(op_cmp_ne_composite_inline, OpCmpNe)
INLINE_CMP_OP(op_cmp_lt_composite_inline, OpCmpLt)
INLINE_CMP_OP(op_cmp_le_composite_inline, OpCmpLe)
INLINE_CMP_OP(op_cmp_gt_composite_inline, OpCmpGt)
INLINE_CMP_OP(op_cmp_ge_composite_inline, OpCmpGe)

#undef INLINE_CMP_OP

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

// Phase 4g.35: read a list head as a native x64, converting from the cell's
// natural element representation. The list cell stores the element either
// inline at headData()[0..payloadWords_-1] (multi-word: Complex/Fraction)
// or in head_ (single-word: Int/Float/Fraction-as-r64-in-2-words/Complex-
// as-x64-in-2-words). Caller guarantees elemType is a numeric scalar so
// the conversion is well-defined.
static x64 readListHeadAsX64(VM& vm, ListNode* node, Type* elemType) {
    if (elemType == vm.complexType()) {
        Word const* dh = node->headData();
        return x64(dh[0].f, dh[1].f);
    }
    if (elemType == vm.fractionType()) {
        Word const* dh = node->headData();
        return x64((f64)r64(dh[0].i, dh[1].i, true), 0.0);
    }
    if (elemType == vm.floatType()) return x64(node->head_.f, 0.0);
    return x64((f64)node->head_.i, 0.0);
}

static r64 readListHeadAsR64(VM& vm, ListNode* node, Type* elemType) {
    if (elemType == vm.fractionType()) {
        Word const* dh = node->headData();
        return r64(dh[0].i, dh[1].i, true);
    }
    return r64(node->head_.i);
}

static x64 applyOpX64(BinopListGen::OpKind kind, x64 a, x64 b) {
    switch (kind) {
        case BinopListGen::Add: return a + b;
        case BinopListGen::Sub: return a - b;
        case BinopListGen::Mul: return a * b;
        case BinopListGen::Div: return a / b;
    }
    return x64();
}

static r64 applyOpR64(BinopListGen::OpKind kind, r64 a, r64 b) {
    switch (kind) {
        case BinopListGen::Add: return a + b;
        case BinopListGen::Sub: return a - b;
        case BinopListGen::Mul: return a * b;
        case BinopListGen::Div: return a / b;
    }
    return r64(0);
}

void BinopListGen::generate(VM& vm, ListNode* owner) {
    // Force source nodes if they are lazy
    if (leftList_) leftList_->force(vm);
    if (rightList_) rightList_->force(vm);

    // Phase 4g.35: fast path for Inline Complex/Fraction broadcast. The
    // broadcast value lives natively in broadcastSlots_, no heap mirror.
    // Read the list head as a native scalar, apply the op natively, write
    // the result directly into owner->headData() -- no per-cell heap
    // Complex/Fraction allocated.
    if (broadcastForm_ == BFComplex) {
        ListNode* listSrc = leftList_ ? leftList_ : rightList_;
        Type* listElemType = leftList_ ? leftElemType_ : rightElemType_;
        x64 bx(broadcastSlots_[0].f, broadcastSlots_[1].f);
        x64 lx = readListHeadAsX64(vm, listSrc, listElemType);
        x64 r = broadcastIsLeft_ ? applyOpX64(opKind_, bx, lx)
                                 : applyOpX64(opKind_, lx, bx);
        Word* dh = owner->headData();
        dh[0].f = r.real();
        dh[1].f = r.imag();

        ListNode* nextSrc = listSrc->tail_;
        if (nextSrc == nullptr) {
            owner->tail_ = nullptr;
        } else {
            auto* tailNode = ListNode::create(resultListType_);
            nextSrc->retain();
            listSrc->release();
            if (leftList_) leftList_ = nextSrc; else rightList_ = nextSrc;
            tailNode->installGenerator(this);
            owner->tail_ = tailNode;
            tailNode->retain();
        }
        return;
    }
    if (broadcastForm_ == BFFraction) {
        ListNode* listSrc = leftList_ ? leftList_ : rightList_;
        Type* listElemType = leftList_ ? leftElemType_ : rightElemType_;
        r64 br(broadcastSlots_[0].i, broadcastSlots_[1].i, true);
        r64 lr = readListHeadAsR64(vm, listSrc, listElemType);
        r64 r = broadcastIsLeft_ ? applyOpR64(opKind_, br, lr)
                                 : applyOpR64(opKind_, lr, br);
        Word* dh = owner->headData();
        dh[0].i = r.numer();
        dh[1].i = r.denom();

        ListNode* nextSrc = listSrc->tail_;
        if (nextSrc == nullptr) {
            owner->tail_ = nullptr;
        } else {
            auto* tailNode = ListNode::create(resultListType_);
            nextSrc->retain();
            listSrc->release();
            if (leftList_) leftList_ = nextSrc; else rightList_ = nextSrc;
            tailNode->installGenerator(this);
            owner->tail_ = tailNode;
            tailNode->retain();
        }
        return;
    }

    // Determine head operands. Phase 4g.20: list heads of Inline composite
    // element types (Tuple/Struct/Enum and Complex/Fraction) are stored as
    // multi-word native data; box them into a 1-Word heap Obj* before
    // calling the scalar dispatcher.
    Word leftVal, rightVal;
    Type* leftType = leftElemType_;
    Type* rightType = rightElemType_;

    auto readListHead = [&](ListNode* node, Type* elemType) -> Word {
        if (node->payloadWords_ > 1) {
            return boxPayload(vm, elemType, node->headData());
        }
        return node->head_;
    };

    if (leftList_ && rightList_) {
        // Zip: both are lists
        leftVal = readListHead(leftList_, leftElemType_);
        rightVal = readListHead(rightList_, rightElemType_);
    } else if (leftList_) {
        // List op Scalar: list is left, scalar is broadcast (right)
        leftVal = readListHead(leftList_, leftElemType_);
        rightVal = broadcastVal_;
        rightType = rightElemType_;
    } else {
        // Scalar op List: scalar is broadcast (left), list is right
        leftVal = broadcastVal_;
        rightVal = readListHead(rightList_, rightElemType_);
        leftType = leftElemType_;
    }

    // Compute the head value. Phase 4g.9: when the result element is an
    // Inline composite and the node has multi-word stride, unbox the
    // dispatcher's freshly-boxed Tuple* into the flex head storage.
    Word headResult = dispatchBinopByKind(vm, opKind_, leftVal, rightVal,
                                          leftType, rightType, resultElemType_);
    if (owner->payloadWords_ > 1) {
        unboxInlineDeepTo(vm, resultElemType_, headResult.o, owner->headData());
    } else {
        owner->head_ = headResult;
        if (storesObjPtr(resultElemType_) && owner->head_.o) owner->head_.o->retain();
    }

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

    if (atEnd) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = ListNode::create(resultListType_);
        // Retain new, release old for field mutations
        if (nextLeft) nextLeft->retain();
        if (leftList_) leftList_->release();
        leftList_ = nextLeft;
        if (nextRight) nextRight->retain();
        if (rightList_) rightList_->release();
        rightList_ = nextRight;
        // Retain generator and tail for owner
        tailNode->installGenerator(this);
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

void UnaryListGen::generate(VM& vm, ListNode* owner) {
    // Force source node if lazy
    source_->force(vm);

    // Phase 4g.20: box the multi-word native head into a 1-Word heap Obj*
    // for the scalar dispatcher.
    Word sourceHead = (source_->payloadWords_ > 1)
        ? boxPayload(vm, sourceElemType_, source_->headData())
        : source_->head_;

    // Compute head via the appropriate unary operation
    Word headResult;
    switch (opKind_) {
        case Neg:
            headResult = dispatchUnaryOp(vm, OpNeg{}, sourceHead,
                                         sourceElemType_, resultElemType_);
            break;
        case Not:
            headResult = dispatchUnaryOp(vm, OpNot{}, sourceHead,
                                         sourceElemType_, resultElemType_);
            break;
        case BitNot:
            headResult = dispatchUnaryOp(vm, OpBitNot{}, sourceHead,
                                         sourceElemType_, resultElemType_);
            break;
    }
    // Phase 4g.9: store the freshly-computed head into the node, unboxing
    // into multi-word head storage when the element type is Inline composite.
    if (owner->payloadWords_ > 1) {
        unboxInlineDeepTo(vm, resultElemType_, headResult.o, owner->headData());
    } else {
        owner->head_ = headResult;
        if (storesObjPtr(resultElemType_) && owner->head_.o) owner->head_.o->retain();
    }

    // Create lazy tail
    ListNode* nextSource = source_->tail_;
    if (nextSource == nullptr) {
        owner->tail_ = nullptr;
    } else {
        auto* tailNode = ListNode::create(resultListType_);
        if (nextSource) nextSource->retain();
        if (source_) source_->release();
        source_ = nextSource;
        tailNode->installGenerator(this);
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
        auto* tailNode = ListNode::create(listType_);
        current_ = next;
        tailNode->installGenerator(this);
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
        // Phase 4g.20: Fraction list head is now native 2-word; zero both.
        Word* h = owner->headData();
        h[0].i = 0; h[1].i = 0;
        owner->tail_ = nullptr;
        return;
    }

    // Set head to current value (native 2-word: numer, denom).
    Word* h = owner->headData();
    h[0].i = cur.numer();
    h[1].i = cur.denom();

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
        auto* tailNode = ListNode::create(listType_);
        auto* oldCurrent = current_;
        current_ = new Fraction(next);
        current_->retain();
        if (oldCurrent) oldCurrent->release();
        tailNode->installGenerator(this);
        owner->tail_ = tailNode;
        tailNode->retain();
    }
}

// --- Tuple Access/Construction ---

// MAKE_ARRAY Rd, firstSrc, numElems (3 words: op, regs{dst, firstSrc, numElems}, ArrayType*)
//
// Phase 4e: Complex / Fraction elements are read as 2 consecutive Words per
// element from firstSrc, so each i strides by sizeWords (2 for those types).
// Codegen lays the elements out in the same multi-word stride.
void op_make_array(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numElems = pc[1].regs[2];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrayType);
            arr->v.resize(numElems);
            for (u16 i = 0; i < numElems; ++i) {
                f64 re = vm.reg((u16)(firstSrc + i * 2)).f;
                f64 im = vm.reg((u16)(firstSrc + i * 2 + 1)).f;
                arr->v[i] = x64(re, im);
            }
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrayType);
            arr->v.resize(numElems);
            for (u16 i = 0; i < numElems; ++i) {
                i64 n = vm.reg((u16)(firstSrc + i * 2)).i;
                i64 d = vm.reg((u16)(firstSrc + i * 2 + 1)).i;
                arr->v[i] = r64(n, d, true);  // already-canonical from inline rep
            }
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrayType);
            arr->v.resize(numElems);
            for (u16 i = 0; i < numElems; ++i)
                arr->v[i] = vm.reg(firstSrc + i).i;
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrayType);
            arr->v.resize(numElems);
            for (u16 i = 0; i < numElems; ++i)
                arr->v[i] = vm.reg(firstSrc + i).f;
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(arrayType);
            u32 sw = arr->stride();
            arr->reserve(numElems);
            for (u16 i = 0; i < numElems; ++i) {
                arr->pushSlot(&vm.reg((u16)(firstSrc + i * sw)));
            }
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrayType);
            arr->reserve(numElems);
            for (u16 i = 0; i < numElems; ++i) {
                arr->push(vm.reg(firstSrc + i).o);
            }
            vm.reg(dst).o = arr;
            DISPATCH(3);
        }
    }
    DISPATCH(3);
}

// TUPLE_GET Rd, Ra, fieldIdx (3 words: op, regs{dst, src, fieldIdx}, TupleType*)
//
// Phase 4g.13: heap Tuple stores fields at layout-aware offsets. Copy
// layout_[fieldIdx].sizeWords words from tuple->v[layout.wordOffset..]
// into dst..dst+sizeWords-1.
void op_tuple_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* tupleType = static_cast<TupleType*>(pc[2].p);
    auto* tuple = static_cast<Tuple*>(vm.reg(src).o);
    auto const& f = tupleType->layout_[fieldIdx];
    for (u8 i = 0; i < f.sizeWords; ++i) {
        vm.reg((u16)(dst + i)) = tuple->v[f.wordOffset + i];
    }
    DISPATCH(3);
}

// INLINE_TUPLE_GET Rd, Ra, fieldIdx (3 words: op, regs, TupleType*)
//
// Phase 4g.2: parent slot src..src+sizeWords-1 holds the inline tuple value;
// copy the field's payload (sizeWords words from layout_[fieldIdx]) into
// dst..dst+fieldSizeWords-1.
void op_inline_tuple_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* tupleType = static_cast<TupleType*>(pc[2].p);
    auto const& f = tupleType->layout_[fieldIdx];
    for (u8 i = 0; i < f.sizeWords; ++i) {
        vm.reg((u16)(dst + i)) = vm.reg((u16)(src + f.wordOffset + i));
    }
    DISPATCH(3);
}

// TUPLE_SLICE Rd, Ra, startIdx (3 words: op, regs{dst, src, startIdx}, TupleType*)
// Creates a new tuple from elements [startIdx..end) of the source tuple.
//
// Phase 4g.13: copy fields by layout-aware multi-word stride so inline
// composite fields are preserved natively.
void op_tuple_slice(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], startIdx = pc[1].regs[2];
    auto* resultType = static_cast<TupleType*>(pc[2].p);
    auto* srcTuple = static_cast<Tuple*>(vm.reg(src).o);
    auto* srcType = static_cast<TupleType*>(srcTuple->type_);
    u32 count = srcTuple->numFields_ - startIdx;
    auto* newTuple = Tuple::create(resultType, count);
    for (size_t i = 0; i < count; ++i) {
        auto const& fSrc = srcType->layout_[startIdx + i];
        auto const& fDst = resultType->layout_[i];
        for (u8 j = 0; j < fDst.sizeWords; ++j) {
            newTuple->v[fDst.wordOffset + j] = srcTuple->v[fSrc.wordOffset + j];
        }
    }
    inlineWalkPointers(&newTuple->v[0], resultType, /*release_=*/false);
    vm.reg(dst).o = newTuple;
    DISPATCH(3);
}

// MAKE_TUPLE_HEAP Rd, firstSrc, numFields (3 words: op, regs, TupleType*)
//
// Phase 4g.13: caller places fields contiguously at multi-word stride per
// layout_. Copy total layout words natively into the new heap Tuple. Used
// for variadic packs and any heap-Tuple construction site.
void op_make_tuple_heap(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numFields = pc[1].regs[2];
    auto* tupleType = static_cast<TupleType*>(pc[2].p);
    auto* tuple = Tuple::create(tupleType, numFields);
    u32 total = 0;
    for (auto const& f : tupleType->layout_) total += f.sizeWords;
    for (u32 i = 0; i < total; ++i) {
        tuple->v[i] = vm.reg((u16)(firstSrc + i));
    }
    inlineWalkPointers(&tuple->v[0], tupleType, /*release_=*/false);
    vm.reg(dst).o = tuple;
    DISPATCH(3);
}

void op_make_tuple(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numFields = pc[1].regs[2];
    auto* tupleType = static_cast<TupleType*>(pc[2].p);
    if (tupleType->repr_ == Type::Repr::Inline) {
        u16 n = tupleType->sizeWords_;
        for (u16 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(firstSrc + i));
        DISPATCH(3);
    }
    auto* tuple = Tuple::create(tupleType, numFields);
    u32 total = 0;
    for (auto const& f : tupleType->layout_) total += f.sizeWords;
    for (u32 i = 0; i < total; ++i) {
        tuple->v[i] = vm.reg((u16)(firstSrc + i));
    }
    inlineWalkPointers(&tuple->v[0], tupleType, /*release_=*/false);
    vm.reg(dst).o = tuple;
    DISPATCH(3);
}

// MAKE_STRUCT Rd, firstSrc, numFields (3 words: op, regs, StructType*)
//
// Phase 4g.13: caller places fields contiguously at multi-word stride per
// layout_; Heap structs store fields natively just like Inline structs.
void op_make_struct(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numFields = pc[1].regs[2];
    auto* structType = static_cast<StructType*>(pc[2].p);
    if (structType->repr_ == Type::Repr::Inline) {
        u16 n = structType->sizeWords_;
        for (u16 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(firstSrc + i));
        DISPATCH(3);
    }
    auto* s = Struct::create(structType, numFields);
    u32 total = 0;
    for (auto const& f : structType->layout_) total += f.sizeWords;
    for (u32 i = 0; i < total; ++i) {
        s->v[i] = vm.reg((u16)(firstSrc + i));
    }
    inlineWalkPointers(&s->v[0], structType, /*release_=*/false);
    vm.reg(dst).o = s;
    DISPATCH(3);
}

// STRUCT_GET Rd, Ra, fieldIdx (3 words: op, regs{dst, src, fieldIdx}, StructType*)
//
// Phase 4g.13: heap Struct stores fields at layout-aware offsets. Copy
// layout_[fieldIdx].sizeWords words natively into dst.
void op_struct_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* structType = static_cast<StructType*>(pc[2].p);
    auto* s = static_cast<Struct*>(vm.reg(src).o);
    auto const& f = structType->layout_[fieldIdx];
    for (u8 i = 0; i < f.sizeWords; ++i) {
        vm.reg((u16)(dst + i)) = s->v[f.wordOffset + i];
    }
    DISPATCH(3);
}

// INLINE_STRUCT_GET Rd, Ra, fieldIdx (3 words: op, regs, StructType*)
//
// Phase 4g.2: parent slot src..src+sizeWords-1 holds the inline struct;
// copy the field's payload from layout_[fieldIdx] into dst.
void op_inline_struct_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1], fieldIdx = pc[1].regs[2];
    auto* structType = static_cast<StructType*>(pc[2].p);
    auto const& f = structType->layout_[fieldIdx];
    for (u8 i = 0; i < f.sizeWords; ++i) {
        vm.reg((u16)(dst + i)) = vm.reg((u16)(src + f.wordOffset + i));
    }
    DISPATCH(3);
}

// MAKE_ENUM Rd, valSrc, caseIdx (3 words: op, regs{dst, valSrc, caseIdx}, EnumType*)
//
// Phase 4g.15: valSrc points to a register range holding the case payload
// natively (layout_[caseIdx].sizeWords consecutive Words). Copy into the
// heap Enum's v[] and ARC-retain embedded Obj* fields.
void op_make_enum(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valSrc = pc[1].regs[1], caseIdx = pc[1].regs[2];
    auto* enumType = static_cast<EnumType*>(pc[2].p);
    auto* e = Enum::create(enumType, caseIdx);
    if ((size_t)caseIdx < enumType->layout_.size()) {
        auto const& f = enumType->layout_[caseIdx];
        if (f.type && f.sizeWords > 0) {
            for (u8 i = 0; i < f.sizeWords; ++i) {
                e->v[i] = vm.reg((u16)(valSrc + i));
            }
            payloadRetain(&e->v[0], f.type);
        }
    }
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// MAKE_ENUM_NODATA Rd, caseIdx (3 words: op, regs{dst, caseIdx}, EnumType*)
void op_make_enum_nodata(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], caseIdx = pc[1].regs[1];
    auto* enumType = static_cast<EnumType*>(pc[2].p);
    auto* e = Enum::create(enumType, caseIdx);
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

// ENUM_GET_VALUE Rd, Ra (3 words: op, regs{dst, src}, caseType*)
//
// Phase 4g.15: heap Enum payload is stored natively in e->v[]. Copy the
// case's payload (caseType->sizeWords_ words) into dst[0..sizeWords).
void op_enum_get_value(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* caseType = static_cast<Type*>(pc[2].p);
    auto* e = static_cast<Enum*>(vm.reg(src).o);
    u32 sw = (caseType && caseType->sizeWords_ > 0) ? caseType->sizeWords_ : 1;
    for (u32 i = 0; i < sw; ++i) vm.reg((u16)(dst + i)) = e->v[i];
    DISPATCH(3);
}

// --- Inline Enum Construction (Phase 4g.4) ---
// MAKE_INLINE_ENUM Rd, valSrc, caseIdx (3 words: op, regs, EnumType*)
// Lay out an inline enum slot at dst:
//   dst[0].i = caseIdx
//   dst[1..1+P] = payload copied from valSrc (P = layout_[caseIdx].sizeWords)
// Inline-composite payload fields are already inline at valSrc, so a flat
// MOVE_N suffices (the layout's sizeWords is the total payload footprint).
// Embedded Obj* fields are retained via inlineWalkPointers on the destination.
void op_make_inline_enum(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valSrc = pc[1].regs[1], caseIdx = pc[1].regs[2];
    auto* en = static_cast<EnumType*>(pc[2].p);
    vm.reg(dst).i = caseIdx;
    // Zero-fill the payload region so unused tail words start clean.
    for (u8 i = 1; i < en->sizeWords_; ++i) vm.reg((u16)(dst + i)).i = 0;
    if (caseIdx < en->layout_.size()) {
        auto const& f = en->layout_[caseIdx];
        if (f.type && f.sizeWords > 0) {
            for (u8 i = 0; i < f.sizeWords; ++i) {
                vm.reg((u16)(dst + 1 + i)) = vm.reg((u16)(valSrc + i));
            }
            // Retain embedded Obj* fields: walk the active case via
            // inlineWalkPointers, which inspects the discriminant we just
            // wrote at dst[0].
            inlineWalkPointers(&vm.reg(dst), en, /*release_=*/false);
        }
    }
    DISPATCH(3);
}

// MAKE_INLINE_ENUM_NODATA Rd, caseIdx (3 words: op, regs, EnumType*)
void op_make_inline_enum_nodata(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], caseIdx = pc[1].regs[1];
    auto* en = static_cast<EnumType*>(pc[2].p);
    vm.reg(dst).i = caseIdx;
    for (u8 i = 1; i < en->sizeWords_; ++i) vm.reg((u16)(dst + i)).i = 0;
    DISPATCH(3);
}

// BOX_ENUM Rd, Ra (3 words: op, regs, EnumType*) - inline -> heap Enum*
void op_box_enum(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* en = static_cast<EnumType*>(pc[2].p);
    vm.reg(dst).o = boxInlineDeep(vm, en, src);
    DISPATCH(3);
}

// UNBOX_ENUM Rd, Ra (3 words: op, regs, EnumType*) - heap Enum* -> inline
void op_unbox_enum(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* en = static_cast<EnumType*>(pc[2].p);
    unboxInlineDeep(vm, en, vm.reg(src).o, dst);
    DISPATCH(3);
}

// --- Dynamic Array Operations (for auto-mapping) ---

// ARRAY_ALLOC Rd, Rn (3 words: op, regs{dst, len_reg}, ArrayType*)
// Allocate an array of runtime-determined length, initialized to zero
void op_array_alloc(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], lenReg = pc[1].regs[1];
    auto* arrayType = static_cast<ArrayType*>(pc[2].p);
    Type* elemType = arrayType->elemType_;
    i64 len = vm.reg(lenReg).i;

    switch (arrayBackendFor(elemType)) {
        case ArrayBackend::Complex: {
            auto* arr = new PodArray<x64>(arrayType);
            arr->v.resize(len);  // value-initialised: 0+0i
            vm.reg(dst).o = arr;
            break;
        }
        case ArrayBackend::Fraction: {
            auto* arr = new PodArray<r64>(arrayType);
            arr->v.resize(len);  // value-initialised: 0/1
            vm.reg(dst).o = arr;
            break;
        }
        case ArrayBackend::Float: {
            auto* arr = new PodArray<f64>(arrayType);
            arr->v.resize(len);
            vm.reg(dst).o = arr;
            break;
        }
        case ArrayBackend::Int: {
            auto* arr = new PodArray<i64>(arrayType);
            arr->v.resize(len);
            vm.reg(dst).o = arr;
            break;
        }
        case ArrayBackend::Inline: {
            auto* arr = new InlineArray(arrayType);
            arr->resize(len);
            vm.reg(dst).o = arr;
            break;
        }
        case ArrayBackend::Obj: {
            auto* arr = new ObjArray(arrayType);
            arr->resize(len);
            vm.reg(dst).o = arr;
            break;
        }
    }
    DISPATCH(3);
}

// ARRAY_SET Ra, Rb_idx, Rc_val (3 words: op, regs{arr, idx_reg, val_reg}, ArrayType*)
//
// Phase 4g.28: one specialization per array backend. Codegen emits the
// variant matching the array's element type so per-element dispatch is a
// direct call -- no runtime switch on arrayBackendFor. The InlineArray
// variant still reads stride at runtime from the InlineArray itself.
void op_array_set_int(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<i64>*>(vm.reg(arrReg).o);
    arr->v[cyclicIndex(idx, arr->v.size())] = vm.reg(valReg).i;
    DISPATCH(3);
}
void op_array_set_float(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<f64>*>(vm.reg(arrReg).o);
    arr->v[cyclicIndex(idx, arr->v.size())] = vm.reg(valReg).f;
    DISPATCH(3);
}
void op_array_set_complex(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<x64>*>(vm.reg(arrReg).o);
    f64 re = vm.reg(valReg).f;
    f64 im = vm.reg((u16)(valReg + 1)).f;
    arr->v[cyclicIndex(idx, arr->v.size())] = x64(re, im);
    DISPATCH(3);
}
void op_array_set_fraction(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<r64>*>(vm.reg(arrReg).o);
    i64 n = vm.reg(valReg).i;
    i64 d = vm.reg((u16)(valReg + 1)).i;
    arr->v[cyclicIndex(idx, arr->v.size())] = r64(n, d, true);
    DISPATCH(3);
}
void op_array_set_inline(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<InlineArray*>(vm.reg(arrReg).o);
    arr->setSlot(cyclicIndex(idx, arr->size()), &vm.reg(valReg));
    DISPATCH(3);
}
void op_array_set_obj(VM& vm, Code* pc) {
    u16 arrReg = pc[1].regs[0], idxReg = pc[1].regs[1], valReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<ObjArray*>(vm.reg(arrReg).o);
    arr->set(cyclicIndex(idx, arr->size()), vm.reg(valReg).o);
    DISPATCH(3);
}

// ARRAY_GET_DYN Rd, Ra, Rb_idx (3 words: op, regs{dst, arr, idx_reg}, ArrayType*)
//
// Phase 4g.28: backend-specialized; one variant per array backend. The
// ArrayType* operand is still emitted so the disassembler can label the
// element type and so InlineArray reads the right stride from the array
// header, but the dispatch case selection is at codegen time.
void op_array_get_dyn_int(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<i64>*>(vm.reg(arrReg).o);
    vm.reg(dst).i = arr->v[cyclicIndex(idx, arr->v.size())];
    DISPATCH(3);
}
void op_array_get_dyn_float(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<f64>*>(vm.reg(arrReg).o);
    vm.reg(dst).f = arr->v[cyclicIndex(idx, arr->v.size())];
    DISPATCH(3);
}
void op_array_get_dyn_complex(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<x64>*>(vm.reg(arrReg).o);
    x64 const& v = arr->v[cyclicIndex(idx, arr->v.size())];
    vm.reg(dst).f = v.real();
    vm.reg((u16)(dst + 1)).f = v.imag();
    DISPATCH(3);
}
void op_array_get_dyn_fraction(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<PodArray<r64>*>(vm.reg(arrReg).o);
    r64 const& v = arr->v[cyclicIndex(idx, arr->v.size())];
    vm.reg(dst).i = v.numer();
    vm.reg((u16)(dst + 1)).i = v.denom();
    DISPATCH(3);
}
void op_array_get_dyn_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<InlineArray*>(vm.reg(arrReg).o);
    arr->getSlot(cyclicIndex(idx, arr->size()), &vm.reg(dst));
    DISPATCH(3);
}
void op_array_get_dyn_obj(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], arrReg = pc[1].regs[1], idxReg = pc[1].regs[2];
    i64 idx = vm.reg(idxReg).i;
    auto* arr = static_cast<ObjArray*>(vm.reg(arrReg).o);
    vm.reg(dst).o = arr->get(cyclicIndex(idx, arr->size()));
    DISPATCH(3);
}

// --- List ---

// CONS Rd, Rhead, Rtail (3 words: op, regs{dst, head, tail}, ListType*)
//
// Phase 4g.9: for Inline composite element types, head spans stride_
// consecutive words at vm.reg(head)..vm.reg(head+stride-1). Copy them into
// the node's flex-array head storage and retain embedded Obj* fields via
// inlineWalkPointers. Other element types use the legacy single-Word path.
void op_cons(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], head = pc[1].regs[1], tail = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);
    Type* et = listType->elemType_;

    auto* node = ListNode::create(listType);
    node->tail_ = static_cast<ListNode*>(vm.reg(tail).o);
    if (node->payloadWords_ > 1) {
        Word* dstHead = node->headData();
        for (u32 i = 0; i < node->payloadWords_; ++i) dstHead[i] = vm.reg((u16)(head + i));
        inlineWalkPointers(dstHead, et, /*release_=*/false);
    } else {
        node->head_ = vm.reg(head);
        if (storesObjPtr(et) && node->head_.o) node->head_.o->retain();
    }
    if (node->tail_) node->tail_->retain();
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// MAKE_LIST Rd, firstSrc, count (3 words: op, regs{dst, firstSrc, count}, ListType*)
// Builds list from N consecutive elements (cons from right to left).
// Phase 4g.9: per-element stride may exceed 1 for Inline composites.
void op_make_list(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], count = pc[1].regs[2];
    auto* listType = static_cast<ListType*>(pc[2].p);
    Type* et = listType->elemType_;
    // Phase 4g.20: all Inline composites (Complex/Fraction included) read
    // their sizeWords_ words natively from consecutive source slots.
    bool elemInline = et && et->repr_ == Type::Repr::Inline;
    u32 stride = elemInline ? et->sizeWords_ : 1;
    bool elemIsObj = storesObjPtr(et);

    ListNode* result = nullptr;
    for (int i = (int)count - 1; i >= 0; --i) {
        auto* node = ListNode::create(listType);
        u16 src = (u16)(firstSrc + (u16)i * stride);
        if (stride > 1) {
            Word* dstHead = node->headData();
            for (u32 k = 0; k < stride; ++k) dstHead[k] = vm.reg((u16)(src + k));
            inlineWalkPointers(dstHead, et, /*release_=*/false);
        } else {
            node->head_ = vm.reg(src);
            if (elemIsObj && node->head_.o) node->head_.o->retain();
        }
        node->tail_ = result;
        if (node->tail_) node->tail_->retain();
        result = node;
    }
    vm.reg(dst).o = result;
    DISPATCH(3);
}

// LIST_HEAD Rd, Ra (2 words). Phase 4g.9: copies stride_ words for Inline
// composite element types; legacy single-Word read for the rest.
void op_list_head(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], src = pc[1].regs[1];
    auto* node = static_cast<ListNode*>(vm.reg(src).o);
    node->force(vm);
    if (node->payloadWords_ > 1) {
        Word const* h = node->headData();
        for (u32 i = 0; i < node->payloadWords_; ++i) vm.reg((u16)(dst + i)) = h[i];
        // Retain embedded Obj* fields so the caller owns the copy.
        auto* lt = static_cast<ListType*>(node->type_);
        inlineWalkPointers(&vm.reg(dst), lt->elemType_, /*release_=*/false);
    } else {
        vm.reg(dst) = node->head_;
    }
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

    // Phase 4g.2: total slot words may exceed argc when params are multi-
    // word inline composites; read funcType to compute the true word span.
    u16 wordCount = argc;
    if (callee->funcType) {
        auto* ft = static_cast<FunctionType*>(callee->funcType);
        u16 sum = 0;
        for (size_t i = 0; i < ft->argTypes_.size() && i < argc; ++i) {
            Type* t = ft->argTypes_[i];
            sum += (t && t->sizeWords_ > 0) ? t->sizeWords_ : 1;
        }
        if (sum > argc) wordCount = sum;
    }
    // Copy args to r0..r(wordCount-1) — forward copy is safe since argBase >= wordCount
    for (u16 i = 0; i < wordCount; i++) {
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
// flags.i: bit 0 = isInfinite
//
// Phase 4g.14: start/end/step are stored natively per the element type's
// footprint (1 word for Int, 2 for Fraction). The caller places each
// endpoint contiguously at its natural sizeWords.
void op_make_range(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], startReg = pc[1].regs[1];
    u16 endReg = pc[1].regs[2], stepReg = pc[1].regs[3];
    auto* rangeType = static_cast<RangeType*>(pc[2].p);
    i64 flags = pc[3].i;
    bool isInfinite = (flags & 1) != 0;

    auto* range = RangeObj::create(rangeType, isInfinite);
    Type* et = rangeType->elemType_;
    u8 sw = range->elemSizeWords_;
    for (u8 i = 0; i < sw; ++i) range->startData()[i] = vm.reg((u16)(startReg + i));
    for (u8 i = 0; i < sw; ++i) range->stepData()[i]  = vm.reg((u16)(stepReg + i));
    if (!isInfinite) {
        for (u8 i = 0; i < sw; ++i) range->endData()[i] = vm.reg((u16)(endReg + i));
    }
    payloadRetain(range->startData(), et);
    payloadRetain(range->stepData(),  et);
    if (!isInfinite) payloadRetain(range->endData(), et);
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

    auto* node = ListNode::create(info->resultListType);
    auto* gen  = new AutoMapListGen(info->resultListType);
    gen->source_       = srcList;
    gen->info_         = info;
    gen->numBroadcast_ = numBroadcast;
    for (u16 i = 0; i < numBroadcast && i < AutoMapListGen::kMaxBroadcast; ++i) {
        gen->broadcastVals_[i] = vm.reg(broadcastBase + i);
    }
    node->installGenerator(gen);
    // Retain Obj* fields stored in generator
    if (srcList) srcList->retain();
    gen->info_->retain();
    for (u16 i = 0; i < numBroadcast && i < AutoMapListGen::kMaxBroadcast; ++i) {
        if (i < info->broadcastArgs.size() && info->broadcastArgs[i].isObj && gen->broadcastVals_[i].o)
            gen->broadcastVals_[i].o->retain();
    }
    vm.reg(dst).o = node;
    DISPATCH(3);
}

// --- Map ---

// MAKE_MAP Rd, firstKeyReg, numPairs (3 words: op, regs{dst, firstKeyReg, numPairs}, MapType*)
// Phase 4g.11: each pair occupies (keyStride + valueStride) consecutive
// registers; inline composite keys/values are stored natively without
// boxing.
void op_make_map(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstKV = pc[1].regs[1], numPairs = pc[1].regs[2];
    auto* mapType = static_cast<MapType*>(pc[2].p);

    auto* map = new MapObj(mapType);
    u32 kS = map->keyStride_;
    u32 vS = map->valueStride_;
    u32 pairStride = kS + vS;
    for (u16 i = 0; i < numPairs; ++i) {
        Word const* kSrc = &vm.reg((u16)(firstKV + i * pairStride));
        Word const* vSrc = kSrc + kS;
        // Retain Obj* children before handing payload to the map. insertOrUpdate
        // releases the redundant retain on duplicate key.
        payloadRetain(kSrc, mapType->keyType_);
        payloadRetain(vSrc, mapType->valueType_);
        map->insertOrUpdate(kSrc, vSrc);
    }
    vm.reg(dst).o = map;
    DISPATCH(3);
}

// MAP_GET Rd, Ra(map), Rb(key) (3 words: op, regs{dst, map, key}, MapType*)
// Phase 4g.11: dst spans valueStride words; key spans keyStride words.
void op_map_get(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], mapReg = pc[1].regs[1], keyReg = pc[1].regs[2];
    auto* map = static_cast<MapObj*>(vm.reg(mapReg).o);

    Word const* keyPtr = &vm.reg(keyReg);
    u32 slot = map->findSlot(keyPtr);
    if (slot != map->capacity()) {
        Word const* v = map->slotVal(slot);
        for (u32 i = 0; i < map->valueStride_; ++i) vm.reg((u16)(dst + i)) = v[i];
        payloadRetain(&vm.reg(dst), map->valueType());
    } else {
        for (u32 i = 0; i < map->valueStride_; ++i) vm.reg((u16)(dst + i)).i = 0;
    }
    DISPATCH(3);
}

// MAP_GET_OPTION Rd, Ra(map), Rb(key) (3 words: op, regs{dst, map, key}, EnumType*)
// Phase 4g.11: key spans keyStride registers starting at Rb.
//
// Phase 4g.24: write the Option result in its destination representation
// directly:
//   * Inline Option -- dst spans sizeWords_ registers; word 0 is the
//     discriminant, words 1.. are the V payload (copied natively from the
//     map slot).
//   * NullablePtrEnum -- dst holds a nullable Obj*.
//   * heap Enum -- dst holds Enum*; multi-word V payloads land natively in
//     the heap Enum's flex array (since Phase 4g.15).
// This drops the box/unbox round-trip on the Inline path that the four
// codegen sites used to emit explicitly with emitUnboxIfInline.
void op_map_get_option(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], mapReg = pc[1].regs[1], keyReg = pc[1].regs[2];
    auto* map = static_cast<MapObj*>(vm.reg(mapReg).o);
    auto* optType = static_cast<EnumType*>(pc[2].p);
    auto* mt = static_cast<MapType*>(map->type_);
    Type* vt = mt->valueType_;

    Word const* keyPtr = &vm.reg(keyReg);
    u32 slot = map->findSlot(keyPtr);
    bool found = (slot != map->capacity());

    // Phase 3: NullablePtrEnum -- store as nullable Obj* directly.
    if (optType->repr_ == Type::Repr::NullablePtrEnum) {
        if (found) {
            Obj* o = map->slotVal(slot)[0].o;
            if (o) o->retain();
            vm.reg(dst).o = o;
        } else {
            vm.reg(dst).o = nullptr;
        }
        DISPATCH(3);
    }

    // Phase 4g.24: Inline Option -- write discriminant + native payload
    // straight into the dst register window. No heap Enum allocation.
    if (optType->repr_ == Type::Repr::Inline) {
        u32 vs = map->valueStride_;
        if (found) {
            vm.reg(dst).i = 0;  // which_ = some
            Word const* src = map->slotVal(slot);
            for (u32 i = 0; i < vs; ++i) vm.reg((u16)(dst + 1 + i)) = src[i];
            payloadRetain(&vm.reg((u16)(dst + 1)), vt);
        } else {
            vm.reg(dst).i = 1;  // which_ = none
            for (u32 i = 0; i < vs; ++i) vm.reg((u16)(dst + 1 + i)).i = 0;
        }
        DISPATCH(3);
    }

    // Phase 4g.15: heap Enum stores payload natively in v[]. Copy the map
    // slot's value words directly into the Enum's payload.
    auto* e = Enum::create(optType, found ? 0 : 1);
    if (found) {
        Word const* src = map->slotVal(slot);
        u32 sw = (vt && vt->sizeWords_ > 0) ? vt->sizeWords_ : 1;
        for (u32 i = 0; i < sw; ++i) e->v[i] = src[i];
        payloadRetain(&e->v[0], vt);
    }
    vm.reg(dst).o = e;
    DISPATCH(3);
}

// --- Set ---

// MAKE_SET Rd, firstSrc, numElems (3 words: op, regs{dst, firstSrc, numElems}, SetType*)
// Phase 4g.11: each element occupies elemStride consecutive registers.
void op_make_set(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], firstSrc = pc[1].regs[1], numElems = pc[1].regs[2];
    auto* setType = static_cast<SetType*>(pc[2].p);

    auto* set = new SetObj(setType);
    u32 eS = set->elemStride_;
    for (u16 i = 0; i < numElems; ++i) {
        Word const* src = &vm.reg((u16)(firstSrc + i * eS));
        payloadRetain(src, setType->elemType_);
        // insertElem releases the retain if the element is already present.
        set->insertElem(src);
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

// --- Inline-composite Ref ops (Phase 4g.5) ---
// Allocate an InlineRef whose flex array holds elemType_->sizeWords_ Words,
// then copy the inline payload from valReg in. ARC: walk the layout to
// retain embedded Obj* fields.
void op_make_ref_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valReg = pc[1].regs[1];
    auto* refType = static_cast<RefType*>(pc[2].p);
    Type* et = refType->elemType_;
    auto* ref = InlineRef::create(refType);
    u32 n = ref->sizeWords_;
    for (u32 i = 0; i < n; ++i) ref->v[i] = vm.reg((u16)(valReg + i));
    inlineWalkPointers(&ref->v[0], et, /*release_=*/false);
    vm.reg(dst).o = ref;
    DISPATCH(3);
}

// REF_GET_INLINE Rd, Ra (3 words: op, regs{dst, ref}, RefType*)
// Copy the inline payload out into the dst slot.
void op_ref_get_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], refReg = pc[1].regs[1];
    auto* ref = static_cast<InlineRef*>(vm.reg(refReg).o);
    u32 n = ref->sizeWords_;
    for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = ref->v[i];
    DISPATCH(3);
}

// REF_SET_INLINE Rd, Ra, Rb (3 words: op, regs{dst, ref, val}, RefType*)
// Mutate the InlineRef's payload in place; also copy the new value into dst
// (so `r <- v` evaluates to v).
void op_ref_set_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], refReg = pc[1].regs[1], valReg = pc[1].regs[2];
    auto* ref = static_cast<InlineRef*>(vm.reg(refReg).o);
    auto* refType = static_cast<RefType*>(ref->type_);
    Type* et = refType->elemType_;
    u32 n = ref->sizeWords_;
    // Release embedded Obj* in the old payload.
    inlineWalkPointers(&ref->v[0], et, /*release_=*/true);
    // Copy in the new payload.
    for (u32 i = 0; i < n; ++i) ref->v[i] = vm.reg((u16)(valReg + i));
    // Retain embedded Obj* in the new payload.
    inlineWalkPointers(&ref->v[0], et, /*release_=*/false);
    // Result of the assignment expression is the assigned value.
    if (dst != valReg) {
        for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.reg((u16)(valReg + i));
    }
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
//
// Phase 4g.12: the yielded value may span sizeWords_ words for an Inline
// composite yield type. Snapshot all of them out of the source slot before
// switching contexts (the caller's register file replaces this one).
void op_yield(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0], gcMapIdx = pc[1].regs[1];

    auto* coro = vm.currentCoroutine();
    auto* frame = vm.currentCoroFrame();
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* yieldType = coroType->yieldType_;
    u16 yieldStride = (u16)((yieldType && yieldType->sizeWords_ > 0)
                            ? yieldType->sizeWords_ : 1);

    // Snapshot the yielded value before switching register files.
    Word yieldedValue[8];
    for (u16 i = 0; i < yieldStride; ++i) yieldedValue[i] = vm.reg((u16)(src + i));

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

    // Write yielded value(s) directly to caller's destination register slot.
    for (u16 i = 0; i < yieldStride; ++i) {
        vm.reg((u16)(coro->callerResultReg_ + i)) = yieldedValue[i];
    }

    // Clear stale caller refs
    coro->callerCoroFrame_ = nullptr;
    coro->callerCoroutine_ = nullptr;

    Code* returnPC = coro->callerReturnPC_;
    [[clang::musttail]] return returnPC->op(vm, returnPC);
}

// CORO_DONE (1 word: op)
void op_coro_done(VM& vm, Code* pc) {
    auto* coro = vm.currentCoroutine();
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* yieldType = coroType->yieldType_;
    u16 yieldStride = (u16)((yieldType && yieldType->sizeWords_ > 0)
                            ? yieldType->sizeWords_ : 1);

    // Mark as done
    coro->topFrame_ = nullptr;
    coro->state_ = CoroutineObj::Done;

    // Restore caller context
    vm.setBaseReg(coro->callerBaseReg_);
    vm.setFrameCount(coro->callerFrameCount_);
    vm.setCurrentCoroFrame(coro->callerCoroFrame_);
    vm.setCurrentCoroutine(coro->callerCoroutine_);
    vm.setCurrentRegs(vm.regsBase() + vm.baseReg());

    // Zero the caller's destination slot (caller checks state, not the bytes).
    for (u16 i = 0; i < yieldStride; ++i) {
        vm.reg((u16)(coro->callerResultReg_ + i)).i = 0;
    }

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
//
// Phase 4g.12: when Option<T> is Inline (the common case for primitive and
// inline-composite yield types), write the discriminant + payload directly
// into the dst slot's sizeWords_ consecutive registers -- no intermediate
// heap Enum*. NullablePtrEnum (Option<Obj*Type>) stays a single nullable
// pointer. The legacy heap-Enum* path remains for any odd repr.
void op_coro_wrap_option(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0], valSrc = pc[1].regs[1], coroReg = pc[1].regs[2];
    auto* optType = static_cast<EnumType*>(pc[2].p);
    auto* coro = static_cast<CoroutineObj*>(vm.reg(coroReg).o);
    auto* coroType = static_cast<CoroutineType*>(coro->type_);
    Type* yieldType = coroType->yieldType_;
    u16 yieldStride = (u16)((yieldType && yieldType->sizeWords_ > 0)
                            ? yieldType->sizeWords_ : 1);
    bool done = (coro->state_ == CoroutineObj::Done);

    // Phase 3: NullablePtrEnum -- store as nullable Obj* directly.
    if (optType->repr_ == Type::Repr::NullablePtrEnum) {
        if (done) {
            vm.reg(dst).o = nullptr;
        } else {
            Word v = vm.reg(valSrc);
            if (v.o) v.o->retain();
            vm.reg(dst).o = v.o;
        }
        DISPATCH(3);
    }

    // Inline Option: write [discriminant, payload...] directly into dst slot.
    if (optType->repr_ == Type::Repr::Inline) {
        if (done) {
            vm.reg(dst).i = 1;  // none
            for (u16 i = 0; i < yieldStride; ++i) {
                vm.reg((u16)(dst + 1 + i)).i = 0;
            }
        } else {
            vm.reg(dst).i = 0;  // some
            for (u16 i = 0; i < yieldStride; ++i) {
                vm.reg((u16)(dst + 1 + i)) = vm.reg((u16)(valSrc + i));
            }
            payloadRetain(&vm.reg((u16)(dst + 1)), yieldType);
        }
        DISPATCH(3);
    }

    // Fallback: heap Enum* with native multi-word payload.
    auto* e = Enum::create(optType, done ? 1 : 0);
    if (!done) {
        u32 sw = (yieldType && yieldType->sizeWords_ > 0) ? yieldType->sizeWords_ : 1;
        for (u32 i = 0; i < sw; ++i) e->v[i] = vm.reg((u16)(valSrc + i));
        payloadRetain(&e->v[0], yieldType);
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

// --- Inline-composite dynvar ops (Phase 4g.5) ---
// Each occupies sizeWords_ consecutive dynVars_ slots. ARC: writes walk the
// type layout via inlineWalkPointers to release old Obj* fields and retain
// new ones. DYNSCOPE_PUSH_I additionally saves the old payload onto the
// side payload buffer for restoration on function return.

// LOAD_DYNAMIC_I Rd, K (4 words: op, regs{dst}, dynvar_index, Type*)
void op_load_dynamic_inline(VM& vm, Code* pc) {
    u16 dst = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    for (u32 i = 0; i < n; ++i) vm.reg((u16)(dst + i)) = vm.dynVar(idx + i);
    DISPATCH(4);
}

// STORE_DYNAMIC_I Ra, K (4 words: op, regs{src}, dynvar_index, Type*)
void op_store_dynamic_inline(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    inlineWalkPointers(&vm.dynVar(idx), type, /*release_=*/true);
    for (u32 i = 0; i < n; ++i) vm.dynVar(idx + i) = vm.reg((u16)(src + i));
    inlineWalkPointers(&vm.dynVar(idx), type, /*release_=*/false);
    DISPATCH(4);
}

// INIT_DYNAMIC_I Ra, K (4 words: op, regs{src}, dynvar_index, Type*)
void op_init_dynamic_inline(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    u32 n = type ? (u32)type->sizeWords_ : 1u;
    if (n == 0) n = 1;
    for (u32 i = 0; i < n; ++i) vm.dynVar(idx + i) = vm.reg((u16)(src + i));
    inlineWalkPointers(&vm.dynVar(idx), type, /*release_=*/false);
    DISPATCH(4);
}

// DYNSCOPE_PUSH_I Ra, K (4 words: op, regs{src}, dynvar_index, Type*)
void op_dynscope_push_inline(VM& vm, Code* pc) {
    u16 src = pc[1].regs[0];
    u32 idx = (u32)pc[2].i;
    auto* type = static_cast<Type*>(pc[3].p);
    vm.dynScopePushInline(idx, &vm.reg(src), type);
    DISPATCH(4);
}

} // namespace ts
