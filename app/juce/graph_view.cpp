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

// Node context menu: per-outlet meter items start here, so they can't
// collide with the fixed items (1..4).
static constexpr int kMeterMenuBase = 100;

// Level meters: horizontal bars the width of the node, stacked under the box,
// one per metered outlet -- the same orientation as the status bar's master
// meter. They hang OUTSIDE the box because inlet and outlet rows share a row
// (pinCentre), so there is no free strip under an outlet, and because growing
// the box would re-layout the whole graph every time a meter is toggled.
static constexpr float kMeterTop = 3.f;    // box bottom -> first bar
static constexpr float kMeterBarH = 10.f;
static constexpr float kMeterBarGap = 1.f;

// Amplitude -> 0..1 bar fill over a -60..0 dB range. Matches the widget
// meters (app/src/widget_draw.cpp, widget_component.cpp) so a node reads the
// same in the graph as in its control panel.
static float db01(float lin) {
    if (lin <= 1e-6f) return 0.f;
    float db = 20.0f * std::log10(lin);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

static juce::String dbText(float lin) {
    if (lin <= 1e-6f) return "-inf";
    return juce::String(20.0f * std::log10(lin), 1);
}

static juce::Font meterFont() {
    return juce::Font(juce::FontOptions(8.f));
}
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

GraphView::~GraphView() {
    // Taps are a scarce RT resource and nothing else owns them.
    meters_.clear(appCtx_.engine);
}

void GraphView::selectSilo(int s) {
    if (s == silo_ || s < 0) return;
    // Meters belong to the silo they were opened on; switching away drops
    // them rather than leaving invisible taps running.
    meters_.clearSilo(appCtx_.engine, silo_);
    syncMeterTimer();
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
        startTimer(kTopologyTimer, 1000 / 8);
        syncMeterTimer();
    } else {
        // Keep the taps: hiding the view is cheap to undo and the user
        // expects their meters back. Only the polling stops.
        stopTimer(kTopologyTimer);
        stopTimer(kMeterTimer);
    }
}

void GraphView::timerCallback(int timerID) {
    if (!isShowing()) return;
    if (timerID == kTopologyTimer) pollNow();
    else pollMeters();
}

void GraphView::pollNow() {
    if (poller_.poll(silo_, vm_)) {
        // A freed node's tap keeps its registry slot (reading silence) until
        // someone untaps it, so every topology change has to sweep.
        meters_.prune(appCtx_.engine, vm_);
        syncMeterTimer();
        rebuildLayout();
        repaint();
    }
}

void GraphView::pollMeters() {
    if (meters_.count() == 0) return;
    // ~1.5s fall at 25 Hz.
    meters_.poll(appCtx_.engine, 0.9f);
    // Repaint only the metered nodes' gutters: a full repaint at 25 Hz would
    // re-stroke every bezier wire in the graph.
    for (auto const& n : vm_.nodes) {
        if (meters_.nodeEnabled(silo_, n.nodeID)) repaint(meterRepaintArea(n));
    }
}

void GraphView::syncMeterTimer() {
    bool want = isShowing() && meters_.count() > 0;
    if (want && !isTimerRunning(kMeterTimer)) startTimer(kMeterTimer, 1000 / 25);
    else if (!want && isTimerRunning(kMeterTimer)) stopTimer(kMeterTimer);
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

        // Level meters: a skirt of node-wide horizontal bars below the box,
        // in outlet order. Labelled with the outlet name and a peak-hold dB
        // readout, so a bar can be read as a number and not just a colour.
        {
            float my = n.y + n.h + kMeterTop;
            bool labels = kMeterBarH * zoom_ >= 9.f;
            for (int p = 0; p < (int)n.outs.size(); ++p) {
                auto const* m = meters_.find({silo_, n.nodeID, p});
                if (!m) continue;
                juce::Rectangle<float> bar(n.x, my, n.w, kMeterBarH);
                my += kMeterBarH + kMeterBarGap;

                g.setColour(juce::Colours::black.withAlpha(0.65f));
                g.fillRect(bar);

                // Scale ticks every 12 dB, so the bar reads as a scale.
                g.setColour(juce::Colours::white.withAlpha(0.18f));
                for (int db = -48; db < 0; db += 12) {
                    float tx = bar.getX() + (float)(db + 60) / 60.f * bar.getWidth();
                    g.fillRect(tx, bar.getY(), 1.f / zoom_, bar.getHeight());
                }

                g.setColour(m->peak >= 1.0f ? juce::Colours::red
                                            : juce::Colour(0xff4caf50));
                g.fillRect(bar.withWidth(db01(m->rms) * bar.getWidth()));

                float hx = bar.getX() + db01(m->peakHold) * bar.getWidth();
                g.setColour(juce::Colours::orange);
                g.fillRect(hx - 0.75f / zoom_, bar.getY(), 1.5f / zoom_,
                           bar.getHeight());

                g.setColour(outline.withAlpha(0.5f));
                g.drawRect(bar, 1.f / zoom_);

                if (!labels) continue;
                g.setFont(meterFont());
                auto inner = bar.reduced(3.f, 0.f);
                g.setColour(juce::Colours::white.withAlpha(0.9f));
                g.drawText(juce::String(n.outs[(size_t)p].name), inner,
                           juce::Justification::centredLeft, false);
                g.drawText(dbText(m->peakHold), inner,
                           juce::Justification::centredRight, false);
            }
            g.setFont(pf);
        }

        // A dot in the title bar keeps "this node is metered" legible when
        // zoomed too far out to see the bars.
        if (meters_.nodeEnabled(silo_, n.nodeID)) {
            g.setColour(juce::Colour(0xff4caf50));
            g.fillEllipse(n.x + n.w - kBoxPadX - 4.f, n.y + kTitleH * 0.5f - 2.f,
                          4.f, 4.f);
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

juce::Rectangle<int> GraphView::meterRepaintArea(graph::NodeVM const& n) const {
    int bars = 0;
    for (int p = 0; p < (int)n.outs.size(); ++p)
        if (meters_.find({silo_, n.nodeID, p})) ++bars;
    float skirt = bars ? kMeterTop + bars * (kMeterBarH + kMeterBarGap) : 0.f;
    juce::Rectangle<float> r(n.x, n.y, n.w, n.h + skirt);
    return r.transformedBy(worldTransform()).getSmallestIntegerContainer()
            .expanded(2);
}

void GraphView::toggleNodeMeters(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= (int)vm_.nodes.size()) return;
    long long id = vm_.nodes[(size_t)nodeIndex].nodeID;
    if (meters_.nodeEnabled(silo_, id)) {
        meters_.disableNode(appCtx_.engine, silo_, id);
    } else {
        int err = meters_.enableNode(appCtx_.engine, silo_, vm_, id);
        if (err != tzpl_errNone) {
            logLine("meter: " + juce::String(graph::errText(err))
                        + " (each silo holds a limited number of taps)", true);
        }
    }
    syncMeterTimer();
    repaint();
}

void GraphView::toggleOutletMeter(int nodeIndex, int outlet) {
    if (nodeIndex < 0 || nodeIndex >= (int)vm_.nodes.size()) return;
    graph::MeterKey k{silo_, vm_.nodes[(size_t)nodeIndex].nodeID, outlet};
    if (meters_.enabled(k)) {
        meters_.disable(appCtx_.engine, k);
    } else {
        int err = meters_.enable(appCtx_.engine, k);
        if (err != tzpl_errNone) {
            logLine("meter: " + juce::String(graph::errText(err))
                        + " (each silo holds a limited number of taps)", true);
        }
    }
    syncMeterTimer();
    repaint();
}

void GraphView::showNodeMenu(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= (int)vm_.nodes.size()) return;
    long long id = vm_.nodes[nodeIndex].nodeID;
    bool builtin = id == 0 || id == 1;

    auto const& node = vm_.nodes[nodeIndex];
    juce::PopupMenu m;
    m.addSectionHeader(juce::String(node.defName)
                       + "  #" + juce::String((juce::int64)id));
    m.addItem(3, "Controls...");

    // Metering is opt-in per node. Only f32 outlets can be tapped, so the
    // rest are greyed rather than silently doing nothing.
    bool anyF32 = false;
    for (auto const& o : node.outs) if (o.elem == tzpl_kF32) anyF32 = true;
    m.addSeparator();
    if (node.outs.size() <= 1) {
        m.addItem(4, "Meter Node", anyF32, meters_.nodeEnabled(silo_, id));
    } else {
        juce::PopupMenu sub;
        sub.addItem(4, "All Outlets", anyF32, meters_.nodeEnabled(silo_, id));
        sub.addSeparator();
        for (int o = 0; o < (int)node.outs.size(); ++o) {
            sub.addItem(kMeterMenuBase + o, portLabel(node.outs[(size_t)o]),
                        node.outs[(size_t)o].elem == tzpl_kF32,
                        meters_.enabled({silo_, id, o}));
        }
        m.addSubMenu("Meter", sub);
    }

    m.addSeparator();
    m.addItem(1, "Disconnect All");
    m.addItem(2, "Free Node", !builtin);

    juce::Component::SafePointer<GraphView> safe(this);
    std::string defName = vm_.nodes[nodeIndex].defName;
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(nullptr),
        [safe, id, defName, nodeIndex](int result) {
            if (safe == nullptr || result <= 0) return;
            auto* self = safe.getComponent();
            if (result == 4) {
                self->toggleNodeMeters(nodeIndex);
                return;
            }
            if (result >= kMeterMenuBase) {
                self->toggleOutletMeter(nodeIndex, result - kMeterMenuBase);
                return;
            }
            if (result == 1)
                self->afterEdit(graph::disconnectAllWires(self->appCtx_.engine,
                                                          self->silo_,
                                                          self->vm_, id),
                                "disconnect node " + juce::String((juce::int64)id));
            else if (result == 2)
                self->afterEdit(graph::freeGraphNode(self->appCtx_.engine,
                                                     self->silo_, id),
                                "free node " + juce::String((juce::int64)id));
            else if (result == 3 && self->onOpenNodeControls)
                self->onOpenNodeControls(id, defName, self->silo_);
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

void GraphView::mouseDoubleClick(juce::MouseEvent const& e) {
    if (e.position.y < kToolbarH) return;
    int ni = hitNode(toWorld(e.position));
    if (ni < 0 || !onOpenNodeControls) return;
    auto const& n = vm_.nodes[ni];
    onOpenNodeControls(n.nodeID, n.defName, silo_);
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
