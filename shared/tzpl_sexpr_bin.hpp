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
//  tzpl_sexpr_bin.hpp
//  shared
//
//  The "TZB" binary message format -- canonical wire layout for serializing an
//  Msg value (lang/modules/std/message.x) to a flat byte buffer. This C++ header
//  is the single source of truth for the layout and is mirrored, byte for byte,
//  by the Tzopilotl implementation in lang/modules/std/messageEncoding.x. Keep
//  the two in sync.
//
//  Prose documentation: lang/docs/FFI_Guide.html section 15 (Binary Messages);
//  the wire format is section 15.5, the C++ API section 15.7.
//
//  It provides a zero-copy `Reader` (random access into a received buffer with
//  no allocation -- intended for the silo / cross-language consumer side) and a
//  `Writer`/`encode` for producing buffers from a small `Value` variant.
//
//  Wire layout (little-endian):
//
//    header:  'T' 'Z' 'B' version(=2)     (4 bytes)
//             u32 rootOffset               (offset of the root value slot)
//
//    value slot (9 bytes): tag(u8) + payload(8 bytes), by tag:
//             0 bool   -> 0/1
//             1 int    -> i64
//             2 float  -> f64
//             3 symbol -> u64 offset to a string blob
//             4 string -> u64 offset to a string blob
//             5 vec    -> u64 offset to a list node
//
//    string blob:  u32 byteLen + raw UTF-8 bytes
//
//    list node:    u32 count + u32 tagsOffset + u32 payloadsOffset
//                  tagsOffset     -> count tag bytes (one per child)
//                  payloadsOffset -> count * 8 payload bytes (one per child)
//
//  Children live in parallel tag/payload arrays, so child i is reached in O(1):
//  tag at tagsOffset+i, payload at payloadsOffset+i*8.
//

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace tzpl::sbin {

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

inline constexpr std::uint8_t kMagic0 = 'T';
inline constexpr std::uint8_t kMagic1 = 'Z';
inline constexpr std::uint8_t kMagic2 = 'B';
inline constexpr std::uint8_t kVersion = 2;
inline constexpr std::size_t  kHeaderSize = 8;   // magic(4) + u32 rootOffset
inline constexpr std::size_t  kRootOffsetPos = 4;

enum class Tag : std::uint8_t {
    Bool = 0, Int = 1, Float = 2, Symbol = 3, String = 4, Vec = 5,
};

// ---------------------------------------------------------------------------
// Zero-copy reader -- a cursor over one value slot (a tag byte at tagOff and an
// 8-byte payload at valOff). Bounds-checked: an out-of-range read yields 0/"".
// ---------------------------------------------------------------------------

class Reader {
public:
    Reader() = default;
    Reader(const std::uint8_t* buf, std::size_t len,
           std::uint32_t tagOff, std::uint32_t valOff)
        : buf_(buf), len_(len), tagOff_(tagOff), valOff_(valOff) {}

    // True if `buf` carries a valid TZB v2 header.
    static bool valid(const std::uint8_t* buf, std::size_t len) {
        return buf && len >= 9 &&
               buf[0] == kMagic0 && buf[1] == kMagic1 &&
               buf[2] == kMagic2 && buf[3] == kVersion;
    }

    // A cursor at the root value slot (call only when valid()).
    static Reader root(const std::uint8_t* buf, std::size_t len) {
        std::uint32_t rootOff = readU32(buf, len, kRootOffsetPos);
        return Reader(buf, len, rootOff, rootOff + 1);
    }

    Tag tag() const {
        std::uint8_t t = readU8(buf_, len_, tagOff_);
        return t <= 5 ? static_cast<Tag>(t) : Tag::Vec;
    }

    bool   asBool()  const { return readI64(buf_, len_, valOff_) != 0; }
    int64_t asInt()  const { return readI64(buf_, len_, valOff_); }
    double asFloat() const { return readF64(buf_, len_, valOff_); }

    // The string blob this slot points at, as a view into the buffer (no copy).
    std::string_view asStr() const {
        std::uint32_t off = static_cast<std::uint32_t>(readI64(buf_, len_, valOff_));
        std::uint32_t n = readU32(buf_, len_, off);
        std::uint64_t start = static_cast<std::uint64_t>(off) + 4;
        if (start > len_) return {};
        if (start + n > len_) n = static_cast<std::uint32_t>(len_ - start);
        return std::string_view(reinterpret_cast<const char*>(buf_ + start), n);
    }

    // Child count (vec slots only).
    std::uint32_t childCount() const {
        std::uint32_t nodeOff = static_cast<std::uint32_t>(readI64(buf_, len_, valOff_));
        return readU32(buf_, len_, nodeOff);
    }

    // Cursor for child i (vec slots only); O(1).
    Reader child(std::uint32_t i) const {
        std::uint32_t nodeOff = static_cast<std::uint32_t>(readI64(buf_, len_, valOff_));
        std::uint32_t tagsOff = readU32(buf_, len_, nodeOff + 4);
        std::uint32_t paysOff = readU32(buf_, len_, nodeOff + 8);
        return Reader(buf_, len_, tagsOff + i, paysOff + i * 8);
    }

private:
    static std::uint64_t readLE(const std::uint8_t* b, std::size_t len,
                                std::uint64_t off, int nbytes) {
        std::uint64_t r = 0;
        if (off + static_cast<std::uint64_t>(nbytes) <= len) {
            for (int k = 0; k < nbytes; ++k)
                r |= static_cast<std::uint64_t>(b[off + k]) << (8 * k);
        }
        return r;
    }
    static std::uint8_t  readU8 (const std::uint8_t* b, std::size_t l, std::uint64_t o) { return static_cast<std::uint8_t>(readLE(b, l, o, 1)); }
    static std::uint32_t readU32(const std::uint8_t* b, std::size_t l, std::uint64_t o) { return static_cast<std::uint32_t>(readLE(b, l, o, 4)); }
    static std::int64_t  readI64(const std::uint8_t* b, std::size_t l, std::uint64_t o) { return static_cast<std::int64_t>(readLE(b, l, o, 8)); }
    static double readF64(const std::uint8_t* b, std::size_t l, std::uint64_t o) {
        std::uint64_t bits = readLE(b, l, o, 8);
        double d; std::memcpy(&d, &bits, sizeof d); return d;
    }

    const std::uint8_t* buf_ = nullptr;
    std::size_t   len_  = 0;
    std::uint32_t tagOff_ = 0;
    std::uint32_t valOff_ = 0;
};

// ---------------------------------------------------------------------------
// Value -- a small in-memory Msg mirror, for building messages in C++.
// ---------------------------------------------------------------------------

struct Value {
    Tag tag = Tag::Bool;
    bool        b = false;
    std::int64_t i = 0;
    double      f = 0.0;
    std::string s;             // symbol / string text
    std::vector<Value> v;      // vec children

    static Value Bool(bool x)               { Value n; n.tag = Tag::Bool;   n.b = x; return n; }
    static Value Int(std::int64_t x)        { Value n; n.tag = Tag::Int;    n.i = x; return n; }
    static Value Float(double x)            { Value n; n.tag = Tag::Float;  n.f = x; return n; }
    static Value Symbol(std::string x)      { Value n; n.tag = Tag::Symbol; n.s = std::move(x); return n; }
    static Value String(std::string x)      { Value n; n.tag = Tag::String; n.s = std::move(x); return n; }
    static Value Vec(std::vector<Value> xs) { Value n; n.tag = Tag::Vec;    n.v = std::move(xs); return n; }
};

// ---------------------------------------------------------------------------
// Writer / encode
// ---------------------------------------------------------------------------

namespace detail {

inline void putLE(std::vector<std::uint8_t>& buf, std::uint64_t v, int nbytes) {
    for (int k = 0; k < nbytes; ++k) buf.push_back(static_cast<std::uint8_t>((v >> (8 * k)) & 0xFF));
}
inline void setU32At(std::vector<std::uint8_t>& buf, std::size_t off, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) buf[off + k] = static_cast<std::uint8_t>((v >> (8 * k)) & 0xFF);
}

struct Enc { std::uint8_t tag; bool isFloat; std::uint64_t ipayload; double fpayload; };

inline void putPayload(std::vector<std::uint8_t>& buf, const Enc& e) {
    if (e.isFloat) { std::uint64_t bits; std::memcpy(&bits, &e.fpayload, sizeof bits); putLE(buf, bits, 8); }
    else           { putLE(buf, e.ipayload, 8); }
}

inline std::uint32_t putBlob(std::vector<std::uint8_t>& buf, const std::string& s) {
    std::uint32_t off = static_cast<std::uint32_t>(buf.size());
    putLE(buf, 0, 4);                                  // placeholder length
    std::uint32_t dataOff = static_cast<std::uint32_t>(buf.size());
    buf.insert(buf.end(), s.begin(), s.end());
    setU32At(buf, off, static_cast<std::uint32_t>(buf.size() - dataOff));
    return off;
}

inline Enc encodeValue(std::vector<std::uint8_t>& buf, const Value& o);

inline std::uint32_t putNode(std::vector<std::uint8_t>& buf, const std::vector<Value>& v) {
    std::vector<Enc> encs;
    encs.reserve(v.size());
    for (const auto& e : v) encs.push_back(encodeValue(buf, e));

    std::uint32_t tagsOff = static_cast<std::uint32_t>(buf.size());
    for (const auto& e : encs) buf.push_back(e.tag);

    std::uint32_t paysOff = static_cast<std::uint32_t>(buf.size());
    for (const auto& e : encs) putPayload(buf, e);

    std::uint32_t nodeOff = static_cast<std::uint32_t>(buf.size());
    putLE(buf, encs.size(), 4);
    putLE(buf, tagsOff, 4);
    putLE(buf, paysOff, 4);
    return nodeOff;
}

inline Enc encodeValue(std::vector<std::uint8_t>& buf, const Value& o) {
    switch (o.tag) {
        case Tag::Bool:   return {0, false, o.b ? 1u : 0u, 0.0};
        case Tag::Int:    return {1, false, static_cast<std::uint64_t>(o.i), 0.0};
        case Tag::Float:  return {2, true, 0, o.f};
        case Tag::Symbol: return {3, false, putBlob(buf, o.s), 0.0};
        case Tag::String: return {4, false, putBlob(buf, o.s), 0.0};
        case Tag::Vec:    return {5, false, putNode(buf, o.v), 0.0};
    }
    return {0, false, 0, 0.0};
}

} // namespace detail

// Encode a Value to a TZB message buffer.
inline std::vector<std::uint8_t> encode(const Value& o) {
    std::vector<std::uint8_t> buf;
    buf.push_back(kMagic0); buf.push_back(kMagic1); buf.push_back(kMagic2); buf.push_back(kVersion);
    std::size_t rootPos = buf.size();          // == 4
    detail::putLE(buf, 0, 4);                   // placeholder rootOffset
    detail::Enc e = detail::encodeValue(buf, o);
    std::uint32_t slotOff = static_cast<std::uint32_t>(buf.size());
    buf.push_back(e.tag);
    detail::putPayload(buf, e);
    detail::setU32At(buf, rootPos, slotOff);
    return buf;
}

} // namespace tzpl::sbin
