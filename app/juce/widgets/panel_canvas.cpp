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
// The flow layout's margin doubles as the left edge of every unarranged
// widget, so it must sit ON the arrange grid (widget_component.cpp's
// snap8) -- otherwise no arranged widget can ever line up with a flowing
// one, since the snap can only land on multiples of 8.
constexpr int kGrid = 8;           // arrange snap grid
constexpr int kGap = kGrid;
constexpr int kFullWidthMax = 520; // slider-likes stretch up to this
constexpr int kTabH = 24;          // sub-panel tab strip height

// One step of the flow layout, rounded up to the grid so that flowing
// widgets sit at grid-aligned y as well as x. Sizes are untouched; a row
// just gains up to kGrid-1 extra px of gap.
int flowAdvance(int widgetHeight) {
    return (widgetHeight + kGap + kGrid - 1) / kGrid * kGrid;
}
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
            // Signature = values + noteData, so piano-roll edits (and any
            // undo/redo) repaint too, not just slider-like value changes.
            std::vector<double> sig = w->values;
            sig.insert(sig.end(), w->noteData.begin(), w->noteData.end());
            auto& cached = lastValues_[id];
            if (cached != sig) { cached = std::move(sig); toRepaint.push_back(id); }
        }
    }
    for (auto id : toRepaint)
        if (auto it = widgets_.find(id); it != widgets_.end())
            it->second->repaint();
}

std::vector<std::uint64_t> PanelCanvas::gatherPages() {
    // Distinct pages: the bare root ("(main)") if any widget targets the panel
    // exactly, plus one page per sub-panel "root/sub", in first-appearance
    // order. Mirrors NotebookPanel::drawPanelCanvas.
    std::vector<std::string> pages;
    std::unordered_map<std::string, std::vector<std::pair<std::uint64_t,
                                                          std::uint64_t>>> seqById;
    std::vector<std::uint64_t> unionIds;
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        bool bare = false;
        for (auto& wp : ui_.widgets) {
            if (!bridge::panelUnderRoot(wp->panel, panel_)) continue;
            if (wp->panel == panel_) bare = true;
            else if (std::find(pages.begin(), pages.end(), wp->panel)
                     == pages.end())
                pages.push_back(wp->panel);
            seqById[wp->panel].push_back({ wp->seq, wp->id });
        }
        if (bare || pages.empty())
            pages.insert(pages.begin(), panel_);
    }

    pages_ = std::move(pages);
    pageIds_.clear();
    for (auto& [panel, sv] : seqById) {
        std::sort(sv.begin(), sv.end());
        auto& ids = pageIds_[panel];
        for (auto& [seq, id] : sv) { ids.push_back(id); unionIds.push_back(id); }
    }
    std::sort(unionIds.begin(), unionIds.end());
    return unionIds;
}

bool PanelCanvas::reconcile() {
    auto prevPages = pages_;
    std::vector<std::uint64_t> ids = gatherPages();

    bool changed = ids != order_ || pages_ != prevPages;

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
            slot->onArrangeEnd = [this] {
                layOutWidgets();
                if (onArrangeCommit) onArrangeCommit();
            };
            slot->setArrange(arrange_, contentTop());
            addChildComponent(*slot);
        } else {
            slot->refreshMeta();
        }
    }
    order_ = ids;
    if (selectedPage_ >= (int)pages_.size()) selectedPage_ = 0;
    if (changed) { rebuildTabs(); layOutWidgets(); }
    return changed;
}

void PanelCanvas::rebuildTabs() {
    tabButtons_.clear();
    if (pages_.size() <= 1) return;
    for (int i = 0; i < (int)pages_.size(); ++i) {
        auto const& p = pages_[(size_t)i];
        juce::String label = (p == panel_)
            ? juce::String("main")
            : juce::String(p.substr(panel_.size() + 1));
        auto b = std::make_unique<juce::TextButton>(label);
        b->setColour(juce::TextButton::buttonColourId,
                     i == selectedPage_ ? juce::Colour(0xff4a70a0)
                                        : juce::Colour(0x30ffffff));
        b->onClick = [this, i] {
            selectedPage_ = i;
            rebuildTabs();
            layOutWidgets();
            if (auto* par = getParentComponent()) par->resized();
        };
        addAndMakeVisible(*b);
        tabButtons_.push_back(std::move(b));
    }
}

std::vector<std::uint64_t> const& PanelCanvas::visibleIds() const {
    static const std::vector<std::uint64_t> empty;
    if (pages_.empty()) return empty;
    int p = juce::jlimit(0, (int)pages_.size() - 1, selectedPage_);
    auto it = pageIds_.find(pages_[(size_t)p]);
    return it == pageIds_.end() ? empty : it->second;
}

int PanelCanvas::contentTop() const {
    return pages_.size() > 1 ? kTabH + kGap : kGap;
}

// Slider-likes stretch to the panel width when flowing; others keep size.
static bool stretchKind(bridge::UIWidgetKind k) {
    switch (k) {
        case bridge::UIWidgetKind::Slider:
        case bridge::UIWidgetKind::Range:
        case bridge::UIWidgetKind::Meter:
        case bridge::UIWidgetKind::MultiSlider:
        case bridge::UIWidgetKind::Scope:
        case bridge::UIWidgetKind::Plot:
        case bridge::UIWidgetKind::Waveform:
        case bridge::UIWidgetKind::Label:
            return true;
        default:
            return false;
    }
}

int PanelCanvas::preferredHeight(int width) const {
    int top = contentTop();
    int flowBottom = top, absBottom = top;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    for (auto id : visibleIds()) {
        auto it = widgets_.find(id);
        if (it == widgets_.end()) continue;
        auto pref = it->second->preferredSize();
        auto* uw = ui_.findById(id);
        if (uw && uw->fx >= 0.0f) {
            int fh = uw->fh > 0.0f ? (int)uw->fh : pref.y;
            absBottom = std::max(absBottom, top + (int)uw->fy + fh);
        } else {
            flowBottom += flowAdvance(pref.y);
        }
    }
    juce::ignoreUnused(width);
    return std::max({ 40, flowBottom, absBottom + kGap });
}

void PanelCanvas::resized() { layOutWidgets(); }

void PanelCanvas::setArrange(bool on) {
    arrange_ = on;
    int top = contentTop();
    for (auto& [id, comp] : widgets_) comp->setArrange(on, top);
    if (auto* p = getParentComponent()) p->resized();
    repaint();
}

void PanelCanvas::layOutWidgets() {
    int w = getWidth();
    if (pages_.size() > 1) {
        int x = kGap;
        for (auto& b : tabButtons_) {
            int tw = std::max(48, b->getBestWidthForHeight(kTabH - 4));
            b->setBounds(x, kGap / 2, tw, kTabH - 4);
            x += tw + 4;
        }
    }
    int top = contentTop();
    // Show only the selected page's widgets; hide the rest.
    auto const& vis = visibleIds();
    std::unordered_map<std::uint64_t, bool> shown;
    for (auto id : vis) shown[id] = true;
    for (auto& [id, comp] : widgets_)
        if (!shown.count(id)) comp->setVisible(false);

    int flowY = top;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    for (auto id : vis) {
        auto it = widgets_.find(id);
        if (it == widgets_.end()) continue;
        it->second->setVisible(true);
        auto pref = it->second->preferredSize();
        int cw = stretchKind(it->second->kind())
                     ? std::min(w - 2 * kGap, kFullWidthMax) : pref.x;
        auto* uw = ui_.findById(id);
        if (uw && uw->fx >= 0.0f) {
            // Placed (arranged): absolute frame, fw/fh default to preferred.
            int fw = uw->fw > 0.0f ? (int)uw->fw : cw;
            int fh = uw->fh > 0.0f ? (int)uw->fh : pref.y;
            it->second->setBounds((int)uw->fx, top + (int)uw->fy,
                                  std::max(fw, 24), std::max(fh, 14));
        } else {
            // Unplaced: flow top to bottom.
            it->second->setBounds(kGap, flowY, std::max(cw, 40), pref.y);
            flowY += flowAdvance(pref.y);
        }
    }
}

}
