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
//  panel_canvas.cpp
//  app (JUCE)
//

#include "panel_canvas.hpp"
#include "controls_dispatch.hpp"
#include "tzpl_ui_state.hpp"
#include <algorithm>

namespace tzplapp {

namespace {
constexpr int kGap = 6;
constexpr int kFullWidthMax = 520; // slider-likes stretch up to this
}

PanelCanvas::PanelCanvas(bridge::UIState& ui, std::string panel,
                         ControlsDispatcher& dispatcher)
    : ui_(ui), panel_(std::move(panel)), dispatcher_(dispatcher)
{
    reconcile();
    startTimerHz(20); // reconcile + repaint for tap animation / new widgets
}

PanelCanvas::~PanelCanvas() = default;

void PanelCanvas::timerCallback() {
    if (reconcile()) {
        if (auto* p = getParentComponent()) p->resized();
    }
    // Repaint tap-backed widgets (meters/scopes animate) and any widget whose
    // values changed since we last painted it -- catches preset recall,
    // undo/redo and key bindings, which mutate the registry directly.
    std::vector<std::uint64_t> toRepaint;
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        for (auto& [id, comp] : widgets_) {
            if (comp->isTapBacked()) { toRepaint.push_back(id); continue; }
            auto* w = ui_.findById(id);
            if (!w) continue;
            auto& cached = lastValues_[id];
            if (cached != w->values) { cached = w->values; toRepaint.push_back(id); }
        }
    }
    for (auto id : toRepaint)
        if (auto it = widgets_.find(id); it != widgets_.end())
            it->second->repaint();
}

bool PanelCanvas::reconcile() {
    std::vector<std::uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        // Collect this panel's widget ids in seq order.
        std::vector<std::pair<std::uint64_t, std::uint64_t>> seqId; // (seq, id)
        for (auto& wp : ui_.widgets)
            if (bridge::panelUnderRoot(wp->panel, panel_))
                seqId.push_back({ wp->seq, wp->id });
        std::sort(seqId.begin(), seqId.end());
        for (auto& [seq, id] : seqId) ids.push_back(id);
    }

    bool changed = ids != order_;

    // Remove components whose widget vanished.
    std::unordered_map<std::uint64_t, bool> live;
    for (auto id : ids) live[id] = true;
    for (auto it = widgets_.begin(); it != widgets_.end();) {
        if (!live.count(it->first)) it = widgets_.erase(it);
        else ++it;
    }
    // Create components for new widgets.
    for (auto id : ids) {
        auto& slot = widgets_[id];
        if (!slot) {
            slot = std::make_unique<WidgetComponent>(ui_, id, dispatcher_);
            addAndMakeVisible(*slot);
        } else {
            slot->refreshMeta();
        }
    }
    order_ = ids;
    if (changed) layOutWidgets();
    return changed;
}

int PanelCanvas::preferredHeight(int width) const {
    int y = kGap;
    for (auto id : order_) {
        auto it = widgets_.find(id);
        if (it == widgets_.end()) continue;
        y += it->second->preferredSize().y + kGap;
    }
    juce::ignoreUnused(width);
    return std::max(y, 40);
}

void PanelCanvas::resized() { layOutWidgets(); }

void PanelCanvas::layOutWidgets() {
    int w = getWidth();
    int y = kGap;
    for (auto id : order_) {
        auto it = widgets_.find(id);
        if (it == widgets_.end()) continue;
        auto pref = it->second->preferredSize();
        int cw = pref.x;
        // Slider-likes stretch to the panel width (capped); others keep size.
        switch (it->second->kind()) {
            case bridge::UIWidgetKind::Slider:
            case bridge::UIWidgetKind::Range:
            case bridge::UIWidgetKind::Meter:
            case bridge::UIWidgetKind::MultiSlider:
            case bridge::UIWidgetKind::Scope:
            case bridge::UIWidgetKind::Plot:
            case bridge::UIWidgetKind::Waveform:
            case bridge::UIWidgetKind::Label:
                cw = std::min(w - 2 * kGap, kFullWidthMax);
                break;
            default:
                break;
        }
        it->second->setBounds(kGap, y, std::max(cw, 40), pref.y);
        y += pref.y + kGap;
    }
}

}
