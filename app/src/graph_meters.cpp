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

#include "graph_meters.hpp"

#include "tzpl_client_interface.hpp"
#include "tzpl_tap.hpp"

#include <vector>

namespace graph {

int MeterStore::enable(engine::Engine* e, MeterKey k) {
    if (!e) return tzpl_errInternal;
    if (meters_.count(k)) return tzpl_errNone; // idempotent

    long long tapID = (long long)engine::allocTapID(e);
    tzpl_SErr err = engine::begin(e);
    if (err == tzpl_errNone) {
        engine::tapOutlet(k.nodeID, k.outlet, tapID, engine::tapMeter);
        err = engine::go(k.silo);
    }
    if (err != tzpl_errNone) return (int)err;

    meters_[k] = MeterState{tapID, 0.f, 0.f, 0.f};
    return tzpl_errNone;
}

void MeterStore::disable(engine::Engine* e, MeterKey k) {
    auto it = meters_.find(k);
    if (it == meters_.end()) return;
    long long tapID = it->second.tapID;
    meters_.erase(it);
    if (!e) return;
    if (engine::begin(e) == tzpl_errNone) {
        engine::untap(tapID);
        engine::go(k.silo);
    }
}

bool MeterStore::enabled(MeterKey k) const {
    return meters_.count(k) != 0;
}

int MeterStore::enableNode(engine::Engine* e, int silo,
                           GraphViewModel const& vm, long long nodeID) {
    int idx = vm.indexOfNode(nodeID);
    if (idx < 0) return tzpl_errNodeNotFound;

    int firstErr = tzpl_errNone;
    auto const& n = vm.nodes[(size_t)idx];
    for (int o = 0; o < (int)n.outs.size(); ++o) {
        // The engine only taps f32 outlets; skip the rest rather than
        // collecting an error per integer/event port.
        if (n.outs[(size_t)o].elem != tzpl_kF32) continue;
        int err = enable(e, MeterKey{silo, nodeID, o});
        if (err != tzpl_errNone && firstErr == tzpl_errNone) firstErr = err;
    }
    return firstErr;
}

void MeterStore::disableNode(engine::Engine* e, int silo, long long nodeID) {
    std::vector<MeterKey> doomed;
    for (auto const& [k, st] : meters_) {
        if (k.silo == silo && k.nodeID == nodeID) doomed.push_back(k);
    }
    for (auto const& k : doomed) disable(e, k);
}

bool MeterStore::nodeEnabled(int silo, long long nodeID) const {
    for (auto const& [k, st] : meters_) {
        if (k.silo == silo && k.nodeID == nodeID) return true;
    }
    return false;
}

void MeterStore::prune(engine::Engine* e, GraphViewModel const& vm) {
    std::vector<MeterKey> doomed;
    for (auto const& [k, st] : meters_) {
        // Only judge the silo currently in view -- another silo's nodes are
        // simply absent from this view model, not gone.
        if (k.silo != vm.silo) continue;
        int idx = vm.indexOfNode(k.nodeID);
        if (idx < 0 || k.outlet >= (int)vm.nodes[(size_t)idx].outs.size())
            doomed.push_back(k);
    }
    for (auto const& k : doomed) disable(e, k);
}

void MeterStore::clearSilo(engine::Engine* e, int silo) {
    std::vector<MeterKey> doomed;
    for (auto const& [k, st] : meters_) {
        if (k.silo == silo) doomed.push_back(k);
    }
    for (auto const& k : doomed) disable(e, k);
}

void MeterStore::clear(engine::Engine* e) {
    std::vector<MeterKey> doomed;
    for (auto const& [k, st] : meters_) doomed.push_back(k);
    for (auto const& k : doomed) disable(e, k);
}

void MeterStore::poll(engine::Engine* e, float holdDecay) {
    if (!e) return;
    for (auto& [k, st] : meters_) {
        st.rms = engine::tapRms(e, st.tapID);
        st.peak = engine::tapPeak(e, st.tapID);
        st.peakHold *= holdDecay;
        if (st.peak > st.peakHold) st.peakHold = st.peak;
    }
}

MeterState const* MeterStore::find(MeterKey k) const {
    auto it = meters_.find(k);
    return it == meters_.end() ? nullptr : &it->second;
}

} // namespace graph
