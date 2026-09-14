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
//  tzpl_builtin_defs.cpp
//  audio engine
//
//  See tzpl_builtin_defs.hpp.
//

#include "tzpl_builtin_defs.hpp"
#include "tzpl_node.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace engine {

namespace {

// One processAudio serves every channel count: the width is read from the
// node's own inlet type at init, so a single def body covers _wire1, _wire2,
// _wire8, ... without a template instantiation per width.
struct FixedFn : tzpl_SynthData {
    int chans = 0;
};

tzpl_SynthData* FixedFn_alloc() { return new FixedFn(); }

tzpl_SErr FixedFn_free(tzpl_SynthData* s) {
    delete static_cast<FixedFn*>(s);
    return tzpl_errNone;
}

tzpl_SErr FixedFn_init(tzpl_SynthData* s) {
    auto* fx = static_cast<FixedFn*>(s);
    auto* node = reinterpret_cast<Node*>(s->node);
    fx->chans = node->ins.empty() ? 0 : node->ins[0].type_.chans;
    return tzpl_errNone;
}

// out = in
void Wire_processAudio(tzpl_SynthData* s) {
    auto* fx = static_cast<FixedFn*>(s);
    memcpy(s->outlets[0], s->inlets[0], sizeof(f32) * fx->chans);
}

// out = in * vol   (vol: 1-channel inlet, constant or ramped by the engine)
void Gain_processAudio(tzpl_SynthData* s) {
    auto* fx = static_cast<FixedFn*>(s);
    f32 const* in = static_cast<f32 const*>(s->inlets[0]);
    f32 const vol = *static_cast<f32 const*>(s->inlets[1]);
    f32* out = static_cast<f32*>(s->outlets[0]);
    for (int c = 0; c < fx->chans; ++c) out[c] = in[c] * vol;
}

// The def name and its port table must outlive the NodeDef (NodeDefInfo
// holds raw pointers), so each (kind, chans) is built once per process and
// kept here. Guarded: two NRT-side callers (host VM thread, a render thread)
// may ask for the same width at once.
struct Registered {
    std::string name;
    PortInfo ins[2];
    PortInfo out[1];
};

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, Registered*>& registry() {
    static std::unordered_map<std::string, Registered*> r;
    return r;
}

char const* ensureDef(Engine* e, char const* prefix, int chans, bool gain) {
    std::string name = std::string(prefix) + std::to_string(chans);

    Registered* reg;
    {
        std::lock_guard<std::mutex> lck(registryMutex());
        auto& r = registry();
        auto it = r.find(name);
        if (it == r.end()) {
            reg = new Registered();
            reg->name = name;
            tzpl_SignalType wide{tzpl_kF32, tzpl_audioRate, chans};
            tzpl_SignalType mono{tzpl_kF32, tzpl_audioRate, 1};
            reg->ins[0] = PortInfo{"in", wide};
            reg->ins[1] = PortInfo{"vol", mono};
            reg->out[0] = PortInfo{"out", wide};
            r[name] = reg;
        } else {
            reg = it->second;
        }
    }

    // Already on this engine (a second call, or copied in from the live
    // engine by copyNodeDefs for an NRT render).
    if (getNodeDef(e, reg->name.c_str())) return reg->name.c_str();

    NodeDefInfo info;
    memset(&info, 0, sizeof(info));
    info.name = reg->name.c_str();
    info.num_ins = gain ? 2 : 1;
    info.num_outs = 1;
    info.ins = reg->ins;
    info.outs = reg->out;
    info.funs.alloc = FixedFn_alloc;
    info.funs.free = FixedFn_free;
    info.funs.init = FixedFn_init;
    info.funs.processAudio = gain ? Gain_processAudio : Wire_processAudio;
    addNodeDef(e, info);
    return reg->name.c_str();
}

} // namespace

char const* ensureWireDef(Engine* e, int chans) {
    return ensureDef(e, "_wire", chans, /*gain=*/false);
}

char const* ensureGainDef(Engine* e, int chans) {
    return ensureDef(e, "_gain", chans, /*gain=*/true);
}

} // namespace engine
