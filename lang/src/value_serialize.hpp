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
//  value_serialize.hpp
//  lang
//
//  TZV1: typed binary serialization of arbitrary (possibly cyclic) value
//  graphs.
//
//  Layout (little-endian, varint = LEB128 unsigned):
//      magic   'T' 'Z' 'V' '1'
//      sig     varint len + structural type signature of the root type
//      nObjs   varint          -- heap objects, ids 1..nObjs (0 = nil)
//      headers per object: varint typeIndex + u8 kind + varint extra
//              (typeIndex indexes the deterministic pre-order walk of the
//               root type that buildTypeSig performs; extra = count /
//               byteLen / which / isInfinite depending on kind)
//      contents per object, in id order
//      root    one value of the root type
//
//  Child references are object ids, so shared substructure is written once
//  (DAG dedup) and cycles are just back/forward references. Map/Set/PMap
//  entries are ordered by a self-contained canonical encoding of the key,
//  making the bytes a deterministic function of the value, independent of
//  construction order (content-addressing friendly). No pointers are ever
//  emitted.
//
//  The type signature is structural (field/case names + recursive layouts,
//  with backrefs for recursive types); deserialize<T> validates it against
//  T by byte equality. Renaming a struct/enum or its fields intentionally
//  breaks compatibility.
//

#ifndef value_serialize_hpp
#define value_serialize_hpp

#include "value.hpp"

namespace ts {

// Is `t` serializable? (Data types only: no functions, coroutines, futures,
// actors, Any, or existentials anywhere in the type.) Used by the resolvers
// so unsupported types are rejected at compile time.
bool isSerializableType(Type* t);

// Append the structural signature of `t` to `out`. When `typeOrder` is
// non-null, every distinct type node is appended on first visit -- this is
// the type-index space used by object headers (encoder and decoder derive
// identical tables from identical signatures).
void buildTypeSig(Type* t, Vec<u8>& out, Vec<Type*>* typeOrder);

// Serialize the (possibly multi-word) value at `base` of type `type` into
// a complete TZV1 buffer appended to `out`. Throws std::runtime_error on
// unbounded lazy lists.
void serializeValue(Word const* base, Type* type, Vec<u8>& out);

// Decode a TZV1 buffer into `dst` (strideForType(type) words). Throws
// std::runtime_error on malformed input or a signature mismatch.
void deserializeValue(u8 const* data, size_t len, Type* type, Word* dst);

} // namespace ts

#endif /* value_serialize_hpp */
