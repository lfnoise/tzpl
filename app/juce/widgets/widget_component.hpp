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
//  widget_component.hpp
//  app (JUCE)
//
//  One live `ui` widget as a JUCE Component. Holds only (UIState*, id):
//  paint() and the mouse handlers lock UIState::mtx, look the widget up by
//  id, read/mutate it, and never enter the VM or engine while holding it
//  (that is the dispatcher's job, on the message thread between repaints).
//  One class dispatches all kinds, mirroring widget_draw.cpp's per-kind
//  functions.
//

#ifndef widget_component_hpp
#define widget_component_hpp

#include "tzpl_ui_state.hpp"
#include "widget_gestures.hpp"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>

namespace tzplapp {

class ControlsDispatcher;

class WidgetComponent : public juce::Component {
public:
    WidgetComponent(bridge::UIState& ui, std::uint64_t id,
                    ControlsDispatcher& dispatcher);

    std::uint64_t widgetId() const { return id_; }
    bridge::UIWidgetKind kind() const { return kind_; }
    bool isTapBacked() const;

    // Refresh the cached kind/name/label after a reconcile (the registry may
    // have re-upserted this id with a new kind).
    void refreshMeta();

    void paint(juce::Graphics& g) override;
    void mouseDown(juce::MouseEvent const& e) override;
    void mouseDrag(juce::MouseEvent const& e) override;
    void mouseUp(juce::MouseEvent const& e) override;
    void mouseDoubleClick(juce::MouseEvent const& e) override;
    void mouseWheelMove(juce::MouseEvent const& e,
                        juce::MouseWheelDetails const& wheel) override;

    // A widget's natural size (used by the panel flow layout).
    juce::Point<int> preferredSize() const;

private:
    // The horizontal track geometry (in local coords) for slider-like kinds.
    void trackGeometry(float& minx, float& usable) const;
    void markDirtyAndNotify(bridge::UIWidget& w, bool gestureEnd);

    // Per-kind paint (called with UIState::mtx held and `w` valid).
    void paintSlider(juce::Graphics&, bridge::UIWidget&);
    void paintRange(juce::Graphics&, bridge::UIWidget&);
    void paintNumber(juce::Graphics&, bridge::UIWidget&);
    void paintButton(juce::Graphics&, bridge::UIWidget&);
    void paintToggle(juce::Graphics&, bridge::UIWidget&);
    void paintXY(juce::Graphics&, bridge::UIWidget&);
    void paintMeter(juce::Graphics&, bridge::UIWidget&);
    void paintScope(juce::Graphics&, bridge::UIWidget&);
    void paintPlot(juce::Graphics&, bridge::UIWidget&);
    void paintWaveform(juce::Graphics&, bridge::UIWidget&);
    void paintMultiSlider(juce::Graphics&, bridge::UIWidget&);
    void paintMatrix(juce::Graphics&, bridge::UIWidget&);
    void paintPianoRoll(juce::Graphics&, bridge::UIWidget&);
    void paintLabel(juce::Graphics&, bridge::UIWidget&);

    bridge::UIState& ui_;
    std::uint64_t id_;
    ControlsDispatcher& dispatcher_;
    bridge::UIWidgetKind kind_ = bridge::UIWidgetKind::Slider;
    juce::String name_;

    // Drag state (message thread only).
    ui_gesture::DragAnchor dragAnchor_;
    bool dragging_ = false;
    int matrixPaintValue_ = -1; // value being painted across a matrix drag

    static constexpr int kLabelH = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WidgetComponent)
};

}

#endif /* widget_component_hpp */
