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
//  graph_view.hpp
//  app (JUCE)
//
//  The audio graph display and editor: the third center-pane mode
//  alongside the text editor and notebook. Shows one silo's running
//  nodes as boxes (inlet pins left, outlet pins right) and connections
//  as wires, polling the engine's topology shadow at ~8 Hz while
//  visible (one atomic read per tick when nothing changed). Nodes
//  auto-lay out left-to-right toward Audio Out; dragged nodes keep
//  their position for the session. Toolbar: silo selector, Re-layout,
//  Fit.
//
//  Editing: drag from a pin to a compatible pin to connect (legal drop
//  targets highlight; the engine revalidates at submit); Delete removes
//  the selected wire or frees the selected node; right-click the
//  background for a new-node palette, right-click a node to disconnect
//  or free it. Edits submit begin/../go bundles on the message thread;
//  there is no optimistic UI -- the commit bumps the graph generation
//  and the next poll redraws. Errors go to `onLog` (the app console).
//

#ifndef graph_view_hpp
#define graph_view_hpp

#include "graph_layout.hpp"
#include "graph_model.hpp"

#include <juce_gui_basics/juce_gui_basics.h>

namespace bridge { struct AppContext; }

namespace tzplapp {

class GraphView : public juce::Component, private juce::Timer {
public:
    explicit GraphView(bridge::AppContext& appCtx);

    // Drop the poller cache and re-snapshot immediately (mode entry).
    void refreshNow();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

    void mouseDown(juce::MouseEvent const& e) override;
    void mouseDrag(juce::MouseEvent const& e) override;
    void mouseUp(juce::MouseEvent const& e) override;
    void mouseWheelMove(juce::MouseEvent const& e,
                        juce::MouseWheelDetails const& wheel) override;
    void mouseMagnify(juce::MouseEvent const& e, float scaleFactor) override;
    void mouseDoubleClick(juce::MouseEvent const& e) override;
    bool keyPressed(juce::KeyPress const& key) override;

    // Edit feedback line for the app console (message, isError).
    std::function<void(std::string const&, bool)> onLog;

    // Open a control panel for a node (double-click / context menu).
    // Args: nodeID, defName, silo. The owner (MainComponent) materializes
    // the widgets and floats the controls window.
    std::function<void(long long, std::string const&, int)> onOpenNodeControls;

private:
    void logLine(juce::String const& msg, bool isError) {
        if (onLog) onLog(msg.toStdString(), isError);
    }

    // A pin reference: index into vm_.nodes + port index + direction.
    struct PinRef {
        int node = -1;
        int port = 0;
        bool input = false;
        bool valid() const { return node >= 0; }
        bool operator==(PinRef const&) const = default;
    };

    PinRef hitPin(juce::Point<float> worldPt) const;
    // True when connecting srcPin (an outlet) to dstPin (an inlet) passes
    // the engine's type rules as far as we can tell (unknown defs pass --
    // the engine is the authority at submit).
    bool dropIsCompatible(PinRef srcPin, PinRef dstPin) const;
    void finishWireDrag(juce::Point<float> worldPt);
    void showBackgroundMenu(juce::Point<float> worldPt);
    void showNodeMenu(int nodeIndex);
    // Log err to the console if nonzero, else re-snapshot immediately.
    void afterEdit(int err, juce::String const& what);
    void timerCallback() override;
    void pollNow();
    void rebuildLayout();   // measure node boxes, then autoLayout
    void zoomToFit();
    void zoomAbout(juce::Point<float> screenPt, float factor);

    juce::Rectangle<int> canvasArea() const;
    juce::AffineTransform worldTransform() const;
    juce::Point<float> toWorld(juce::Point<float> screenPt) const;

    // World-space pin centre for a node's inlet/outlet row.
    juce::Point<float> pinCentre(graph::NodeVM const& n, bool input, int port) const;
    juce::Path edgePath(graph::EdgeVM const& e) const;
    int hitNode(juce::Point<float> worldPt) const;  // index or -1, topmost last
    int hitEdge(juce::Point<float> worldPt) const;  // index or -1

    bridge::AppContext& appCtx_;
    graph::GraphPoller poller_;
    graph::GraphViewModel vm_;
    graph::LayoutStore layoutStore_;

    // Silo selector: a segmented row of toggle buttons for small silo
    // counts, a dropdown only when there are many silos.
    juce::Label siloLabel_{{}, "Silo"};
    std::vector<std::unique_ptr<juce::TextButton>> siloButtons_;
    juce::ComboBox siloBox_; // used only when the count is large
    juce::TextButton relayoutButton_{"Re-layout"}, fitButton_{"Fit"};
    void selectSilo(int s);

    int silo_ = 0;
    float zoom_ = 1.f;
    juce::Point<float> pan_{0.f, 0.f};
    bool didInitialFit_ = false;

    long long selectedNode_ = -1;
    int selectedEdge_ = -1;
    int draggingNode_ = -1;              // index into vm_.nodes during a drag
    juce::Point<float> dragGrabOffset_;  // world offset from node origin
    bool panning_ = false;
    juce::Point<float> panAnchor_;       // pan_ value at mouseDown
    juce::Point<float> panMouseStart_;   // screen position at mouseDown

    // Wire drag (started on a pin; may run in either direction).
    PinRef wireFrom_;                    // invalid when not wire-dragging
    juce::Point<float> wireMouseWorld_;
    PinRef wireHover_;                   // compatible pin under the mouse

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphView)
};

}

#endif /* graph_view_hpp */
