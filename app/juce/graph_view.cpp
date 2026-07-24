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
//  graph_view.cpp
//  app (JUCE)
//

#include "graph_view.hpp"

#include "graph_edits.hpp"
#include "tzpl_app_context.hpp"
#include "tzpl_client_interface.hpp"

namespace tzplapp {

// Box metrics (world units at zoom 1).
static constexpr float kTitleH = 22.f;
static constexpr float kPinRowH = 16.f;
static constexpr float kBoxPadX = 10.f;
static constexpr float kMinBoxW = 90.f;
static constexpr float kPinR = 4.f;
static constexpr int kToolbarH = 34;
static constexpr float kMinZoom = 0.25f;
static constexpr float kMaxZoom = 2.5f;

static juce::Font titleFont() {
    return juce::Font(juce::FontOptions(13.f, juce::Font::bold));
}
static juce::Font portFont() {
    return juce::Font(juce::FontOptions(11.f));
}

// Short port label: name, channel count when > 1, elem/rate glyph when
// not the common f32/audio pair (e.g. "trig ×1 i64e").
static juce::String portLabel(graph::PortVM const& p) {
    juce::String s(p.name);
    if (p.chans > 1) s << " ×" << p.chans;
    if (p.elem != tzpl_kF32 || p.rate != tzpl_audioRate) {
        char const* elem = p.elem == tzpl_kI32 ? "i32"
                         : p.elem == tzpl_kI64 ? "i64"
                         : p.elem == tzpl_kF64 ? "f64" : "f32";
        char rate = p.rate == tzpl_eventRate ? 'e'
                  : p.rate == tzpl_constRate ? 'c'
                  : p.rate == tzpl_initRate  ? 'i'
                  : p.rate == tzpl_resetRate ? 'r' : 'a';
        s << " " << elem << juce::String::charToString((juce::juce_wchar)rate);
    }
    return s;
}

// Above this many silos the segmented buttons give way to a dropdown.
static constexpr int kMaxSiloButtons = 8;

GraphView::GraphView(bridge::AppContext& appCtx)
    : appCtx_(appCtx), poller_(appCtx.engine)
{
    int silos = appCtx_.engine ? engine::numSilos(appCtx_.engine) : 1;
    if (silos <= kMaxSiloButtons) {
        siloLabel_.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(siloLabel_);
        for (int i = 0; i < silos; ++i) {
            auto b = std::make_unique<juce::TextButton>(juce::String(i));
            b->setClickingTogglesState(true);
            b->setRadioGroupId(1, juce::dontSendNotification);
            b->setToggleState(i == silo_, juce::dontSendNotification);
            b->setConnectedEdges(
                (i > 0 ? juce::Button::ConnectedOnLeft : 0)
                | (i < silos - 1 ? juce::Button::ConnectedOnRight : 0));
            b->onClick = [this, i] { selectSilo(i); };
            addAndMakeVisible(*b);
            siloButtons_.push_back(std::move(b));
        }
    } else {
        for (int i = 0; i < silos; ++i)
            siloBox_.addItem("Silo " + juce::String(i), i + 1);
        siloBox_.setSelectedId(1, juce::dontSendNotification);
        siloBox_.onChange = [this] { selectSilo(siloBox_.getSelectedId() - 1); };
        addAndMakeVisible(siloBox_);
    }

    relayoutButton_.onClick = [this] {
        layoutStore_.clearSilo(silo_);
        rebuildLayout();
        repaint();
    };
    addAndMakeVisible(relayoutButton_);

    fitButton_.onClick = [this] { zoomToFit(); repaint(); };
    addAndMakeVisible(fitButton_);

    setWantsKeyboardFocus(true); // Delete removes the selected wire/node
}

void GraphView::selectSilo(int s) {
    if (s == silo_ || s < 0) return;
    silo_ = s;
    didInitialFit_ = false;
    refreshNow();
}

void GraphView::refreshNow() {
    poller_.invalidate();
    pollNow();
}

void GraphView::visibilityChanged() {
    if (isVisible()) {
        refreshNow();
        startTimerHz(8);
    } else {
        stopTimer();
    }
}

void GraphView::timerCallback() {
    if (isShowing()) pollNow();
}

void GraphView::pollNow() {
    if (poller_.poll(silo_, vm_)) {
        rebuildLayout();
        repaint();
    }
}

void GraphView::rebuildLayout() {
    auto tf = titleFont();
    auto pf = portFont();
    for (auto& n : vm_.nodes) {
        juce::String title(n.defName);
        title << "  #" << juce::String((juce::int64)n.nodeID);
        float w = tf.getStringWidthFloat(title) + 2 * kBoxPadX;
        int rows = std::max((int)n.ins.size(), (int)n.outs.size());
        for (int r = 0; r < rows; ++r) {
            float rowW = 2 * kBoxPadX + 8.f;
            if (r < (int)n.ins.size())
                rowW += pf.getStringWidthFloat(portLabel(n.ins[r]));
            if (r < (int)n.outs.size())
                rowW += pf.getStringWidthFloat(portLabel(n.outs[r]));
            w = std::max(w, rowW);
        }
        n.w = std::max(kMinBoxW, w);
        n.h = kTitleH + rows * kPinRowH + 6.f;
    }
    graph::autoLayout(vm_, layoutStore_);
    if (!didInitialFit_) {
        zoomToFit();
        didInitialFit_ = true;
    }
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

juce::Rectangle<int> GraphView::canvasArea() const {
    return getLocalBounds().withTrimmedTop(kToolbarH);
}

juce::AffineTransform GraphView::worldTransform() const {
    return juce::AffineTransform::scale(zoom_).translated(pan_.x, pan_.y);
}

juce::Point<float> GraphView::toWorld(juce::Point<float> screenPt) const {
    return screenPt.transformedBy(worldTransform().inverted());
}

juce::Point<float> GraphView::pinCentre(graph::NodeVM const& n, bool input,
                                        int port) const {
    float y = n.y + kTitleH + (port + 0.5f) * kPinRowH;
    return {input ? n.x : n.x + n.w, y};
}

juce::Path GraphView::edgePath(graph::EdgeVM const& e) const {
    auto p1 = pinCentre(vm_.nodes[e.srcNode], false, e.srcPort);
    auto p2 = pinCentre(vm_.nodes[e.dstNode], true, e.dstPort);
    float dx = std::max(30.f, std::abs(p2.x - p1.x) * 0.5f);
    juce::Path path;
    path.startNewSubPath(p1);
    path.cubicTo(p1.x + dx, p1.y, p2.x - dx, p2.y, p2.x, p2.y);
    return path;
}

int GraphView::hitNode(juce::Point<float> worldPt) const {
    for (int i = (int)vm_.nodes.size() - 1; i >= 0; --i) {
        auto const& n = vm_.nodes[i];
        if (juce::Rectangle<float>(n.x, n.y, n.w, n.h).contains(worldPt))
            return i;
    }
    return -1;
}

GraphView::PinRef GraphView::hitPin(juce::Point<float> worldPt) const {
    float const tol = 9.f / zoom_;
    PinRef best;
    float bestDist = tol;
    for (int i = 0; i < (int)vm_.nodes.size(); ++i) {
        auto const& n = vm_.nodes[i];
        for (int p = 0; p < (int)n.ins.size(); ++p) {
            float d = pinCentre(n, true, p).getDistanceFrom(worldPt);
            if (d < bestDist) { bestDist = d; best = {i, p, true}; }
        }
        for (int p = 0; p < (int)n.outs.size(); ++p) {
            float d = pinCentre(n, false, p).getDistanceFrom(worldPt);
            if (d < bestDist) { bestDist = d; best = {i, p, false}; }
        }
    }
    return best;
}

int GraphView::hitEdge(juce::Point<float> worldPt) const {
    float const tol = 6.f / zoom_;
    for (int i = 0; i < (int)vm_.edges.size(); ++i) {
        juce::Path p = edgePath(vm_.edges[i]);
        juce::PathFlatteningIterator it(p, {}, 8.f);
        while (it.next()) {
            juce::Line<float> seg(it.x1, it.y1, it.x2, it.y2);
            juce::Point<float> foot;
            if (seg.getDistanceFromPoint(worldPt, foot) <= tol) return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void GraphView::paint(juce::Graphics& g) {
    auto& lf = getLookAndFeel();
    auto bg      = lf.findColour(juce::ResizableWindow::backgroundColourId);
    auto text    = lf.findColour(juce::TextEditor::textColourId);
    auto outline = lf.findColour(juce::TextEditor::outlineColourId);
    auto accent  = lf.findColour(juce::TextEditor::focusedOutlineColourId);

    g.fillAll(bg.darker(0.15f));

    // Toolbar strip.
    g.setColour(bg);
    g.fillRect(getLocalBounds().removeFromTop(kToolbarH));
    g.setColour(outline.withAlpha(0.4f));
    g.drawHorizontalLine(kToolbarH - 1, 0.f, (float)getWidth());

    auto canvas = canvasArea();
    g.saveState();
    g.reduceClipRegion(canvas);
    g.addTransform(worldTransform());

    auto boxBg = bg.brighter(0.08f);
    auto wire  = text.withAlpha(0.8f);

    // Wires under the nodes. Stroke width is divided by the zoom so wires
    // keep a constant on-screen width; at >= 2 screen px with high alpha,
    // anti-aliasing no longer makes near-horizontal runs look thicker than
    // steep ones (thin translucent strokes weigh differently by slope).
    juce::PathStrokeType wireStroke(2.0f / zoom_,
                                    juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded);
    juce::PathStrokeType selStroke(3.0f / zoom_,
                                   juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded);
    for (int i = 0; i < (int)vm_.edges.size(); ++i) {
        bool sel = i == selectedEdge_;
        g.setColour(sel ? accent : wire);
        g.strokePath(edgePath(vm_.edges[i]), sel ? selStroke : wireStroke);
    }

    auto tf = titleFont();
    auto pf = portFont();

    for (auto const& n : vm_.nodes) {
        juce::Rectangle<float> box(n.x, n.y, n.w, n.h);
        bool builtin = n.nodeID == 0 || n.nodeID == 1;
        bool sel = n.nodeID == selectedNode_;

        auto fill = builtin ? boxBg.interpolatedWith(accent, 0.12f) : boxBg;
        if (n.defMissing) fill = fill.withAlpha(0.6f);
        g.setColour(fill);
        g.fillRoundedRectangle(box, 5.f);
        g.setColour(sel ? accent : outline.withAlpha(n.defMissing ? 0.3f : 0.6f));
        g.drawRoundedRectangle(box, 5.f, sel ? 2.f : 1.f);

        // Title bar: def name left, #nodeID right.
        auto titleArea = box.withHeight(kTitleH).reduced(kBoxPadX, 0);
        auto tcol = n.defMissing ? text.withAlpha(0.5f) : text;
        g.setColour(tcol);
        g.setFont(tf);
        g.drawText(juce::String(n.defName), titleArea,
                   juce::Justification::centredLeft, true);
        g.setColour(tcol.withAlpha(0.6f));
        g.drawText("#" + juce::String((juce::int64)n.nodeID), titleArea,
                   juce::Justification::centredRight, true);
        g.setColour(outline.withAlpha(0.3f));
        g.drawHorizontalLine((int)(n.y + kTitleH), n.x + 2, n.x + n.w - 2);

        // Pins + labels.
        g.setFont(pf);
        for (int p = 0; p < (int)n.ins.size(); ++p) {
            auto c = pinCentre(n, true, p);
            g.setColour(accent.withAlpha(0.9f));
            g.fillEllipse(c.x - kPinR, c.y - kPinR, 2 * kPinR, 2 * kPinR);
            g.setColour(tcol.withAlpha(0.85f));
            g.drawText(portLabel(n.ins[p]),
                       juce::Rectangle<float>(n.x + kBoxPadX * 0.7f,
                                              c.y - kPinRowH / 2,
                                              n.w * 0.6f, kPinRowH),
                       juce::Justification::centredLeft, true);
        }
        for (int p = 0; p < (int)n.outs.size(); ++p) {
            auto c = pinCentre(n, false, p);
            g.setColour(accent.withAlpha(0.9f));
            g.fillEllipse(c.x - kPinR, c.y - kPinR, 2 * kPinR, 2 * kPinR);
            g.setColour(tcol.withAlpha(0.85f));
            g.drawText(portLabel(n.outs[p]),
                       juce::Rectangle<float>(n.x + n.w - kBoxPadX * 0.7f
                                                  - n.w * 0.6f,
                                              c.y - kPinRowH / 2,
                                              n.w * 0.6f, kPinRowH),
                       juce::Justification::centredRight, true);
        }
    }

    // Wire drag: rubber band + rings on every compatible drop target.
    if (wireFrom_.valid()) {
        PinRef probe; // candidate in normalized (outlet -> inlet) order
        for (int i = 0; i < (int)vm_.nodes.size(); ++i) {
            auto const& n = vm_.nodes[i];
            int numPins = wireFrom_.input ? (int)n.outs.size()
                                          : (int)n.ins.size();
            for (int p = 0; p < numPins; ++p) {
                probe = {i, p, !wireFrom_.input};
                bool ok = wireFrom_.input ? dropIsCompatible(probe, wireFrom_)
                                          : dropIsCompatible(wireFrom_, probe);
                if (!ok) continue;
                auto c = pinCentre(n, probe.input, p);
                bool hovered = probe == wireHover_;
                g.setColour(accent.withAlpha(hovered ? 1.f : 0.5f));
                float r = kPinR + (hovered ? 4.f : 2.5f);
                g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r,
                              (hovered ? 2.f : 1.2f) / zoom_);
            }
        }
        auto p1 = pinCentre(vm_.nodes[wireFrom_.node], wireFrom_.input,
                            wireFrom_.port);
        auto p2 = wireHover_.valid()
            ? pinCentre(vm_.nodes[wireHover_.node], wireHover_.input,
                        wireHover_.port)
            : wireMouseWorld_;
        float dx = std::max(30.f, std::abs(p2.x - p1.x) * 0.5f);
        float dir = wireFrom_.input ? -1.f : 1.f;
        juce::Path band;
        band.startNewSubPath(p1);
        band.cubicTo(p1.x + dir * dx, p1.y, p2.x - dir * dx, p2.y, p2.x, p2.y);
        g.setColour(accent);
        g.strokePath(band, selStroke);
    }

    g.restoreState();

    // Empty-state hint: only the built-in Audio In / Audio Out present.
    if (vm_.nodes.size() <= 2 && vm_.edges.empty()) {
        g.setColour(text.withAlpha(0.45f));
        g.setFont(juce::Font(juce::FontOptions(14.f)));
        g.drawText("No nodes yet — evaluate some code that plays audio",
                   canvas, juce::Justification::centred, true);
    }
}

void GraphView::resized() {
    auto bar = getLocalBounds().removeFromTop(kToolbarH).reduced(6, 5);
    if (!siloButtons_.empty()) {
        siloLabel_.setBounds(bar.removeFromLeft(34));
        bar.removeFromLeft(4);
        for (auto& b : siloButtons_)
            b->setBounds(bar.removeFromLeft(28));
    } else {
        siloBox_.setBounds(bar.removeFromLeft(110));
    }
    bar.removeFromLeft(8);
    relayoutButton_.setBounds(bar.removeFromLeft(84));
    bar.removeFromLeft(6);
    fitButton_.setBounds(bar.removeFromLeft(50));
}

// ---------------------------------------------------------------------------
// Interaction: drag nodes, pan background, wheel/pinch zoom, click select.
// ---------------------------------------------------------------------------

void GraphView::mouseDown(juce::MouseEvent const& e) {
    if (e.position.y < kToolbarH) return;
    grabKeyboardFocus();
    auto w = toWorld(e.position);

    if (e.mods.isPopupMenu()) {
        int ni = hitNode(w);
        if (ni >= 0) showNodeMenu(ni);
        else showBackgroundMenu(w);
        return;
    }

    PinRef pin = hitPin(w);
    if (pin.valid()) {
        wireFrom_ = pin;
        wireMouseWorld_ = w;
        wireHover_ = {};
        selectedNode_ = -1;
        selectedEdge_ = -1;
        repaint();
        return;
    }

    int ni = hitNode(w);
    if (ni >= 0) {
        selectedNode_ = vm_.nodes[ni].nodeID;
        selectedEdge_ = -1;
        draggingNode_ = ni;
        dragGrabOffset_ = {w.x - vm_.nodes[ni].x, w.y - vm_.nodes[ni].y};
        repaint();
        return;
    }
    int ei = hitEdge(w);
    if (ei >= 0) {
        selectedEdge_ = ei;
        selectedNode_ = -1;
        repaint();
        return;
    }
    selectedNode_ = -1;
    selectedEdge_ = -1;
    panning_ = true;
    panAnchor_ = pan_;
    panMouseStart_ = e.position;
    repaint();
}

void GraphView::mouseDrag(juce::MouseEvent const& e) {
    if (wireFrom_.valid()) {
        wireMouseWorld_ = toWorld(e.position);
        PinRef over = hitPin(wireMouseWorld_);
        wireHover_ = over.valid() && over.input != wireFrom_.input
                         && dropIsCompatible(wireFrom_.input ? over : wireFrom_,
                                             wireFrom_.input ? wireFrom_ : over)
                     ? over : PinRef{};
        repaint();
    } else if (draggingNode_ >= 0 && draggingNode_ < (int)vm_.nodes.size()) {
        auto w = toWorld(e.position);
        auto& n = vm_.nodes[draggingNode_];
        n.x = w.x - dragGrabOffset_.x;
        n.y = w.y - dragGrabOffset_.y;
        n.placedByUser = true;
        layoutStore_.set(silo_, n.nodeID, n.x, n.y);
        repaint();
    } else if (panning_) {
        pan_ = panAnchor_ + (e.position - panMouseStart_);
        repaint();
    }
}

void GraphView::mouseUp(juce::MouseEvent const& e) {
    if (wireFrom_.valid()) finishWireDrag(toWorld(e.position));
    draggingNode_ = -1;
    panning_ = false;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

bool GraphView::dropIsCompatible(PinRef srcPin, PinRef dstPin) const {
    if (!srcPin.valid() || !dstPin.valid()) return false;
    if (srcPin.input || !dstPin.input) return false;
    auto const& sn = vm_.nodes[srcPin.node];
    auto const& dn = vm_.nodes[dstPin.node];
    // Unknown defs: let the engine decide at submit.
    if (sn.defMissing || dn.defMissing) return true;
    return graph::canConnect(sn.outs[srcPin.port], dn.ins[dstPin.port],
                             dn.nodeID);
}

void GraphView::afterEdit(int err, juce::String const& what) {
    if (err != 0) {
        logLine("graph: " + what + " failed: " + graph::errText(err), true);
    } else {
        // No optimistic UI: the accepted bundle's commit bumps the graph
        // generation; re-snapshot now (inline when audio is stopped, next
        // poll tick otherwise).
        poller_.invalidate();
        pollNow();
    }
}

void GraphView::finishWireDrag(juce::Point<float> worldPt) {
    PinRef from = wireFrom_;
    wireFrom_ = {};
    wireHover_ = {};
    repaint();

    PinRef over = hitPin(worldPt);
    if (!over.valid() || over.input == from.input) return; // dropped on air
    PinRef src = from.input ? over : from;
    PinRef dst = from.input ? from : over;

    auto const& sn = vm_.nodes[src.node];
    auto const& dn = vm_.nodes[dst.node];
    int err = graph::connectNodes(appCtx_.engine, silo_,
                                  sn.nodeID, src.port, dn.nodeID, dst.port);
    afterEdit(err, juce::String("connect ") + juce::String((juce::int64)sn.nodeID)
                       + "." + juce::String(src.port) + " -> "
                       + juce::String((juce::int64)dn.nodeID) + "."
                       + juce::String(dst.port));
}

bool GraphView::keyPressed(juce::KeyPress const& key) {
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        if (selectedEdge_ >= 0 && selectedEdge_ < (int)vm_.edges.size()) {
            auto const& ed = vm_.edges[selectedEdge_];
            auto const& sn = vm_.nodes[ed.srcNode];
            auto const& dn = vm_.nodes[ed.dstNode];
            selectedEdge_ = -1;
            afterEdit(graph::disconnectWire(appCtx_.engine, silo_,
                                            sn.nodeID, ed.srcPort,
                                            dn.nodeID, ed.dstPort),
                      "disconnect");
            return true;
        }
        if (selectedNode_ >= 2) { // never delete Audio Out / Audio In
            long long id = selectedNode_;
            selectedNode_ = -1;
            afterEdit(graph::freeGraphNode(appCtx_.engine, silo_, id),
                      "free node " + juce::String((juce::int64)id));
            return true;
        }
    }
    if (key == juce::KeyPress::escapeKey
        && (selectedNode_ >= 0 || selectedEdge_ >= 0)) {
        selectedNode_ = -1;
        selectedEdge_ = -1;
        repaint();
        return true;
    }
    return false;
}

void GraphView::showBackgroundMenu(juce::Point<float> worldPt) {
    if (!appCtx_.engine) return;
    std::vector<engine::DefDesc> defs;
    engine::listDefDescs(appCtx_.engine, defs);

    juce::PopupMenu m;
    m.addSectionHeader("New Node");
    std::vector<std::string> names;
    for (auto const& d : defs) {
        if (d.name == "Audio Out" || d.name == "Audio In") continue;
        names.push_back(d.name);
        m.addItem((int)names.size(), juce::String(d.name));
    }
    if (names.empty()) m.addItem(-1, "(no synthdefs loaded)", false);

    juce::Component::SafePointer<GraphView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nullptr),
        [safe, names = std::move(names), worldPt](int result) {
            if (safe == nullptr || result <= 0) return;
            auto* self = safe.getComponent();
            long long id = graph::nextFreeNodeID(self->vm_);
            // Drop the new node where the user clicked.
            self->layoutStore_.set(self->silo_, id, worldPt.x, worldPt.y);
            self->afterEdit(graph::createNode(self->appCtx_.engine, self->silo_,
                                              names[(size_t)result - 1], id),
                            "new node " + juce::String(names[(size_t)result - 1]));
        });
}

void GraphView::showNodeMenu(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= (int)vm_.nodes.size()) return;
    long long id = vm_.nodes[nodeIndex].nodeID;
    bool builtin = id == 0 || id == 1;

    juce::PopupMenu m;
    m.addSectionHeader(juce::String(vm_.nodes[nodeIndex].defName)
                       + "  #" + juce::String((juce::int64)id));
    m.addItem(1, "Disconnect All");
    m.addItem(2, "Free Node", !builtin);

    juce::Component::SafePointer<GraphView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nullptr),
        [safe, id](int result) {
            if (safe == nullptr || result <= 0) return;
            auto* self = safe.getComponent();
            if (result == 1)
                self->afterEdit(graph::disconnectAllWires(self->appCtx_.engine,
                                                          self->silo_,
                                                          self->vm_, id),
                                "disconnect node " + juce::String((juce::int64)id));
            else if (result == 2)
                self->afterEdit(graph::freeGraphNode(self->appCtx_.engine,
                                                     self->silo_, id),
                                "free node " + juce::String((juce::int64)id));
        });
}

void GraphView::mouseWheelMove(juce::MouseEvent const& e,
                               juce::MouseWheelDetails const& wheel) {
    if (e.mods.isCommandDown()) {
        zoomAbout(e.position, 1.f + wheel.deltaY * 1.5f);
    } else if (e.mods.isShiftDown()) {
        pan_.x += wheel.deltaY * 160.f;
    } else {
        pan_.x += wheel.deltaX * 160.f;
        pan_.y += wheel.deltaY * 160.f;
    }
    repaint();
}

void GraphView::mouseMagnify(juce::MouseEvent const& e, float scaleFactor) {
    zoomAbout(e.position, scaleFactor);
    repaint();
}

void GraphView::zoomAbout(juce::Point<float> screenPt, float factor) {
    float newZoom = juce::jlimit(kMinZoom, kMaxZoom, zoom_ * factor);
    if (newZoom == zoom_) return;
    // Keep the world point under the cursor stationary on screen.
    auto w = toWorld(screenPt);
    zoom_ = newZoom;
    pan_ = {screenPt.x - w.x * zoom_, screenPt.y - w.y * zoom_};
}

void GraphView::zoomToFit() {
    if (vm_.nodes.empty()) return;
    auto canvas = canvasArea().toFloat().reduced(24.f);
    if (canvas.isEmpty()) return;

    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto const& n : vm_.nodes) {
        x0 = std::min(x0, n.x);
        y0 = std::min(y0, n.y);
        x1 = std::max(x1, n.x + n.w);
        y1 = std::max(y1, n.y + n.h);
    }
    float w = std::max(1.f, x1 - x0), h = std::max(1.f, y1 - y0);
    zoom_ = juce::jlimit(kMinZoom, 1.f,
                         std::min(canvas.getWidth() / w, canvas.getHeight() / h));
    pan_ = {canvas.getX() + (canvas.getWidth() - w * zoom_) * 0.5f - x0 * zoom_,
            (float)kToolbarH + 24.f
                + (canvas.getHeight() - h * zoom_) * 0.5f - y0 * zoom_};
}

}
