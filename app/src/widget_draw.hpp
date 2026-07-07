// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//
//  widget_draw.hpp
//  app
//
//  ImGui rendering for the `ui` module's widgets, shared between the
//  floating Controls windows (controls_panel) and the notebook's inline
//  panel cells (notebook_panel).
//

#ifndef widget_draw_hpp
#define widget_draw_hpp

#include <string>

namespace bridge { struct UIState; struct UIWidget; }

// Draw one widget. Caller holds UIState::mtx. Returns true if the widget
// is tap-backed (meter/scope), i.e. wants continuous frames while visible.
bool drawUIWidget(bridge::UIWidget& w);

// Draw all widgets belonging to `panel`, in registry order. Caller holds
// UIState::mtx. Returns true if any tap-backed widget was drawn.
bool drawPanelWidgets(bridge::UIState& ui, std::string const& panel);

// Scale applied to widget frames and default sizes by the draw functions
// (perform mode's big targets). Set around a panel draw, then restore to
// 1.0. Pair with ImGui::SetWindowFontScale so text follows.
void setUIDrawScale(float s);

// Fire key-bound widgets (ui.bindKey): buttons act momentary (down/up),
// toggles flip on press. Call once per frame inside the ImGui frame; a
// no-op while a text field wants input or a hovered gesture widget owns
// the keyboard. Takes UIState::mtx itself.
void dispatchWidgetKeys(bridge::UIState& ui);

// Hover keyboard ownership: a hovered slider/range/xy/multislider eats
// ALL typed characters (they act on the widget and reach nothing else).
// Call right after ImGui::NewFrame(), before any window draws -- when a
// widget was hovered last frame this moves the character queue aside
// before editors can read it.
void uiHoverKeysNewFrame();

#endif /* widget_draw_hpp */
