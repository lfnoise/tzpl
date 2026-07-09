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
#include <random>
#include <utility>

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

void drawTick(Graphics& g, juce::Rectangle<float> const& z, float lenScale,
              float pos) {
    float x = z.getX() + pos * z.getWidth();
    g.drawLine(x, z.getY(), x, z.getY() + z.getHeight() * lenScale, 1.0f);
}

// Graduated scale on the slider background, ported from widget_draw.cpp.
// Exponential warps get log-spaced ticks (1..9 per decade, longer at 1/5);
// everything else gets a decimal hierarchy (longest at 0, then every 10th,
// 5th, 1st step).
void drawTickMarks(Graphics& g, bridge::UISpec const& spec,
                   juce::Rectangle<float> const& z) {
    double lo = spec.lo, hi = spec.hi;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return;
    g.setColour(juce::Colours::white.withAlpha(0.30f));

    if (spec.warp == bridge::UIWarp::Exponential && lo > 0.0 && hi > 0.0) {
        int lowOrder = (int)std::floor(std::log10(lo));
        int highOrder = (int)std::floor(std::log10(hi));
        for (int i = lowOrder; i <= highOrder; ++i) {
            double decade = std::pow(10.0, i);
            for (int j = 1; j <= 9; ++j) {
                double v = decade * j;
                if (v <= lo || v >= hi) continue;
                float pos = (float)spec.unmap(v);
                float next = (float)spec.unmap(decade * (j + 1));
                if (std::fabs(next - pos) < 0.005f) continue;
                float lenScale = j == 1 ? 0.75f : j == 5 ? 0.525f : 0.3f;
                drawTick(g, z, lenScale, pos);
            }
        }
    } else {
        double range = hi - lo;
        int order = (int)std::floor(std::log10(range) + 0.5);
        double step = std::pow(10.0, order - 1);
        if (range / step >= 20.0) {
            step *= 2.0;
            if (range / step >= 20.0) step *= 2.5;
        }
        double epsilon = range * 1e-5;
        for (double v = step * std::ceil(lo / step); v <= hi + epsilon;
             v += step) {
            int index = (int)std::floor(v / step + 0.5);
            float lenScale = index == 0      ? 1.0f
                           : index % 10 == 0 ? 0.75f
                           : index % 5 == 0  ? 0.525f
                                             : 0.3f;
            drawTick(g, z, lenScale, (float)spec.unmap(v));
        }
    }
}
}

WidgetComponent::WidgetComponent(bridge::UIState& ui, std::uint64_t id,
                                 ControlsDispatcher& dispatcher)
    : ui_(ui), id_(id), dispatcher_(dispatcher)
{
    setMouseClickGrabsKeyboardFocus(false);
    refreshMeta();
}

void WidgetComponent::refreshMeta() {
    std::lock_guard<std::mutex> lock(ui_.mtx);
    if (auto* w = ui_.findById(id_)) {
        kind_ = w->kind;
        name_ = w->name;
    }
    // Only slider-likes accept hover-key focus; others never take it.
    setWantsKeyboardFocus(isHoverAdjustable());
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
    if (arrange_) {
        // Move border + bottom-right resize grip.
        g.setColour(juce::Colour(0xffe0a020));
        g.drawRect(getLocalBounds().toFloat(), 1.5f);
        float s = 12.0f;
        g.fillRect(getWidth() - s, getHeight() - s, s, s);
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
    // Graduated tick scale while hovered or dragging (matches ImGui).
    if (isMouseOverOrDragging(true) || dragging_) {
        auto z = bb.reduced(2.0f, 2.0f);
        drawTickMarks(g, w.spec, z);
    }
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
    // The half-pixel outsets keep a collapsed range visible as a thin line.
    float x0 = minx + plo * usable - 0.5f;
    float x1 = minx + phi * usable + 0.5f;
    g.setColour(juce::Colour(0xff5a9bd4));
    g.fillRoundedRectangle(x0, bb.getY() + 2, x1 - x0, bb.getHeight() - 4, 3.0f);
    // Graduated tick scale while hovered or dragging (matches ImGui).
    if (isMouseOverOrDragging(true) || dragging_)
        drawTickMarks(g, w.spec, bb.reduced(2.0f, 2.0f));
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
    int edo = std::max(1, w.rollEdo);
    float rh = bb.getHeight() / rows;
    float beats = std::max(0.001f, w.rollBeats);
    constexpr float stepBeats = 0.25f;   // 16th grid
    int cols = std::max(1, (int)std::lround(beats / stepBeats));
    float cw = bb.getWidth() / cols;

    // Reference-band shading on octave rows (pitch % edo == 0) for orientation.
    if (rh >= 2.0f) {
        g.setColour(juce::Colour(0x10ffffff));
        for (int r = 0; r < rows; ++r) {
            int pitch = w.rollLowPitch + r;
            if (pitch % edo == 0)
                g.fillRect(bb.getX(), bb.getBottom() - (r + 1) * rh,
                           bb.getWidth(), rh);
        }
    }
    // Horizontal lane lines (skip when subpixel-dense, e.g. cents).
    if (edo <= 24 && rh >= 3.0f) {
        g.setColour(juce::Colour(0x18ffffff));
        for (int r = 0; r <= rows; ++r)
            g.drawHorizontalLine((int)(bb.getY() + r * rh),
                                 bb.getX(), bb.getRight());
    }
    // Vertical grid: 16th columns, brighter on beats (every 4th step).
    for (int c = 0; c <= cols; ++c) {
        g.setColour(c % 4 == 0 ? juce::Colour(0x50ffffff)
                               : juce::Colour(0x1cffffff));
        g.drawVerticalLine((int)(bb.getX() + c * cw), bb.getY(), bb.getBottom());
    }
    // Notes: (pitch, startBeat, durBeats) triplets.
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

void WidgetComponent::setArrange(bool on, int canvasTop) {
    arrange_ = on;
    canvasTop_ = canvasTop;
    setMouseCursor(on ? juce::MouseCursor::DraggingHandCursor
                      : juce::MouseCursor::NormalCursor);
    repaint();
}

// Modifiers are read live rather than latched at mouse-down, so one gesture
// can switch modes mid-drag. Shift (not ctrl) adjusts an end: on macOS a
// physical ctrl+click arrives as a right-click.
void WidgetComponent::applyRangeDrag(UIWidget& w, float pos,
                                     juce::ModifierKeys mods) {
    float plo = (float)w.spec.unmap(w.values[0]);
    float phi = (float)w.spec.unmap(w.values[1]);
    if (mods.isAltDown()) {
        ui_gesture::moveRange(pos, plo, phi);        // slide the whole range
    } else if (mods.isShiftDown()) {
        ui_gesture::adjustRangeEnd(pos, plo, phi);   // adjust the nearer end
    } else {
        plo = std::min(rangeAnchor_, pos);           // sweep out a new range
        phi = std::max(rangeAnchor_, pos);
    }
    w.values[0] = w.spec.map(plo);
    w.values[1] = w.spec.map(phi);
    markDirtyAndNotify(w, false);
}

void WidgetComponent::mouseDown(juce::MouseEvent const& e) {
    // Arrange mode: begin a move, or a resize if in the bottom-right grip.
    if (arrange_) {
        arrangeStartBounds_ = getBounds();
        arrangeResizing_ = e.x >= getWidth() - 14 && e.y >= getHeight() - 14;
        return;
    }

    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;

    // Cmd-click a Slider/Number opens inline numeric entry (Alt is fine-drag,
    // so Cmd is free for this -- matches the ImGui app).
    if (e.mods.isCommandDown() && !e.mods.isAltDown()
        && (w.kind == UIWidgetKind::Slider || w.kind == UIWidgetKind::Number)) {
        openValueEntry(w.values.empty() ? w.spec.init : w.values[0], 0,
                       bodyRect(*this, kLabelH).toNearestInt());
        return;
    }
    // Cmd-click a Range edits the end whose half was clicked; the editor
    // covers only that half so the other end stays readable.
    if (e.mods.isCommandDown() && !e.mods.isAltDown()
        && w.kind == UIWidgetKind::Range) {
        if (w.values.size() < 2) w.values.resize(2);
        auto halves = bodyRect(*this, kLabelH);
        int end = (float)e.x < halves.getCentreX() ? 0 : 1;
        auto half = end == 0 ? halves.withTrimmedRight(halves.getWidth() * 0.5f)
                             : halves.withTrimmedLeft(halves.getWidth() * 0.5f);
        openValueEntry(w.values[(size_t)end], end, half.toNearestInt());
        return;
    }
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
            rangeAnchor_ = std::clamp((e.x - minx) / usable, 0.0f, 1.0f);
            applyRangeDrag(w, rangeAnchor_, e.mods);
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
        case UIWidgetKind::PianoRoll: {
            // Click a cell to add a 16th note; click an existing note to
            // remove it. Notes are (pitch, startBeat, durBeats) triplets.
            dragging_ = false;   // a discrete edit, not a drag
            constexpr float stepBeats = 0.25f;
            int rows = std::max(1, w.rollRows);
            float rh = bb.getHeight() / rows;
            float beats = std::max(0.001f, w.rollBeats);
            float bx = (float)e.x / bb.getWidth() * beats;
            float start = std::max(0.0f, std::floor(bx / stepBeats) * stepBeats);
            int row = std::clamp((int)std::floor((bb.getBottom() - (float)e.y) / rh),
                                 0, rows - 1);
            int pitch = w.rollLowPitch + row;
            int hit = -1;
            for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
                if (start < w.noteData[i + 1]
                    || start >= w.noteData[i + 1] + w.noteData[i + 2]) continue;
                if ((int)std::lround(w.noteData[i]) == pitch) { hit = (int)i; break; }
            }
            if (hit >= 0)
                w.noteData.erase(w.noteData.begin() + hit,
                                 w.noteData.begin() + hit + 3);
            else {
                w.noteData.push_back((float)pitch);
                w.noteData.push_back(start);
                w.noteData.push_back(stepBeats);
            }
            // Note lists deliver via the onChange callback only (no engine
            // fast path); commit one history node per edit.
            w.dirtyCallback = true;
            w.gestureEnded = true;
            dispatcher_.ensureRunning();
            repaint();
        } break;
        default: break;
    }
}

void WidgetComponent::mouseDrag(juce::MouseEvent const& e) {
    if (arrange_) {
        int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
        if (arrangeResizing_) {
            setSize(std::max(24, arrangeStartBounds_.getWidth() + dx),
                    std::max(14, arrangeStartBounds_.getHeight() + dy));
        } else {
            setTopLeftPosition(
                std::max(0, arrangeStartBounds_.getX() + dx),
                std::max(canvasTop_, arrangeStartBounds_.getY() + dy));
        }
        return;
    }
    if (!dragging_) return;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return;
    UIWidget& w = *wp;

    float minx, usable;
    trackGeometry(minx, usable);
    auto bb = bodyRect(*this, kLabelH);
    // Shift = fine drag (0.1x), Alt = finer (0.01x). Cmd is reserved for
    // numeric text entry.
    int fine = e.mods.isShiftDown() ? 1 : (e.mods.isAltDown() ? 2 : 0);

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
        case UIWidgetKind::Range:
            applyRangeDrag(w, std::clamp((e.x - minx) / usable, 0.0f, 1.0f),
                           e.mods);
            break;
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
    if (arrange_) {
        // Snap to the 8px grid and write the frame back to the registry.
        auto snap = [](int v) { return (v + 4) / 8 * 8; };
        int nx = std::max(0, snap(getX()));
        int ny = std::max(0, snap(getY() - canvasTop_));
        setTopLeftPosition(nx, ny + canvasTop_);
        {
            std::lock_guard<std::mutex> lock(ui_.mtx);
            if (auto* wp = ui_.findById(id_)) {
                wp->fx = (float)nx;
                wp->fy = (float)ny;
                wp->fw = (float)getWidth();
                wp->fh = (float)getHeight();
            }
        }
        if (onArrangeEnd) onArrangeEnd();
        return;
    }
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

void WidgetComponent::mouseWheelMove(juce::MouseEvent const& e,
                                     juce::MouseWheelDetails const& wheel) {
    // Pushing the wheel up RAISES the value (matches the ImGui app, whose
    // handler subtracts the +up wheel delta from the position). Non-adjustable
    // kinds pass the event through so an enclosing viewport can scroll.
    if (arrange_) { juce::Component::mouseWheelMove(e, wheel); return; }
    constexpr float kStep = 0.06f;
    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) { juce::Component::mouseWheelMove(e, wheel); return; }
    UIWidget& w = *wp;
    auto bb = bodyRect(*this, kLabelH);
    switch (w.kind) {
        case UIWidgetKind::Slider: {
            if (w.values.empty()) break;
            float pos = (float)w.spec.unmap(w.values[0]) - wheel.deltaY * kStep;
            w.values[0] = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
            markDirtyAndNotify(w, true);
        } break;
        case UIWidgetKind::Range: {
            if (w.values.size() < 2) break;
            // The wheel slides the whole range, preserving its width.
            float plo = (float)w.spec.unmap(w.values[0]);
            float phi = (float)w.spec.unmap(w.values[1]);
            ui_gesture::moveRange((plo + phi) * 0.5f - wheel.deltaY * kStep,
                                  plo, phi);
            w.values[0] = w.spec.map(plo);
            w.values[1] = w.spec.map(phi);
            markDirtyAndNotify(w, true);
        } break;
        case UIWidgetKind::XY: {
            if (w.values.size() < 2) break;
            // Vertical wheel drives Y (up = higher), horizontal drives X.
            float y = (float)w.spec2.unmap(w.values[1]) - wheel.deltaY * kStep;
            float x = (float)w.spec.unmap(w.values[0]) + wheel.deltaX * kStep;
            w.values[0] = w.spec.map(std::clamp(x, 0.0f, 1.0f));
            w.values[1] = w.spec2.map(std::clamp(y, 0.0f, 1.0f));
            markDirtyAndNotify(w, true);
        } break;
        case UIWidgetKind::MultiSlider: {
            int n = (int)w.values.size();
            if (n <= 0) break;
            // The wheel adjusts only the bar under the cursor.
            int i = std::clamp((int)(e.position.x / (bb.getWidth() / n)),
                               0, n - 1);
            float pos = (float)w.spec.unmap(w.values[i]) - wheel.deltaY * kStep;
            w.values[i] = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
            markDirtyAndNotify(w, true);
        } break;
        default:
            juce::Component::mouseWheelMove(e, wheel);
            break;
    }
}

// ---------------------------------------------------------------------------
// Hover-key adjustments (widget_draw.cpp hoverAdjust*). While the pointer is
// over a slider-like widget and no text field is focused, typed characters
// nudge the value: c center, [ / ] rails, 1-9 jump, r randomize, j/J jitter,
// , / . step, z zero, i init, - negate, / reciprocal. The Range binds the same
// keys to both of its ends (see hoverRangeChar).
// ---------------------------------------------------------------------------

bool WidgetComponent::isHoverAdjustable() const {
    return kind_ == UIWidgetKind::Slider || kind_ == UIWidgetKind::Range
        || kind_ == UIWidgetKind::XY || kind_ == UIWidgetKind::MultiSlider;
}

namespace {
float hoverUniform(float lo, float hi) {
    static thread_local std::minstd_rand rng{ std::random_device{}() };
    return lo + (hi - lo) * std::uniform_real_distribution<float>{}(rng);
}
float hoverBounce(float p) {
    if (p < 0.0f) return -p;
    if (p > 1.0f) return 2.0f - p;
    return p;
}

// Apply one character to a single-axis position (Slider bar / MultiSlider bar).
// Returns true and writes the new value if the char was a recognized command.
bool hoverAxisChar(unsigned c, bridge::UISpec const& spec, double& value) {
    float pos = (float)spec.unmap(value);
    double v = value;
    auto set = [&](float p) {
        value = spec.map(std::clamp(p, 0.0f, 1.0f));
    };
    switch (c) {
        case 'c': case 'C': set(0.5f); return true;
        case '[': set(0.0f); return true;
        case ']': set(1.0f); return true;
        case 'r': case 'R': set(hoverUniform(0.0f, 1.0f)); return true;
        case 'j': set(hoverBounce(pos + hoverUniform(-0.05f, 0.05f))); return true;
        case 'J': set(hoverBounce(pos + hoverUniform(-0.005f, 0.005f))); return true;
        case ',': set(pos - 0.05f); return true;
        case '.': set(pos + 0.05f); return true;
        case 'z': case 'Z': set((float)spec.unmap(spec.clamp(0.0))); return true;
        case 'i': case 'I': set((float)spec.unmap(spec.clamp(spec.init))); return true;
        case '-':
            if (spec.contains(-v)) set((float)spec.unmap(-v));
            return true;
        case '/':
            if (v != 0.0 && spec.contains(1.0 / v)) set((float)spec.unmap(1.0 / v));
            return true;
        default:
            if (c >= '1' && c <= '9') { set(0.1f * (float)(c - '0')); return true; }
            return false;
    }
}

// The Range's hover keys mirror the slider's where they make sense: keys that
// set one position act on both ends (collapsing the range), [ / ] send one end
// to its rail, , / . slide the whole range (width preserved), and - / / negate
// / take reciprocals of both ends (re-ordered). Digits are not bound: there is
// no single position for them to jump to.
bool hoverRangeChar(unsigned c, bridge::UISpec const& spec, double& lo,
                    double& hi) {
    float ulo = (float)spec.unmap(lo), uhi = (float)spec.unmap(hi);
    double vlo = lo, vhi = hi;
    auto setPos = [&](float plo, float phi) {
        if (plo > phi) std::swap(plo, phi);
        lo = spec.map(std::clamp(plo, 0.0f, 1.0f));
        hi = spec.map(std::clamp(phi, 0.0f, 1.0f));
    };
    auto setVal = [&](double a, double b) {
        if (a > b) std::swap(a, b);
        lo = a;
        hi = b;
    };
    auto slide = [&](float d) {
        float a = ulo, b = uhi;
        ui_gesture::moveRange((a + b) * 0.5f + d, a, b);
        setPos(a, b);
    };
    switch (c) {
        case 'c': case 'C': setPos(0.5f, 0.5f); return true;
        case '[': setPos(0.0f, uhi); return true;
        case ']': setPos(ulo, 1.0f); return true;
        case 'r': case 'R':
            setPos(hoverUniform(0.0f, 1.0f), hoverUniform(0.0f, 1.0f));
            return true;
        case 'j':
            setPos(hoverBounce(ulo + hoverUniform(-0.05f, 0.05f)),
                   hoverBounce(uhi + hoverUniform(-0.05f, 0.05f)));
            return true;
        case 'J':
            setPos(hoverBounce(ulo + hoverUniform(-0.005f, 0.005f)),
                   hoverBounce(uhi + hoverUniform(-0.005f, 0.005f)));
            return true;
        case 'z': case 'Z':
            if (spec.contains(0.0)) setVal(0.0, 0.0);
            return true;
        case 'i': case 'I': {
            double init = spec.clamp(spec.init);
            setVal(init, init);
        }   return true;
        case '-':
            if (spec.contains(-vlo) && spec.contains(-vhi)) setVal(-vhi, -vlo);
            return true;
        case '/':
            if (vlo != 0.0 && vhi != 0.0 && spec.contains(1.0 / vlo)
                && spec.contains(1.0 / vhi))
                setVal(1.0 / vlo, 1.0 / vhi);
            return true;
        case ',': slide(-0.05f); return true;
        case '.': slide(0.05f); return true;
        default: return false;
    }
}
}

bool WidgetComponent::keyPressed(juce::KeyPress const& key) {
    if (!isHoverAdjustable()) return false;
    juce::juce_wchar ch = key.getTextCharacter();
    if (ch == 0) return false;
    unsigned c = (unsigned)ch;

    std::lock_guard<std::mutex> lock(ui_.mtx);
    UIWidget* wp = ui_.findById(id_);
    if (!wp) return false;
    UIWidget& w = *wp;
    bool handled = false;

    if (w.kind == UIWidgetKind::Slider) {
        if (!w.values.empty()) handled = hoverAxisChar(c, w.spec, w.values[0]);
    } else if (w.kind == UIWidgetKind::Range) {
        if (w.values.size() >= 2)
            handled = hoverRangeChar(c, w.spec, w.values[0], w.values[1]);
    } else if (w.kind == UIWidgetKind::MultiSlider) {
        // Keys act on ALL bars (independent randoms per bar for r/j).
        for (auto& val : w.values)
            handled = hoverAxisChar(c, w.spec, val) || handled;
    } else if (w.kind == UIWidgetKind::XY && w.values.size() >= 2) {
        bool hx = hoverAxisChar(c, w.spec, w.values[0]);
        bool hy = hoverAxisChar(c, w.spec2, w.values[1]);
        handled = hx || hy;
    }

    if (handled) markDirtyAndNotify(w, true);
    return handled;
}

void WidgetComponent::mouseEnter(juce::MouseEvent const&) {
    if (arrange_) return;
    // Take keyboard focus so hover-keys reach this widget -- but never steal
    // it from an editor/text field the user is typing in.
    if (isHoverAdjustable()) {
        auto* f = juce::Component::getCurrentlyFocusedComponent();
        if (dynamic_cast<juce::TextInputTarget*>(f) == nullptr)
            grabKeyboardFocus();
    }
    repaint(); // show the graduated scale on hover
}

void WidgetComponent::mouseExit(juce::MouseEvent const&) {
    // Release focus so a non-hovered widget doesn't keep eating keys.
    if (hasKeyboardFocus(false)) giveAwayKeyboardFocus();
    repaint();
}

void WidgetComponent::openValueEntry(double current, int index,
                                     juce::Rectangle<int> bounds) {
    valueEditIndex_ = index;
    valueEditor_ = std::make_unique<juce::TextEditor>();
    valueEditor_->setBounds(bounds);
    valueEditor_->setJustification(juce::Justification::centred);
    valueEditor_->setText(fmtValue(current), false);
    valueEditor_->selectAll();
    valueEditor_->onReturnKey = [this] { commitValueEntry(); };
    valueEditor_->onEscapeKey = [this] { valueEditor_.reset(); };
    valueEditor_->onFocusLost = [this] { commitValueEntry(); };
    addAndMakeVisible(*valueEditor_);
    valueEditor_->grabKeyboardFocus();
}

void WidgetComponent::commitValueEntry() {
    if (!valueEditor_) return;
    // reset() first so the onFocusLost fired by destroying/refocusing doesn't
    // re-enter this.
    auto editor = std::move(valueEditor_);
    double v = editor->getText().getDoubleValue();
    {
        std::lock_guard<std::mutex> lock(ui_.mtx);
        auto* wp = ui_.findById(id_);
        if (!wp || (int)wp->values.size() <= valueEditIndex_) return;
        wp->values[(size_t)valueEditIndex_] = wp->spec.clamp(v);
        // Typing past the other end of a Range re-orders the pair rather
        // than rejecting the edit.
        if (wp->kind == UIWidgetKind::Range && wp->values.size() >= 2
            && wp->values[0] > wp->values[1])
            std::swap(wp->values[0], wp->values[1]);
        markDirtyAndNotify(*wp, true);
    }
}

}
