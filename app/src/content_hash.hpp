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
//  content_hash.hpp
//  app
//
//  Content addressing for the notebook document model.
//
//  Every hashable object (Preset, WidgetSnap, Cell, DocSnapshot) has ONE
//  canonical representation: its TZB (tzpl::sbin) Value tree, built by the
//  -- TZB is the binary message format documented in lang/docs/FFI_Guide.html
//  section 15, with the layout defined in shared/tzpl_sexpr_bin.hpp --
//  toValue functions below. The content hash is a 64-bit FNV-1a over the
//  encoded bytes of that tree, lazily cached in the object (doc::CHash).
//  The .tzd writer consumes the same builders, so the hash, the equality
//  fast path, and the on-disk representation can never drift apart.
//
//  Hashes are an EQUALITY FAST PATH, never trusted alone: interners and
//  comparisons verify with structural equality before collapsing pointers,
//  so a collision can only miss a dedup, never corrupt a document.
//
//  Float caveats (documented, deliberate): doubles hash by bit pattern, so
//  -0.0 and 0.0 hash differently (missed dedup at worst); NaN-bearing
//  objects never dedup because the structural == verify fails -- matching
//  the existing WidgetSnap::operator== semantics.
//

#ifndef content_hash_hpp
#define content_hash_hpp

#include "document.hpp"
#include "tzpl_sexpr_bin.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace doc {

// --- hash primitive ---

std::uint64_t fnv1a64(std::uint8_t const* p, std::size_t n);

// FNV-1a over the TZB encoding of `v` (never returns 0; 0 means "unset"
// in CHash caches).
std::uint64_t contentHash(tzpl::sbin::Value const& v);

// --- canonical Value builders (shared with the .tzd writer) ---

// (lo hi init warpInt warpParam)
tzpl::sbin::Value specValue(bridge::UISpec const& s);

// (preset name (entry panel widget (v...))...)
tzpl::sbin::Value presetValue(Preset const& p);

// (cell id kindInt name text runOnLoad panelHeight collapsed (presets ...))
tzpl::sbin::Value cellValue(Cell const& c);

// (wsnap panel name kindInt spec spec2 (v...) (fx fy fw fh) rows cols
//  (notes...) labelText rollBeats rollLowPitch rollRows rollEdo keyChord
//  momentary (cellLabels...))
// History-table / hashing record only; the live "panels" section of the
// .tzd format keeps its own shape.
tzpl::sbin::Value widgetSnapValue(WidgetSnap const& s);

// --- cached content hashes ---

std::uint64_t contentHashOf(Preset const& p);
std::uint64_t contentHashOf(WidgetSnap const& s);
std::uint64_t contentHashOf(Cell const& c);
// Hash-of-hashes over (nextCellId, cell hashes, separator, hasWidgets,
// widget hashes) -- O(children) once child hashes are cached.
std::uint64_t contentHashOf(DocSnapshot const& s);

// --- structural equality (the verify side of the fast path) ---

bool operator==(PresetEntry const& a, PresetEntry const& b);
bool operator==(Preset const& a, Preset const& b);
bool sameCell(Cell const& a, Cell const& b);
// Element-wise with pointer short-circuit and cached-hash inequality
// pre-check.
bool sameWidgetLists(WidgetSnapList const& a, WidgetSnapList const& b);
bool sameSnapshot(DocSnapshot const& a, DocSnapshot const& b);

inline bool sameContent(Preset const& a, Preset const& b) { return a == b; }
inline bool sameContent(WidgetSnap const& a, WidgetSnap const& b) { return a == b; }
inline bool sameContent(Cell const& a, Cell const& b) { return sameCell(a, b); }
inline bool sameContent(DocSnapshot const& a, DocSnapshot const& b) { return sameSnapshot(a, b); }

// --- interner ---

// Content-addressed pointer pool: intern() returns the canonical
// shared_ptr for a value's content. Buckets hold weak_ptrs, so the pool
// never extends lifetime (the history cap still frees memory); expired
// entries are purged during bucket scans. GUI-thread-only, like
// DocumentStore.
template <class T>
class Interner {
    std::unordered_map<std::uint64_t,
                       std::vector<std::weak_ptr<T const>>> buckets_;
public:
    std::shared_ptr<T const> intern(std::shared_ptr<T const> p) {
        if (!p) return p;
        std::uint64_t h = contentHashOf(*p);
        auto& bucket = buckets_[h];
        for (auto it = bucket.begin(); it != bucket.end();) {
            if (auto q = it->lock()) {
                if (q == p || sameContent(*q, *p)) return q;
                ++it;
            } else {
                it = bucket.erase(it);
            }
        }
        bucket.push_back(p);
        return p;
    }

    // Live (non-expired) entry count -- for the History window stats line.
    int liveCount() const {
        int n = 0;
        for (auto const& [h, bucket] : buckets_) {
            for (auto const& w : bucket) {
                if (!w.expired()) ++n;
            }
        }
        return n;
    }
};

struct InternPool {
    Interner<Preset> presets;
    Interner<WidgetSnap> snaps;
    Interner<Cell> cells;
    Interner<DocSnapshot> snapshots;
};

} // namespace doc

#endif /* content_hash_hpp */
