// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  controls_panel.hpp
//  app
//
//  Renders the `ui` module's widget registry (bridge::UIState) as floating
//  ImGui windows -- one per panel name not claimed by the open notebook
//  document (claimed panels render inline as notebook panel cells) -- and
//  dispatches widget value changes each frame:
//    * engine fast path: dirty engine-bound widgets batch into one
//      begin()/setControl()/go() bundle per silo, on the GUI thread,
//      without entering the VM;
//    * lang callbacks: onChange closures fire with the latest value at
//      most once per widget per frame, only when nrtvm.mtx is free
//      (try_lock) -- a long-running eval never blocks the GUI.
//  dispatch() also polls engine taps into meter values / scope rings for
//  ALL widgets, wherever they are drawn.
//

#ifndef controls_panel_hpp
#define controls_panel_hpp

#include <string>
#include <vector>

namespace bridge { struct AppContext; struct UIState; }

struct ControlsPanel {
    // Reset per-frame display state. Call once per frame BEFORE any widget
    // drawing (notebook panel cells or floating windows).
    void beginFrame() { anyTapsVisible_ = false; }

    // Draw floating windows for all panels except those in `skipPanels`.
    void draw(bridge::UIState& ui,
              std::vector<std::string> const* skipPanels = nullptr);

    // Per-frame event dispatch + tap polling; call after all widget drawing.
    void dispatch(bridge::UIState& ui, bridge::AppContext& ctx);

    // True if any widget event is still pending (e.g. a callback deferred
    // because an eval holds the VM mutex) -- keeps the frame loop ticking.
    bool hasPendingEvents(bridge::UIState& ui);

    // True while a meter/scope widget was visible last frame -- the frame
    // loop stays at animation rate so live displays keep moving.
    bool wantsContinuousFrames() const { return anyTapsVisible_; }

    // Notebook panel cells report their visible taps here (draw() resets
    // the flag each frame before either view draws).
    void noteTapsVisible() { anyTapsVisible_ = true; }

    // Queue removal of these panels' widgets (with engine untap),
    // processed in dispatch() -- the same path as closing a floating
    // panel window. Used when a notebook document is closed or
    // replaced: its claimed panels' widgets go with it instead of
    // falling out into floating windows.
    void queuePanelRemoval(std::vector<std::string> const& panels) {
        pendingClosedPanels_.insert(pendingClosedPanels_.end(),
                                    panels.begin(), panels.end());
    }

private:
    bool anyTapsVisible_ = false;
    // Panels whose floating window was closed this frame; dispatch()
    // removes their widgets (and releases engine taps).
    std::vector<std::string> pendingClosedPanels_;
};

#endif /* controls_panel_hpp */
