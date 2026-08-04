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

#include "status_bar.hpp"

#include "tzpl_app_context.hpp"
#include "nrt_vm.hpp"
#include "tracing_gc.hpp"

#include <cmath>

namespace tzplapp {

namespace {

constexpr int kHz = 20;
// The cheap master-level read is atomics only, so it runs every tick. The
// full snapshot takes the engine's NRT lock, so it runs at 5 Hz while
// collapsed (every tick while the detail panel is open).
constexpr int kStatsEvery = 4;
constexpr int kRowH = 22;
constexpr int kDetailRowH = 16;
constexpr int kFlashTicks = 16;      // ~800 ms
constexpr int kLogCooldownTicks = kHz; // one console line per second, at most
constexpr int kPad = 8;

// Compact-row cell widths, left to right. Shared by paint and hit testing so
// the clip square's rect can't drift from where it is drawn.
constexpr int kTriangleW = 14;
constexpr int kDeviceW = 120;
constexpr int kCpuW = 96;
constexpr int kMasterW = 70;
constexpr int kClipW = 14;
constexpr int kGainW = 104;
// Dead space between the gain slider and the mute button, so a slightly
// missed drag at the top of the slider can't toggle mute.
constexpr int kGainMuteGap = 14;
constexpr int kMuteW = 52;
constexpr int kPanicBtnH = 18;

juce::String fmt1(double v) { return juce::String(v, 1); }

// Amplitude -> 0..1 over -60..0 dB, matching the widget and graph meters.
float db01(float lin) {
    if (lin <= 1e-6f) return 0.f;
    float db = 20.0f * std::log10(lin);
    return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
}

juce::Colour loadColour(double pct) {
    if (pct >= 80.0) return juce::Colours::red;
    if (pct >= 50.0) return juce::Colours::orange;
    return juce::Colour(0xff4caf50);
}

} // namespace

StatusBar::StatusBar(bridge::AppContext& appCtx) : appCtx_(appCtx) {
    // Never steal the caret from the editor underneath.
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setInterceptsMouseClicks(true, false);
}

int StatusBar::preferredHeight() const {
    if (!expanded_) return kRowH;
    int silos = (int)stats_.silos.size();
    // Panic row + header + one row per silo + NRT-VM row + device row.
    return kRowH + kPanicBtnH + kDetailRowH * (silos + 3) + kPad;
}

void StatusBar::visibilityChanged() {
    if (isVisible()) { refreshStats(); startTimerHz(kHz); }
    else stopTimer();
}

void StatusBar::timerCallback() {
    if (!isShowing()) return;

    if (expanded_ || ++statsTick_ >= kStatsEvery) {
        statsTick_ = 0;
        refreshStats();
    }

    if (flashTicks_ > 0) --flashTicks_;
    if (logCooldown_ > 0) --logCooldown_;
    repaint();
}

void StatusBar::refreshStats() {
    if (!appCtx_.engine) return;
    engine::getEngineStats(appCtx_.engine, stats_);

    // Both dropout sources rolled into one "did audio break" signal; the
    // detail panel breaks them apart again.
    unsigned long long total = stats_.engineDropouts + stats_.deviceXruns;
    // A total that went DOWN means the counters were reset behind us (not
    // only by our own click). Follow it down, or the next real dropout goes
    // unreported until it climbs back past the old high-water mark.
    if (total < lastDropoutTotal_) lastDropoutTotal_ = total;
    if (total > lastDropoutTotal_) {
        unsigned long long added = total - lastDropoutTotal_;
        lastDropoutTotal_ = total;
        dropoutLatched_ = true;
        flashTicks_ = kFlashTicks;
        if (logCooldown_ == 0 && onDropout) {
            onDropout("audio dropout (" + std::to_string(added)
                      + " new, " + std::to_string(total) + " total)"
                      " -- click XRUN in the status bar to reset");
            logCooldown_ = kLogCooldownTicks;
        }
    }
    // Same rule as the dropout total: follow the counter down when something
    // else resets it, or the next clip goes unlatched.
    if (stats_.clipCount < lastClipCount_) lastClipCount_ = stats_.clipCount;
    if (stats_.clipCount > lastClipCount_) {
        lastClipCount_ = stats_.clipCount;
        clipLatched_ = true;
    }

    // The NRT VM's collector lives behind nrtvm.mtx, which a long compile
    // can hold for a while. try_lock and keep the previous numbers rather
    // than blocking the message thread. (Lock order is nrtvm.mtx before
    // UIState::mtx -- we hold neither here.)
    if (appCtx_.nrtvm) {
        std::unique_lock<std::mutex> lk(appCtx_.nrtvm->mtx, std::try_to_lock);
        if (lk.owns_lock()) {
            auto& gc = appCtx_.nrtvm->vm.tracingGC();
            nrtGcSteps_ = gc.stepCount();
            nrtGcCycles_ = gc.cyclesCompleted();
            nrtGcMaxNanos_ = gc.stepMaxNanos();
            nrtGcValid_ = true;
        }
    }
}

juce::Rectangle<int> StatusBar::compactRow() const {
    return getLocalBounds().removeFromBottom(kRowH);
}

juce::Rectangle<int> StatusBar::xrunArea() const {
    auto r = compactRow();
    return r.removeFromRight(110).withTrimmedRight(20);
}

// The clip square, at its full cell width -- the drawn 8x8 dot is too small
// to ask anyone to hit.
juce::Rectangle<int> StatusBar::clipArea() const {
    auto r = compactRow().reduced(kPad, 3);
    r.removeFromLeft(kTriangleW);
    r.removeFromLeft(kDeviceW);
    r.removeFromLeft(kCpuW);
    r.removeFromLeft(kMasterW);
    return r.removeFromLeft(kClipW);
}

juce::Rectangle<int> StatusBar::gainArea() const {
    auto r = compactRow().reduced(kPad, 3);
    r.removeFromLeft(kTriangleW + kDeviceW + kCpuW + kMasterW + kClipW);
    return r.removeFromLeft(kGainW);
}

juce::Rectangle<int> StatusBar::muteArea() const {
    auto r = compactRow().reduced(kPad, 3);
    r.removeFromLeft(kTriangleW + kDeviceW + kCpuW + kMasterW + kClipW + kGainW
                     + kGainMuteGap);
    return r.removeFromLeft(kMuteW).reduced(3, 1);
}

juce::Rectangle<int> StatusBar::panicRow() const {
    return getLocalBounds().withTrimmedBottom(kRowH).reduced(kPad, 2)
        .removeFromTop(kPanicBtnH);
}

void StatusBar::panicButtonRects(juce::Rectangle<int>* out3) const {
    auto row = panicRow();
    row.removeFromLeft(48);   // "panic" caption
    out3[0] = row.removeFromLeft(96).reduced(2, 1);
    out3[1] = row.removeFromLeft(122).reduced(2, 1);
    out3[2] = row.removeFromLeft(100).reduced(2, 1);
}

void StatusBar::setGainFromX(int x) {
    if (!appCtx_.engine) return;
    auto track = gainArea().withTrimmedLeft(30);   // past the "gain" label
    float frac = juce::jlimit(0.0f, 1.0f,
        (float)(x - track.getX()) / (float)std::max(1, track.getWidth()));
    engine::masterGain(appCtx_.engine, frac);
    repaint();
}

void StatusBar::forEachSiloBundle(std::function<void()> const& queueOps) {
    if (!appCtx_.engine) return;
    int n = engine::numSilos(appCtx_.engine);
    for (int i = 0; i < n; ++i) {
        engine::begin(appCtx_.engine);
        queueOps();
        engine::go(i);
    }
}

void StatusBar::mouseDown(juce::MouseEvent const& e) {
    // Master gain and mute live in the compact row and always win their
    // clicks: the row's expand-anywhere behavior must not swallow them.
    if (gainArea().contains(e.getPosition()) && appCtx_.engine) {
        draggingGain_ = true;
        setGainFromX(e.getPosition().x);
        return;
    }
    if (muteArea().contains(e.getPosition()) && appCtx_.engine) {
        engine::masterMute(appCtx_.engine, !engine::masterMuted(appCtx_.engine));
        repaint();
        return;
    }
    if (expanded_ && appCtx_.engine) {
        juce::Rectangle<int> btns[3];
        panicButtonRects(btns);
        if (btns[0].contains(e.getPosition())) {           // all notes off
            forEachSiloBundle([] { engine::allNotesOffAll(); });
            return;
        }
        if (btns[1].contains(e.getPosition())) {           // disconnect output
            forEachSiloBundle([] { engine::disconnectNode(0); });
            return;
        }
        if (btns[2].contains(e.getPosition())) {           // free all nodes
            forEachSiloBundle([] { engine::freeAllNodes(); });
            return;
        }
    }
    // The clip latch clears on its own click, without disturbing the dropout
    // counters -- checked first, since it sits inside the compact row. Only
    // while it is lit: an unlit square shouldn't be a dead spot in a row that
    // otherwise expands the detail panel wherever you click it.
    if (clipLatched_ && clipArea().contains(e.getPosition()) && appCtx_.engine) {
        engine::resetMasterClip(appCtx_.engine);
        clipLatched_ = false;
        lastClipCount_ = 0;
        repaint();
        return;
    }
    if (xrunArea().contains(e.getPosition()) && appCtx_.engine) {
        engine::resetEngineStats(appCtx_.engine);
        dropoutLatched_ = false;
        clipLatched_ = false;
        lastDropoutTotal_ = 0;
        lastClipCount_ = 0;
        refreshStats();
        repaint();
        return;
    }
    if (compactRow().contains(e.getPosition())) {
        expanded_ = !expanded_;
        if (expanded_) refreshStats();
        if (onHeightChanged) onHeightChanged();
        repaint();
    }
}

void StatusBar::mouseDrag(juce::MouseEvent const& e) {
    if (draggingGain_) setGainFromX(e.getPosition().x);
}

void StatusBar::mouseUp(juce::MouseEvent const&) {
    draggingGain_ = false;
}

void StatusBar::paint(juce::Graphics& g) {
    auto bg = findColour(juce::ResizableWindow::backgroundColourId);
    g.fillAll(flashTicks_ > 0 ? bg.interpolatedWith(juce::Colours::orange, 0.35f)
                              : bg.darker(0.2f));

    auto text = findColour(juce::Label::textColourId);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));

    auto row = compactRow();
    g.setColour(text.withAlpha(0.2f));
    g.drawHorizontalLine(row.getY(), (float)row.getX(), (float)row.getRight());

    auto r = row.reduced(kPad, 3);

    // Disclosure triangle.
    {
        auto tri = r.removeFromLeft(kTriangleW);
        juce::Path p;
        float cx = (float)tri.getCentreX(), cy = (float)tri.getCentreY();
        if (expanded_) { p.addTriangle(cx - 4, cy + 2, cx + 4, cy + 2, cx, cy - 3); }
        else           { p.addTriangle(cx - 2, cy - 4, cx - 2, cy + 4, cx + 3, cy); }
        g.setColour(text.withAlpha(0.6f));
        g.fillPath(p);
    }

    // Device format.
    g.setColour(text.withAlpha(0.55f));
    juce::String dev;
    if (stats_.sampleRate > 0.) {
        dev << juce::String(stats_.sampleRate / 1000.0, 1) << "k \xc2\xb7 "
            << stats_.bufferFrames;
        if (!stats_.audioRunning) dev << "  (stopped)";
    } else {
        dev << "no audio";
    }
    g.drawText(dev, r.removeFromLeft(kDeviceW), juce::Justification::centredLeft, true);

    // DSP load: number + bar.
    {
        auto cpu = r.removeFromLeft(kCpuW);
        g.setColour(text);
        g.drawText("CPU " + juce::String((int)std::lround(stats_.loadPercent)) + "%",
                   cpu.removeFromLeft(54), juce::Justification::centredLeft, true);
        auto bar = cpu.withSizeKeepingCentre(38, 8);
        g.setColour(text.withAlpha(0.2f));
        g.fillRect(bar);
        double frac = juce::jlimit(0.0, 1.0, stats_.loadPercent / 100.0);
        g.setColour(loadColour(stats_.loadPercent));
        g.fillRect(bar.withWidth((int)std::lround(frac * bar.getWidth())));
    }

    // Master level (peak hold), one lane per channel, plus a clip square.
    if (appCtx_.engine) {
        int chans = juce::jlimit(1, 2, engine::masterChans(appCtx_.engine));
        auto meter = r.removeFromLeft(kMasterW).withSizeKeepingCentre(60, 12);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(meter);
        int laneH = std::max(2, meter.getHeight() / chans);
        for (int c = 0; c < chans; ++c) {
            float v = engine::masterPeakHold(appCtx_.engine, c);
            auto lane = meter.withHeight(laneH).withY(meter.getY() + c * laneH);
            g.setColour(v >= 1.0f ? juce::Colours::red : juce::Colour(0xff4caf50));
            g.fillRect(lane.withWidth((int)std::lround(db01(v) * lane.getWidth())));
        }
        auto clip = r.removeFromLeft(kClipW).withSizeKeepingCentre(8, 8);
        g.setColour(clipLatched_ ? juce::Colours::red : text.withAlpha(0.15f));
        g.fillRect(clip);
    }

    // Master gain: label + draggable track + numeric value.
    if (appCtx_.engine) {
        auto cell = r.removeFromLeft(kGainW);
        float gain = engine::masterGain(appCtx_.engine);
        g.setColour(text.withAlpha(0.7f));
        g.drawText("gain", cell.removeFromLeft(30), juce::Justification::centredLeft, true);
        auto track = cell.withSizeKeepingCentre(cell.getWidth() - 4, 8);
        g.setColour(text.withAlpha(0.2f));
        g.fillRect(track);
        g.setColour(text.withAlpha(0.8f));
        int w = (int)std::lround(juce::jlimit(0.0f, 1.0f, gain) * track.getWidth());
        g.fillRect(track.withWidth(w));
        // Handle tick so the position reads even when the fill is tiny.
        g.fillRect(track.getX() + std::max(0, w - 1), track.getY() - 2, 2, track.getHeight() + 4);

        // Mute button: state carried by the label and fill, not colour alone.
        bool muted = engine::masterMuted(appCtx_.engine);
        auto mute = muteArea();
        if (muted) {
            g.setColour(juce::Colours::red.withAlpha(0.85f));
            g.fillRoundedRectangle(mute.toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText("MUTED", mute, juce::Justification::centred, true);
        } else {
            g.setColour(text.withAlpha(0.5f));
            g.drawRoundedRectangle(mute.toFloat(), 3.0f, 1.0f);
            g.drawText("mute", mute, juce::Justification::centred, true);
        }
    }

    // Dropouts: latching, cleared only by an explicit click.
    unsigned long long drops = stats_.engineDropouts + stats_.deviceXruns;
    if (dropoutLatched_ || drops > 0) {
        g.setColour(juce::Colours::red);
        g.drawText("XRUN " + juce::String((juce::int64)drops), xrunArea(),
                   juce::Justification::centredRight, true);
    }

    if (!expanded_) return;

    // ---- Detail panel -----------------------------------------------------
    auto d = getLocalBounds().withTrimmedBottom(kRowH).reduced(kPad, 2);

    // Panic row: three plain buttons that take effect on every silo.
    {
        auto row2 = d.removeFromTop(kPanicBtnH);
        g.setColour(text.withAlpha(0.7f));
        g.drawText("panic", row2.removeFromLeft(48), juce::Justification::centredLeft, true);
        juce::Rectangle<int> btns[3];
        panicButtonRects(btns);
        char const* labels[3] = { "all notes off", "disconnect output", "free all nodes" };
        for (int i = 0; i < 3; ++i) {
            g.setColour(text.withAlpha(0.5f));
            g.drawRoundedRectangle(btns[i].toFloat(), 3.0f, 1.0f);
            g.setColour(text.withAlpha(0.85f));
            g.drawText(labels[i], btns[i], juce::Justification::centred, true);
        }
    }

    auto line = [&](juce::String const& s, juce::Colour c) {
        g.setColour(c);
        g.drawText(s, d.removeFromTop(kDetailRowH),
                   juce::Justification::centredLeft, true);
    };

    line("block " + fmt1(stats_.blockAvgMs) + " / " + fmt1(stats_.blockBudgetMs)
             + " ms   peak " + fmt1(stats_.blockMaxMs)
             + " ms   over-budget " + juce::String((juce::int64)stats_.overBudgetCount),
         text.withAlpha(0.8f));

    for (auto const& s : stats_.silos) {
        juce::String row2;
        row2 << "silo " << s.index
             << "   dsp " << fmt1(s.avgMs) << " ms (max " << fmt1(s.maxMs) << ")"
             << "   " << (int)std::lround(s.loadPercent) << "%"
             << "   mix " << fmt1(s.mixWaitMs) << " ms"
             << "   taps " << s.numTaps
             << "   q " << s.fromNrtDepth << "/" << s.toNrtDepth
             << "/" << s.deadNodesDepth;
        if (s.hasVM) {
            row2 << "   gc " << (juce::int64)s.gcStepCount
                 << " steps, rt max " << (juce::int64)(s.gcRtMaxNanos / 1000) << " us";
        }
        line(row2, text.withAlpha(0.7f));
    }

    if (nrtGcValid_) {
        line("nrt vm   gc " + juce::String((juce::int64)nrtGcSteps_) + " steps, "
                 + juce::String((juce::int64)nrtGcCycles_) + " cycles, max "
                 + juce::String((juce::int64)(nrtGcMaxNanos_ / 1000)) + " us",
             text.withAlpha(0.7f));
    } else {
        line("nrt vm   gc (busy)", text.withAlpha(0.4f));
    }

    juce::String devRow;
    devRow << "device   ";
    if (stats_.deviceTelemetry) {
        devRow << "xruns " << (juce::int64)stats_.deviceXruns
               << "   cpu " << (int)std::lround(stats_.deviceCpu * 100.0) << "%";
    } else {
        devRow << "(no telemetry)";
    }
    devRow << "   engine dropouts " << (juce::int64)stats_.engineDropouts
           << "   bad blocks " << (juce::int64)stats_.badBlockSizeCount
           << "   rt exceptions " << (juce::int64)stats_.rtExceptionCount
           << "   clips " << (juce::int64)stats_.clipCount
           << "   limiter " << fmt1(stats_.limiterGain * 100.f) << "%";
    line(devRow, text.withAlpha(0.7f));

    g.setColour(text.withAlpha(0.4f));
    g.drawText("click XRUN to reset counters", d.removeFromTop(kDetailRowH),
               juce::Justification::centredLeft, true);
}

}
