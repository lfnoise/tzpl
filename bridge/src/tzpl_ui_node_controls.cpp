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
//  tzpl_ui_node_controls.cpp
//  bridge
//

#include "tzpl_ui_node_controls.hpp"

#include "tzpl_tap.hpp"
#include "tzpl_ui_taps.hpp"

#include "tzpl_audio_file.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace bridge {

UISpec specFromControl(tzpl_ControlSpec const& cs) {
    UISpec spec;
    spec.lo = cs.lo;
    spec.hi = cs.hi;
    spec.init = cs.init;
    int warp = static_cast<int>(cs.warp) - 1;
    if (warp < 0 || warp > static_cast<int>(UIWarp::Cubed)) warp = 0;
    spec.warp = static_cast<UIWarp>(warp);
    spec.warpParam = cs.param;
    return spec;
}

UIWidgetKind widgetKindForControl(tzpl_ControlKind kind) {
    switch (kind) {
        case tzpl_ckTrigger: return UIWidgetKind::Button;
        case tzpl_ckBoolean: return UIWidgetKind::Toggle;
        case tzpl_ckSelect:  return UIWidgetKind::Number;
        case tzpl_ckContinuous:
        default:             return UIWidgetKind::Slider;
    }
}

std::uint64_t bindControlWidget(UIState* ui, std::string const& panel,
                                engine::ControlDesc const& c,
                                long long nodeID, int silo) {
    if (!ui) return 0;
    UISpec spec = specFromControl(c.spec);
    int const chans = std::max(1, c.type.chans);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w;
    if (chans == 1) {
        w = ui->upsert(panel, c.name, widgetKindForControl(c.spec.kind), spec, {});
    } else if (c.spec.kind == tzpl_ckBoolean || c.spec.kind == tzpl_ckTrigger) {
        // Multichannel on/off or trigger: a 1 x chans button row. The
        // dispatcher sends the whole cell vector as ONE setControl (there
        // is no per-element setControl form, so per-channel widgets could
        // not bind independently).
        w = ui->upsert(panel, c.name, UIWidgetKind::ButtonMatrix, UISpec{}, {});
        if (w->rows != 1 || w->cols != chans) {
            w->values.assign((size_t)chans, 0.0);
            if (!w->cellLabels.empty())
                w->cellLabels.assign((size_t)chans, std::string());
            w->rows = 1;
            w->cols = chans;
            w->labelsVersion++;
        } else {
            w->values.resize((size_t)chans, 0.0);
        }
        w->momentary = c.spec.kind == tzpl_ckTrigger;
    } else {
        // Multichannel continuous/select: one bar per channel, all mapped
        // through the control's spec; sent as one vector setControl.
        w = ui->upsert(panel, c.name, UIWidgetKind::MultiSlider, spec, {});
        w->values.resize((size_t)chans, spec.clamp(spec.init));
        for (auto& v : w->values) v = spec.clamp(v);
    }
    w->target = UIEngineTarget{static_cast<long>(nodeID),
                               static_cast<long>(c.controlID), silo};
    w->dirtyEngine = true;  // push current value(s) through the fresh binding
    return w->id;
}

int materializeNodeControls(UIState* ui, engine::Engine* e,
                            std::string const& panel,
                            long long nodeID, int silo,
                            char const* defName) {
    if (!ui || !e || !defName) return -1;
    std::vector<engine::ControlDesc> controls;
    if (!engine::listDefControls(e, defName, controls)) return -1;

    int bound = 0;
    for (auto const& c : controls)
        if (bindControlWidget(ui, panel, c, nodeID, silo)) ++bound;
    return bound;
}

std::uint64_t bindTapWidget(UIState* ui, engine::Engine* e,
                            std::string const& panel, std::string const& name,
                            UIWidgetKind kind, long long nodeID, int outlet,
                            int silo, int* engineErr) {
    if (engineErr) *engineErr = tzpl_errNone;
    if (!ui || !e) return 0;

    bool master = nodeID < 0;
    if (master) silo = 0; // the post-limiter section runs on silo 0

    // Claim the tap id and hand it to the widget first, so a retap can
    // release the old tap after the lock is dropped.
    long oldTap = 0;
    int oldSilo = 0;
    long tapID = (long)engine::allocTapID(e);
    std::uint64_t widgetID = 0;
    {
        std::lock_guard<std::mutex> lock(ui->mtx);
        UIWidget* w = ui->upsert(panel, name, kind, UISpec{}, UISpec{});
        oldTap = w->tapID;
        oldSilo = w->tapSilo;
        w->tapID = tapID;
        w->tapSilo = silo;
        widgetID = w->id;
    }
    if (oldTap) untapWidget(e, oldTap, oldSilo);

    // Scope and Spectrum both need the sample FIFO; a meter only needs the
    // running peak/rms.
    int mode = kind == UIWidgetKind::Meter ? engine::tapMeter : engine::tapScope;
    tzpl_SErr err = engine::begin(e);
    if (err == tzpl_errNone) {
        if (master) engine::tapMaster(tapID, mode);
        else engine::tapOutlet(nodeID, outlet, tapID, mode);
        err = engine::go(silo);
    }
    if (err != tzpl_errNone) {
        if (engineErr) *engineErr = (int)err;
        std::lock_guard<std::mutex> lock(ui->mtx);
        if (UIWidget* w = ui->findById(widgetID)) w->tapID = 0;
    }
    return widgetID;
}

std::uint64_t bindWaveformWidget(UIState* ui, std::string const& panel,
                                 std::string const& name, char const* path) {
    if (!ui || !path) return 0;
    tzpl_Buffer* buffer = tzpl_loadAudioFile(path, 0, 0, INT64_MAX);
    if (!buffer) return 0;

    // Min/max overview of channel 0.
    constexpr int kBins = 2048;
    std::int64_t frames = buffer->length;
    int bins = static_cast<int>(std::min<std::int64_t>(kBins, frames));
    std::vector<float> mins(bins, 0.0f), maxs(bins, 0.0f);
    double const* ch0 = buffer->data[0];
    for (int b = 0; b < bins; ++b) {
        std::int64_t i0 = frames * b / bins;
        std::int64_t i1 = frames * (b + 1) / bins;
        if (i1 <= i0) i1 = i0 + 1;
        float lo = static_cast<float>(ch0[i0]), hi = lo;
        for (std::int64_t i = i0 + 1; i < i1 && i < frames; ++i) {
            float v = static_cast<float>(ch0[i]);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        mins[b] = lo;
        maxs[b] = hi;
    }
    tzpl_freeBuffer(buffer);

    std::lock_guard<std::mutex> lock(ui->mtx);
    UIWidget* w = ui->upsert(panel, name, UIWidgetKind::Waveform,
                             UISpec{}, UISpec{});
    w->waveMin = std::move(mins);
    w->waveMax = std::move(maxs);
    w->waveFrames = frames;
    return w->id;
}

} // namespace bridge
