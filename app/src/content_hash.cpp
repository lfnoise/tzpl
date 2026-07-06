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
//  content_hash.cpp
//  app
//
//  Canonical Value builders + cached content hashes + structural equality.
//  See content_hash.hpp for the design contract.
//

#include "content_hash.hpp"

namespace doc {

using tzpl::sbin::Value;

// --- hash primitive ---

std::uint64_t fnv1a64(std::uint8_t const* p, std::size_t n) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::uint64_t contentHash(Value const& v) {
    auto bytes = tzpl::sbin::encode(v);
    std::uint64_t h = fnv1a64(bytes.data(), bytes.size());
    return h ? h : 1;   // 0 is the CHash "unset" sentinel
}

// --- canonical Value builders ---

Value specValue(bridge::UISpec const& s) {
    return Value::Vec({Value::Float(s.lo), Value::Float(s.hi),
                       Value::Float(s.init), Value::Int((int)s.warp),
                       Value::Float(s.warpParam)});
}

Value presetValue(Preset const& p) {
    std::vector<Value> pv{Value::Symbol("preset"), Value::String(p.name)};
    for (auto const& e : p.entries) {
        std::vector<Value> vals;
        for (double v : e.values) vals.push_back(Value::Float(v));
        pv.push_back(Value::Vec({Value::Symbol("entry"),
                                 Value::String(e.panel),
                                 Value::String(e.widget),
                                 Value::Vec(std::move(vals))}));
    }
    return Value::Vec(std::move(pv));
}

Value cellValue(Cell const& c) {
    std::vector<Value> ps{Value::Symbol("presets")};
    for (auto const& pp : c.presets) ps.push_back(presetValue(*pp));
    return Value::Vec({
        Value::Symbol("cell"),
        Value::Int((std::int64_t)c.id),
        Value::Int((int)c.kind),
        Value::String(c.name),
        Value::String(c.text),
        Value::Bool(c.runOnLoad),
        Value::Float(c.panelHeight),
        Value::Bool(c.collapsed),
        Value::Vec(std::move(ps)),
    });
}

Value widgetSnapValue(WidgetSnap const& s) {
    std::vector<Value> values;
    for (double v : s.values) values.push_back(Value::Float(v));
    std::vector<Value> notes;
    for (float v : s.noteData) notes.push_back(Value::Float(v));
    return Value::Vec({
        Value::Symbol("wsnap"),
        Value::String(s.panel),
        Value::String(s.name),
        Value::Int((int)s.kind),
        specValue(s.spec),
        specValue(s.spec2),
        Value::Vec(std::move(values)),
        Value::Vec({Value::Float(s.fx), Value::Float(s.fy),
                    Value::Float(s.fw), Value::Float(s.fh)}),
        Value::Int(s.rows),
        Value::Int(s.cols),
        Value::Vec(std::move(notes)),
        Value::String(s.labelText),
        Value::Float(s.rollBeats),
        Value::Int(s.rollLowPitch),
        Value::Int(s.rollRows),
        Value::Int(s.rollEdo),
        Value::String(s.keyChord),
    });
}

// --- cached content hashes ---

std::uint64_t contentHashOf(Preset const& p) {
    if (!p.chash.v) p.chash.v = contentHash(presetValue(p));
    return p.chash.v;
}

std::uint64_t contentHashOf(WidgetSnap const& s) {
    if (!s.chash.v) s.chash.v = contentHash(widgetSnapValue(s));
    return s.chash.v;
}

std::uint64_t contentHashOf(Cell const& c) {
    if (!c.chash.v) c.chash.v = contentHash(cellValue(c));
    return c.chash.v;
}

std::uint64_t contentHashOf(DocSnapshot const& s) {
    if (s.chash.v) return s.chash.v;
    // Hash-of-hashes: mix the children's cached content hashes instead of
    // re-encoding the whole document.
    std::vector<std::uint64_t> stream;
    stream.reserve(2 + s.cells.size()
                   + (s.widgets ? s.widgets->size() : 0) + 2);
    stream.push_back((std::uint64_t)s.nextCellId);
    for (auto const& c : s.cells) stream.push_back(contentHashOf(*c));
    stream.push_back(0xffffffffffffffffull);   // cells / widgets separator
    stream.push_back(s.widgets ? 1 : 0);
    if (s.widgets) {
        for (auto const& w : *s.widgets) stream.push_back(contentHashOf(*w));
    }
    std::uint64_t h = fnv1a64(
        reinterpret_cast<std::uint8_t const*>(stream.data()),
        stream.size() * sizeof(std::uint64_t));
    s.chash.v = h ? h : 1;
    return s.chash.v;
}

// --- structural equality ---

bool operator==(PresetEntry const& a, PresetEntry const& b) {
    return a.panel == b.panel && a.widget == b.widget && a.values == b.values;
}

bool operator==(Preset const& a, Preset const& b) {
    return a.name == b.name && a.entries == b.entries;
}

bool sameCell(Cell const& a, Cell const& b) {
    if (a.id != b.id || a.kind != b.kind || a.name != b.name
        || a.text != b.text || a.runOnLoad != b.runOnLoad
        || a.panelHeight != b.panelHeight || a.collapsed != b.collapsed
        || a.presets.size() != b.presets.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.presets.size(); ++i) {
        if (a.presets[i] == b.presets[i]) continue;   // shared pointer
        if (!a.presets[i] || !b.presets[i]) return false;
        if (!(*a.presets[i] == *b.presets[i])) return false;
    }
    return true;
}

bool sameWidgetLists(WidgetSnapList const& a, WidgetSnapList const& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) continue;   // shared pointer
        if (!a[i] || !b[i]) return false;
        // Cached-hash pre-check: only trusted for INEQUALITY (equality is
        // always verified structurally below).
        if (a[i]->chash.v && b[i]->chash.v
            && a[i]->chash.v != b[i]->chash.v) {
            return false;
        }
        if (!(*a[i] == *b[i])) return false;
    }
    return true;
}

bool sameSnapshot(DocSnapshot const& a, DocSnapshot const& b) {
    if (a.nextCellId != b.nextCellId) return false;
    if (a.cells.size() != b.cells.size()) return false;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        if (a.cells[i] == b.cells[i]) continue;   // shared pointer
        if (!a.cells[i] || !b.cells[i]) return false;
        if (!sameCell(*a.cells[i], *b.cells[i])) return false;
    }
    if (a.widgets == b.widgets) return true;
    if (a.widgets && b.widgets) return sameWidgetLists(*a.widgets, *b.widgets);
    // One side null: equal only if the other is empty.
    auto const* nonNull = (a.widgets ? a.widgets : b.widgets).get();
    return !nonNull || nonNull->empty();
}

} // namespace doc
