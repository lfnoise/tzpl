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
//  Renders the `ui` module's widget registry (bridge::UIState) as ImGui
//  windows, and dispatches widget value changes each frame:
//    * engine fast path: dirty engine-bound widgets batch into one
//      begin()/setControl()/go() bundle per silo, on the GUI thread,
//      without entering the VM;
//    * lang callbacks: onChange closures fire with the latest value at
//      most once per widget per frame, only when nrtvm.mtx is free
//      (try_lock) -- a long-running eval never blocks the GUI.
//

#ifndef controls_panel_hpp
#define controls_panel_hpp

namespace bridge { struct AppContext; struct UIState; }

struct ControlsPanel {
    // Draw all widgets, grouped into one window per panel name.
    void draw(bridge::UIState& ui);

    // Per-frame event dispatch; call after draw().
    void dispatch(bridge::UIState& ui, bridge::AppContext& ctx);

    // True if any widget event is still pending (e.g. a callback deferred
    // because an eval holds the VM mutex) -- keeps the frame loop ticking.
    bool hasPendingEvents(bridge::UIState& ui);
};

#endif /* controls_panel_hpp */
