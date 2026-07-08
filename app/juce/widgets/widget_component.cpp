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
//  widget_component.cpp
//  app (JUCE)
//

#include "widget_component.hpp"
#include "controls_dispatch.hpp"
#include <algorithm>
#include <cmath>

namespace tzplapp {

using bridge::UIWidget;
using bridge::UIWidgetKind;
using juce::Graphics;
using juce::String;

namespace {
String fmtValue(double v) {
    return String(v, v == std::floor(v) && std::abs(v) < 1e9 ? 0 : 4);
}
juce::Colour trackColour(juce::Component& c) {
    return c.findColour(juce::Slider::backgroundColourId,
                        /*inheritFromParent=*/true);
}
}

WidgetComponent::WidgetComponent(bridge::UIState& ui, std::uint64_t id,
                                 ControlsDispatcher& dispatcher)
    : ui_(ui), id_(id), dispatcher_(dispatcher)
{
    setWantsKeyboardFocus(false);
    refreshMeta();
}

void WidgetComponent::refreshMeta() {
    std::lock_guard<std::mutex> lock(ui_.mtx);
    if (auto* w = ui_.findById(id_)) {
        kind_ = w->kind;
        name_ = w->name;
    }
}

bool WidgetComponent::isTapBacked() const {
    return kind_ == UIWidgetKind::Meter || kind_ == UIWidgetKind::Scope;
}

juce::Point<int> WidgetComponent::preferredSize() const {
    switch (kind_) {
        case UIWidgetKind::Button:
        case UIWidgetKind::Toggle:
        case UIWidgetKind::Number:
        case UIWidgetKind::Slider:
        case UIWidgetKind::Range:
        case UIWidgetKind::Label:
        case UIWidgetKind::Meter:  return { 200, 28 };
        case UIWidgetKind::XY:     return { 160, 160 };
        case UIWidgetKind::Scope:
        case UIWidgetKind::Plot:
        case UIWidgetKind::Waveform: return { 260, 120 };
        case UIWidgetKind::MultiSlider: return { 260, 120 };
        case UIWidgetKind::Matrix: return { 200, 160 };
        case UIWidgetKind::PianoRoll: return { 320, 200 };
    }
    return { 200, 28 };
}

void WidgetComponent::markDirtyAndNotify(UIWidget& w, bool gestureEnd) {
    w.dirtyEngine = true;
    w.dirtyCallback = true;
    if (gestureEnd) { w.gestureActive = false; w.gestureEnded = true; }
    else            w.gestureActive = true;
    dispatcher_.ensureRunning();
    repaint();
}

void WidgetComponent::trackGeometry(float& minx, float& usable) const {
    constexpr float pad = 2.0f;
    float labelW = 0.0f; // name drawn under the widget instead of beside it
    minx = pad;
    usable = std::max((float)getWidth() - 2 * pad - labelW, 1.0f);
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void WidgetComponent::paint(Graphics& g) {
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;
    switch (w.kind) {
        case UIWidgetKind::Slider:      paintSlider(g, w); break;
        case UIWidgetKind::Range:       paintRange(g, w); break;
        case UIWidgetKind::Number:      paintNumber(g, w); break;
        case UIWidgetKind::Button:      paintButton(g, w); break;
        case UIWidgetKind::Toggle:      paintToggle(g, w); break;
        case UIWidgetKind::XY:          paintXY(g, w); break;
        case UIWidgetKind::Meter:       paintMeter(g, w); break;
        case UIWidgetKind::Scope:       paintScope(g, w); break;
        case UIWidgetKind::Plot:        paintPlot(g, w); break;
        case UIWidgetKind::Waveform:    paintWaveform(g, w); break;
        case UIWidgetKind::MultiSlider: paintMultiSlider(g, w); break;
        case UIWidgetKind::Matrix:      paintMatrix(g, w); break;
        case UIWidgetKind::PianoRoll:   paintPianoRoll(g, w); break;
        case UIWidgetKind::Label:       paintLabel(g, w); break;
    }
}

// Common helpers used by several paints.
static juce::Rectangle<float> bodyRect(juce::Component const& c, int labelH) {
    return juce::Rectangle<float>(0, 0, (float)c.getWidth(),
                                  (float)c.getHeight() - labelH);
}

void WidgetComponent::paintSlider(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(trackColour(*this).withAlpha(0.5f));
    g.fillRoundedRectangle(bb, 3.0f);
    float minx, usable;
    trackGeometry(minx, usable);
    float pos = (float)w.spec.unmap(w.values.empty() ? 0.0 : w.values[0]);
    g.setColour(juce::Colour(0xff5a9bd4));
    g.fillRoundedRectangle(minx, bb.getY() + 2, pos * usable,
                           bb.getHeight() - 4, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(12.0f);
    g.drawText(fmtValue(w.values.empty() ? 0.0 : w.values[0]), bb,
               juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintRange(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(trackColour(*this).withAlpha(0.5f));
    g.fillRoundedRectangle(bb, 3.0f);
    float minx, usable;
    trackGeometry(minx, usable);
    double lo = w.values.size() > 0 ? w.values[0] : w.spec.lo;
    double hi = w.values.size() > 1 ? w.values[1] : w.spec.hi;
    float plo = (float)w.spec.unmap(lo), phi = (float)w.spec.unmap(hi);
    g.setColour(juce::Colour(0xff5a9bd4));
    g.fillRoundedRectangle(minx + plo * usable, bb.getY() + 2,
                           (phi - plo) * usable, bb.getHeight() - 4, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(12.0f);
    g.drawText(fmtValue(lo) + " .. " + fmtValue(hi), bb,
               juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintNumber(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(trackColour(*this).withAlpha(0.4f));
    g.fillRoundedRectangle(bb, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(13.0f);
    g.drawText(fmtValue(w.values.empty() ? 0.0 : w.values[0]), bb,
               juce::Justification::centred);
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintButton(Graphics& g, UIWidget& w) {
    bool down = !w.values.empty() && w.values[0] != 0.0;
    g.setColour(down ? juce::Colour(0xff5a9bd4) : trackColour(*this));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText(name_, getLocalBounds(), juce::Justification::centred);
}

void WidgetComponent::paintToggle(Graphics& g, UIWidget& w) {
    bool on = !w.values.empty() && w.values[0] != 0.0;
    auto box = juce::Rectangle<float>(2, (getHeight() - 18) * 0.5f, 34, 18);
    g.setColour(on ? juce::Colour(0xff4caf50) : trackColour(*this));
    g.fillRoundedRectangle(box, 9.0f);
    g.setColour(juce::Colours::white);
    g.fillEllipse(on ? box.getRight() - 16 : box.getX() + 2,
                  box.getY() + 2, 14, 14);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(13.0f);
    g.drawText(name_, 42, 0, getWidth() - 42, getHeight(),
               juce::Justification::centredLeft);
}

void WidgetComponent::paintXY(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(trackColour(*this).withAlpha(0.4f));
    g.fillRect(bb);
    double x = w.values.size() > 0 ? w.values[0] : w.spec.init;
    double y = w.values.size() > 1 ? w.values[1] : w.spec2.init;
    float px = (float)w.spec.unmap(x) * bb.getWidth();
    float py = (1.0f - (float)w.spec2.unmap(y)) * bb.getHeight();
    g.setColour(juce::Colour(0x405a9bd4));
    g.drawLine(px, bb.getY(), px, bb.getBottom());
    g.drawLine(bb.getX(), py, bb.getRight(), py);
    g.setColour(juce::Colour(0xff5a9bd4));
    g.fillEllipse(px - 5, py - 5, 10, 10);
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintMeter(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRect(bb);
    double rms = w.values.size() > 0 ? w.values[0] : 0.0;
    double peak = w.values.size() > 1 ? w.values[1] : 0.0;
    auto db01 = [](double lin) {
        if (lin <= 1e-6) return 0.0;
        double db = 20.0 * std::log10(lin);
        return std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
    };
    g.setColour(juce::Colour(0xff4caf50));
    g.fillRect(bb.withWidth((float)(db01(rms) * bb.getWidth())));
    float px = (float)(db01(peak) * bb.getWidth());
    g.setColour(juce::Colours::orange);
    g.drawLine(px, bb.getY(), px, bb.getBottom(), 2.0f);
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintScope(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRect(bb);
    g.setColour(juce::Colour(0x40ffffff));
    g.drawLine(bb.getX(), bb.getCentreY(), bb.getRight(), bb.getCentreY());
    int chans = std::max(1, w.scopeChans);
    int frames = (int)w.scopeRing.size() / chans;
    if (frames > 1) {
        juce::Path p;
        int show = std::min(frames, (int)bb.getWidth());
        int start = frames - show;
        for (int i = 0; i < show; ++i) {
            float s = w.scopeRing[(size_t)(start + i) * chans]; // channel 0
            float x = bb.getX() + (float)i / (float)std::max(1, show - 1)
                          * bb.getWidth();
            float y = bb.getCentreY() - std::clamp(s, -1.0f, 1.0f)
                          * bb.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xff8fd0ff));
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintPlot(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRect(bb);
    if (w.plotData.size() > 1) {
        float mn = *std::min_element(w.plotData.begin(), w.plotData.end());
        float mx = *std::max_element(w.plotData.begin(), w.plotData.end());
        float rng = mx - mn;
        if (rng <= 0.0f) rng = 1.0f;
        juce::Path p;
        for (size_t i = 0; i < w.plotData.size(); ++i) {
            float x = bb.getX() + (float)i / (float)(w.plotData.size() - 1)
                          * bb.getWidth();
            float y = bb.getBottom() - (w.plotData[i] - mn) / rng * bb.getHeight();
            if (i == 0) p.startNewSubPath(x, y);
            else        p.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xff8fd0ff));
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintWaveform(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRect(bb);
    size_t n = std::min(w.waveMin.size(), w.waveMax.size());
    if (n > 0) {
        g.setColour(juce::Colour(0xff6fa0c0));
        for (size_t i = 0; i < n; ++i) {
            float x = bb.getX() + (float)i / (float)n * bb.getWidth();
            float y0 = bb.getCentreY() - w.waveMax[i] * bb.getHeight() * 0.48f;
            float y1 = bb.getCentreY() - w.waveMin[i] * bb.getHeight() * 0.48f;
            g.drawLine(x, y0, x, y1);
        }
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintMultiSlider(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(trackColour(*this).withAlpha(0.4f));
    g.fillRect(bb);
    int n = (int)w.values.size();
    if (n > 0) {
        float bw = bb.getWidth() / n;
        for (int i = 0; i < n; ++i) {
            float h = (float)w.spec.unmap(w.values[i]) * bb.getHeight();
            g.setColour(juce::Colour(0xff5a9bd4));
            g.fillRect(bb.getX() + i * bw + 1, bb.getBottom() - h,
                       bw - 2, h);
        }
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintMatrix(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    int rows = std::max(1, w.rows), cols = std::max(1, w.cols);
    float cw = bb.getWidth() / cols, ch = bb.getHeight() / rows;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            bool on = idx < (int)w.values.size() && w.values[idx] != 0.0;
            g.setColour(on ? juce::Colour(0xff5a9bd4)
                           : trackColour(*this).withAlpha(0.4f));
            g.fillRect(bb.getX() + c * cw + 1, bb.getY() + r * ch + 1,
                       cw - 2, ch - 2);
        }
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintPianoRoll(Graphics& g, UIWidget& w) {
    auto bb = bodyRect(*this, kLabelH);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRect(bb);
    int rows = std::max(1, w.rollRows);
    float rh = bb.getHeight() / rows;
    // Horizontal lane lines.
    g.setColour(juce::Colour(0x18ffffff));
    for (int r = 0; r <= rows; ++r)
        g.drawHorizontalLine((int)(bb.getY() + r * rh), bb.getX(), bb.getRight());
    // Notes: (pitch, startBeat, durBeats) triplets.
    float beats = std::max(0.001f, w.rollBeats);
    g.setColour(juce::Colour(0xff5a9bd4));
    for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
        float pitch = w.noteData[i];
        float start = w.noteData[i + 1];
        float dur = w.noteData[i + 2];
        int row = (int)std::lround(pitch) - w.rollLowPitch;
        if (row < 0 || row >= rows) continue;
        float x = bb.getX() + start / beats * bb.getWidth();
        float wpx = dur / beats * bb.getWidth();
        float y = bb.getBottom() - (row + 1) * rh;
        g.fillRect(x, y + 1, std::max(2.0f, wpx), rh - 2);
    }
    g.setColour(juce::Colours::grey);
    g.drawText(name_, 0, getHeight() - kLabelH, getWidth(), kLabelH,
               juce::Justification::centredLeft);
}

void WidgetComponent::paintLabel(Graphics& g, UIWidget& w) {
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(13.0f);
    g.drawText(String(w.labelText.empty() ? w.name : w.labelText),
               getLocalBounds(), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void WidgetComponent::mouseDown(juce::MouseEvent const& e) {
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;
    dragging_ = true;

    float minx, usable;
    trackGeometry(minx, usable);
    auto bb = bodyRect(*this, kLabelH);

    switch (w.kind) {
        case UIWidgetKind::Slider: {
            if (w.values.empty()) w.values.resize(1);
            dragAnchor_ = { (float)w.spec.unmap(w.values[0]), (float)e.x, 0 };
            float pos = std::clamp((e.x - minx) / usable, 0.0f, 1.0f);
            w.values[0] = w.spec.map(pos);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::Range: {
            if (w.values.size() < 2) w.values.resize(2);
            float plo = (float)w.spec.unmap(w.values[0]);
            float phi = (float)w.spec.unmap(w.values[1]);
            float pos = std::clamp((e.x - minx) / usable, 0.0f, 1.0f);
            ui_gesture::adjustRangeEnd(pos, plo, phi);
            w.values[0] = w.spec.map(plo);
            w.values[1] = w.spec.map(phi);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::Number:
            if (w.values.empty()) w.values.resize(1);
            dragAnchor_ = { 0.0f, (float)e.y, 0 };
            break;
        case UIWidgetKind::Button:
            if (w.values.empty()) w.values.resize(1);
            w.values[0] = 1.0;
            markDirtyAndNotify(w, false);
            break;
        case UIWidgetKind::Toggle:
            if (w.values.empty()) w.values.resize(1);
            w.values[0] = w.values[0] != 0.0 ? 0.0 : 1.0;
            markDirtyAndNotify(w, true);
            break;
        case UIWidgetKind::XY: {
            if (w.values.size() < 2) w.values.resize(2);
            float px = std::clamp((float)e.x / bb.getWidth(), 0.0f, 1.0f);
            float py = std::clamp(1.0f - (float)e.y / bb.getHeight(), 0.0f, 1.0f);
            w.values[0] = w.spec.map(px);
            w.values[1] = w.spec2.map(py);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::MultiSlider: {
            int n = (int)w.values.size();
            if (n > 0) {
                int i = std::clamp((int)(e.x / (bb.getWidth() / n)), 0, n - 1);
                float pos = std::clamp(1.0f - (float)e.y / bb.getHeight(),
                                       0.0f, 1.0f);
                w.values[i] = w.spec.map(pos);
                markDirtyAndNotify(w, false);
            }
        } break;
        case UIWidgetKind::Matrix: {
            int idx = ui_gesture::matrixCellHit(
                (float)e.x, (float)e.y, bb.getWidth(), bb.getHeight(),
                std::max(1, w.rows), std::max(1, w.cols));
            if (idx >= 0 && idx < (int)w.values.size()) {
                matrixPaintValue_ = w.values[idx] != 0.0 ? 0 : 1;
                w.values[idx] = matrixPaintValue_;
                markDirtyAndNotify(w, false);
            }
        } break;
        default: break;
    }
}

void WidgetComponent::mouseDrag(juce::MouseEvent const& e) {
    if (!dragging_) return;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;

    float minx, usable;
    trackGeometry(minx, usable);
    auto bb = bodyRect(*this, kLabelH);
    // Cmd (or Ctrl) = fine drag; Shift = finer.
    int fine = e.mods.isCommandDown() ? 1 : (e.mods.isShiftDown() ? 2 : 0);

    switch (w.kind) {
        case UIWidgetKind::Slider: {
            if (fine != dragAnchor_.fine) {
                dragAnchor_ = { (float)w.spec.unmap(w.values[0]), (float)e.x,
                                fine };
            }
            float pos = ui_gesture::sliderDragPos(dragAnchor_, minx, usable,
                                                  (float)e.x);
            w.values[0] = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::Range: {
            float plo = (float)w.spec.unmap(w.values[0]);
            float phi = (float)w.spec.unmap(w.values[1]);
            float pos = std::clamp((e.x - minx) / usable, 0.0f, 1.0f);
            ui_gesture::adjustRangeEnd(pos, plo, phi);
            w.values[0] = w.spec.map(plo);
            w.values[1] = w.spec.map(phi);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::Number: {
            // Vertical drag adjusts; scale by the spec range.
            float dy = dragAnchor_.anchorMouse - (float)e.y;
            dragAnchor_.anchorMouse = (float)e.y;
            double step = (w.spec.hi - w.spec.lo) / 200.0;
            if (fine) step *= (fine == 1 ? 0.1 : 0.01);
            w.values[0] = w.spec.clamp(w.values[0] + dy * step);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::XY: {
            float px = std::clamp((float)e.x / bb.getWidth(), 0.0f, 1.0f);
            float py = std::clamp(1.0f - (float)e.y / bb.getHeight(), 0.0f, 1.0f);
            w.values[0] = w.spec.map(px);
            w.values[1] = w.spec2.map(py);
            markDirtyAndNotify(w, false);
        } break;
        case UIWidgetKind::MultiSlider: {
            int n = (int)w.values.size();
            if (n > 0) {
                int i = std::clamp((int)(e.x / (bb.getWidth() / n)), 0, n - 1);
                float pos = std::clamp(1.0f - (float)e.y / bb.getHeight(),
                                       0.0f, 1.0f);
                w.values[i] = w.spec.map(pos);
                markDirtyAndNotify(w, false);
            }
        } break;
        case UIWidgetKind::Matrix: {
            int idx = ui_gesture::matrixCellHit(
                (float)e.x, (float)e.y, bb.getWidth(), bb.getHeight(),
                std::max(1, w.rows), std::max(1, w.cols));
            if (idx >= 0 && idx < (int)w.values.size()
                && matrixPaintValue_ >= 0
                && w.values[idx] != (double)matrixPaintValue_) {
                w.values[idx] = matrixPaintValue_;
                markDirtyAndNotify(w, false);
            }
        } break;
        default: break;
    }
}

void WidgetComponent::mouseUp(juce::MouseEvent const&) {
    if (!dragging_) return;
    dragging_ = false;
    matrixPaintValue_ = -1;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;
    if (w.kind == UIWidgetKind::Button) {
        if (!w.values.empty()) w.values[0] = 0.0; // momentary release
    }
    markDirtyAndNotify(w, true);
}

void WidgetComponent::mouseDoubleClick(juce::MouseEvent const&) {
    // Reset to the spec's init value.
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;
    switch (w.kind) {
        case UIWidgetKind::Slider:
        case UIWidgetKind::Number:
            if (!w.values.empty()) {
                w.values[0] = w.spec.clamp(w.spec.init);
                markDirtyAndNotify(w, true);
            }
            break;
        case UIWidgetKind::XY:
            if (w.values.size() >= 2) {
                w.values[0] = w.spec.clamp(w.spec.init);
                w.values[1] = w.spec2.clamp(w.spec2.init);
                markDirtyAndNotify(w, true);
            }
            break;
        default: break;
    }
}

void WidgetComponent::mouseWheelMove(juce::MouseEvent const&,
                                     juce::MouseWheelDetails const& wheel) {
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;
    if (w.kind == UIWidgetKind::Slider && !w.values.empty()) {
        float pos = (float)w.spec.unmap(w.values[0]) + wheel.deltaY * 0.03f;
        w.values[0] = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
        markDirtyAndNotify(w, true);
    }
}

}
