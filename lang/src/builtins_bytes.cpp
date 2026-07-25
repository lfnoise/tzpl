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
//  builtins_bytes.cpp
//  lang
//
//  Low-level primitives over the `Bytes` type: a growable little-endian byte
//  buffer plus bounds-checked readers. These are the substrate the Msg binary
//  message format is built from (see lang/docs/FFI_Guide.html section 15 for the
//  format, lang/modules/std/messageEncoding.x and shared/tzpl_sexpr_bin.hpp for
//  the two implementations of the wire layout). They never reference Msg;
//  encode/decode/Reader live in Tzopilotl on top of these.
//
//  Also registers `toSymbol(String) Symbol` (interning), the inverse of the
//  existing `Symbol.toString`, used by decode to rebuild interned symbols.
//

#include "builtins_internal.hpp"
#include "symbol.hpp"

#include <bit>
#include <string_view>

namespace ts {

static inline BytesObj* asBytes(VM& vm, u16 r) {
    return static_cast<BytesObj*>(vm.reg(r).o);
}

static inline void pushLE(BytesObj* b, u64 v, int nbytes) {
    for (int k = 0; k < nbytes; ++k) b->data.push_back((u8)((v >> (8 * k)) & 0xFF));
}

static inline u64 readLE(BytesObj const* b, i64 off, int nbytes) {
    u64 r = 0;
    if (off >= 0 && (u64)off + (u64)nbytes <= b->data.size()) {
        for (int k = 0; k < nbytes; ++k) r |= (u64)b->data[(usize)off + k] << (8 * k);
    }
    return r;
}

// ---------------------------------------------------------------------------
// Builders (mutating; little-endian)
// ---------------------------------------------------------------------------

// bytes() Bytes -- a new empty buffer.
static void builtin_bytes_new(VM& vm, u16 dst, u16, u16) {
    vm.reg(dst).o = new BytesObj();   // self-registers in its constructor
}

// putU8!(b, v) Void -- append the low byte of v.
static void builtin_put_u8(VM& vm, u16 dst, u16, u16 ab) {
    asBytes(vm, ab)->data.push_back((u8)(vm.reg(ab + 1).i & 0xFF));
    vm.reg(dst).i = 0;
}

// putU32!(b, v) Void -- append 4 bytes LE.
static void builtin_put_u32(VM& vm, u16 dst, u16, u16 ab) {
    pushLE(asBytes(vm, ab), (u64)vm.reg(ab + 1).i, 4);
    vm.reg(dst).i = 0;
}

// putU64!(b, v) Void -- append 8 bytes LE (an i64 payload or an offset).
static void builtin_put_u64(VM& vm, u16 dst, u16, u16 ab) {
    pushLE(asBytes(vm, ab), (u64)vm.reg(ab + 1).i, 8);
    vm.reg(dst).i = 0;
}

// putF64!(b, v) Void -- append an IEEE-754 double, 8 bytes LE.
static void builtin_put_f64(VM& vm, u16 dst, u16, u16 ab) {
    pushLE(asBytes(vm, ab), std::bit_cast<u64>(vm.reg(ab + 1).f), 8);
    vm.reg(dst).i = 0;
}

// putUtf8!(b, s) Void -- append the raw bytes of String s (no length prefix).
static void builtin_put_utf8(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = asBytes(vm, ab);
    auto* s = static_cast<StringObj*>(vm.reg(ab + 1).o);
    const u8* p = reinterpret_cast<const u8*>(s->s.data());
    b->data.insert(b->data.end(), p, p + s->s.size());
    vm.reg(dst).i = 0;
}

// setU32At!(b, off, v) Void -- overwrite 4 bytes at off (back-patch). No-op if
// the slot is out of bounds.
static void builtin_set_u32_at(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = asBytes(vm, ab);
    i64 off = vm.reg(ab + 1).i;
    u32 v = (u32)(u64)vm.reg(ab + 2).i;
    if (off >= 0 && (u64)off + 4 <= b->data.size()) {
        for (int k = 0; k < 4; ++k) b->data[(usize)off + k] = (u8)((v >> (8 * k)) & 0xFF);
    }
    vm.reg(dst).i = 0;
}

// byteLength(b) Int -- current length in bytes.
static void builtin_byte_length(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (i64)asBytes(vm, ab)->data.size();
}

// ---------------------------------------------------------------------------
// Readers (bounds-checked; an out-of-range read yields 0 / "" rather than
// reading out of bounds -- NATS payloads are untrusted input)
// ---------------------------------------------------------------------------

// u8At(b, off) Int
static void builtin_u8_at(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (i64)readLE(asBytes(vm, ab), vm.reg(ab + 1).i, 1);
}

// u32At(b, off) Int
static void builtin_u32_at(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (i64)readLE(asBytes(vm, ab), vm.reg(ab + 1).i, 4);
}

// i64At(b, off) Int -- 8 bytes LE (an i64 payload or an offset).
static void builtin_i64_at(VM& vm, u16 dst, u16, u16 ab) {
    vm.reg(dst).i = (i64)readLE(asBytes(vm, ab), vm.reg(ab + 1).i, 8);
}

// f64At(b, off) Float
static void builtin_f64_at(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = asBytes(vm, ab);
    i64 off = vm.reg(ab + 1).i;
    bool ok = off >= 0 && (u64)off + 8 <= b->data.size();
    vm.reg(dst).f = ok ? std::bit_cast<f64>(readLE(b, off, 8)) : 0.0;
}

// utf8At(b, off, len) String -- copy len bytes as a String, clamped to the
// buffer end.
static void builtin_utf8_at(VM& vm, u16 dst, u16, u16 ab) {
    auto* b = asBytes(vm, ab);
    i64 off = vm.reg(ab + 1).i;
    i64 len = vm.reg(ab + 2).i;
    auto* result = new StringObj();
    if (off >= 0 && len > 0 && (u64)off < b->data.size()) {
        u64 avail = b->data.size() - (u64)off;
        u64 n = (u64)len <= avail ? (u64)len : avail;
        result->s.assign(reinterpret_cast<const char*>(b->data.data()) + off, n);
    }
    registerNewObj(result);
    vm.reg(dst).o = result;
}

// ---------------------------------------------------------------------------
// toSymbol(s String) Symbol -- intern a String into a Symbol (inverse of
// Symbol.toString).
// ---------------------------------------------------------------------------
static void builtin_to_symbol(VM& vm, u16 dst, u16, u16 ab) {
    auto* s = static_cast<StringObj*>(vm.reg(ab).o);
    vm.reg(dst).s = intern(std::string_view(s->s.data(), s->s.size()));
}

void registerBytesBuiltins(Compiler& compiler, FuncMap& functions) {
    Type* Bytes  = compiler.bytesType();
    Type* Int    = compiler.intType();
    Type* Float  = compiler.floatType();
    Type* String = compiler.stringType();
    Type* Symbol = compiler.symbolType();
    Type* Void   = compiler.voidType();

    // Builders. Not pure (they mutate / allocate), rtSafe (TLSF only).
    registerOne(compiler, functions, "bytes",     Bytes, {},                builtin_bytes_new,  /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "putU8!",    Void,  {Bytes, Int},      builtin_put_u8,     /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "putU32!",   Void,  {Bytes, Int},      builtin_put_u32,    /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "putU64!",   Void,  {Bytes, Int},      builtin_put_u64,    /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "putF64!",   Void,  {Bytes, Float},    builtin_put_f64,    /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "putUtf8!",  Void,  {Bytes, String},   builtin_put_utf8,   /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "setU32At!", Void,  {Bytes, Int, Int}, builtin_set_u32_at, /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "byteLength",Int,   {Bytes},           builtin_byte_length,/*pure=*/false, /*rtSafe=*/true);

    // Readers. Not pure (the buffer is mutable, so no CSE across mutations).
    registerOne(compiler, functions, "u8At",   Int,    {Bytes, Int},      builtin_u8_at,   /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "u32At",  Int,    {Bytes, Int},      builtin_u32_at,  /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "i64At",  Int,    {Bytes, Int},      builtin_i64_at,  /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "f64At",  Float,  {Bytes, Int},      builtin_f64_at,  /*pure=*/false, /*rtSafe=*/true);
    registerOne(compiler, functions, "utf8At", String, {Bytes, Int, Int}, builtin_utf8_at, /*pure=*/false, /*rtSafe=*/true);

    // String -> interned Symbol (immutable input, so safely pure).
    registerOne(compiler, functions, "toSymbol", Symbol, {String}, builtin_to_symbol, /*pure=*/true, /*rtSafe=*/true);
}

} // namespace ts
