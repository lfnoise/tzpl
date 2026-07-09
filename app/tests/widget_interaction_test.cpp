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
//  widget_interaction_test.cpp
//
//  Headless interaction tests for the custom slider / range widgets:
//  an ImGui context with no rendering backend, driven by injected IO
//  events. Covers the drag gestures (absolute, fine, range sweep /
//  move / end-adjust) and cmd-click text entry -- the paths that broke
//  before because TempInputScalar ran without a submitted item, and
//  because macOS reports Cmd as KeyCtrl (Ctrl<->Super swap).
//

#include "widget_draw.hpp"
#include "tzpl_ui_state.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;

static void check(bool ok, char const* what) {
    std::printf("%s: %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++failures;
}

static void checkNear(char const* what, double got, double want,
                      double eps = 0.75) {
    bool ok = std::fabs(got - want) <= eps;
    std::printf("%s: %s (got %g, want %g)\n", what, ok ? "ok" : "FAIL",
                got, want);
    if (!ok) ++failures;
}

// ---------------------------------------------------------------------------
// Harness: one widget drawn in a fixed window each frame.
// ---------------------------------------------------------------------------

static bridge::UIWidget* gW = nullptr;
static ImVec2 gItemPos;    // top-left of the widget's frame (captured per frame)
static float gFrameH = 0;
static float gLabelX = 0;  // where the name label landed (layout regression)

// Arrange mode: the widget is drawn DISABLED under an invisible move
// overlay + resize grip, mirroring notebook_panel.cpp's drawPanelPage.
static bool gArrange = false;
// The fix. With it off, the disabled widget keeps HoveredId and the
// overlay is only grabbable where the widget submitted no item (its label).
static bool gArrangeAllowOverlap = true;
// IsItemHovered() is only a rect test (it never consults HoveredId), so the
// meaningful signal is whether a press actually activates the overlay --
// that path runs ButtonBehavior -> ItemHoverable, where the HoveredId
// first-come rule lives.
static bool gMoveActive = false;
static bool gSizeActive = false;
static ImVec2 gGroupMin, gGroupMax;

static void drawArrangeOverlay() {
    ImGui::BeginDisabled();
    if (gArrangeAllowOverlap)
        ImGui::PushItemFlag(ImGuiItemFlags_AllowOverlap, true);
    ImGui::BeginGroup();
    if (gW) drawUIWidget(*gW);
    ImGui::EndGroup();
    if (gArrangeAllowOverlap) ImGui::PopItemFlag();
    ImGui::EndDisabled();

    gGroupMin = ImGui::GetItemRectMin();
    gGroupMax = ImGui::GetItemRectMax();
    ImGui::PushID(1);
    // Grip first: overlapping items are first-come-first-served.
    float const kGrip = 12.0f;   // hit area == the drawn square
    ImGui::SetCursorScreenPos(
        ImVec2(gGroupMax.x - kGrip, gGroupMax.y - kGrip));
    ImGui::InvisibleButton("##size", ImVec2(kGrip, kGrip));
    gSizeActive = ImGui::IsItemActive();
    ImGui::SetCursorScreenPos(gGroupMin);
    ImGui::InvisibleButton("##move",
        ImVec2(std::max(gGroupMax.x - gGroupMin.x, 16.0f),
               std::max(gGroupMax.y - gGroupMin.y, 16.0f)));
    gMoveActive = ImGui::IsItemActive();
    ImGui::PopID();
}

static void frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    uiHoverKeysNewFrame();  // hover keyboard ownership, as the app does
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoDecoration);
    gItemPos = ImGui::GetCursorScreenPos();
    gFrameH = ImGui::GetFrameHeight();
    if (gArrange) {
        drawArrangeOverlay();
    } else {
        if (gW) drawUIWidget(*gW);
        gLabelX = ImGui::GetItemRectMin().x;  // last item = the name label
    }
    ImGui::End();
    ImGui::Render();
}

static void frames(int n) {
    for (int i = 0; i < n; ++i) frame();
}

// The widget is created with fw = 200, so its frame is 200 wide and the
// usable span (inside kGrabPad = 2) is 196.
static float posToX(float pos01) { return gItemPos.x + 2.0f + pos01 * 196.0f; }
static float midY() { return gItemPos.y + gFrameH * 0.5f; }

static void mouseTo(float pos01) {
    ImGui::GetIO().AddMousePosEvent(posToX(pos01), midY());
    frames(2);  // trickled events need a frame to land + one to settle
}

static void press() { ImGui::GetIO().AddMouseButtonEvent(0, true); frames(2); }
static void release() { ImGui::GetIO().AddMouseButtonEvent(0, false); frames(2); }
static void mod(ImGuiKey m, bool down) {
    ImGui::GetIO().AddKeyEvent(m, down);
    frames(2);
}
static void typeText(char const* s) {
    for (; *s; ++s) {
        ImGui::GetIO().AddInputCharacter((unsigned)*s);
        frames(2);
    }
}
static void pressKey(ImGuiKey k) {
    ImGui::GetIO().AddKeyEvent(k, true);
    frame();
    ImGui::GetIO().AddKeyEvent(k, false);
    frames(2);
}

// Space the tests out past the double-click window so a test's click
// never reads as a double-click on the previous test's.
static void settle() { frames(30); }

static bridge::UIWidget makeWidget(bridge::UIWidgetKind kind, double lo,
                                   double hi, double v0, double v1) {
    bridge::UIWidget w;
    w.id = 1;
    w.kind = kind;
    w.name = "s";
    w.spec.lo = lo;
    w.spec.hi = hi;
    w.spec.init = v0;
    w.fw = 200.0f;
    w.values = {v0};
    if (kind == bridge::UIWidgetKind::Range) w.values.push_back(v1);
    return w;
}

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(400, 300);
    io.IniFilename = nullptr;
    // Deterministic macOS behavior regardless of build platform: the
    // physical Cmd key arrives as ImGuiMod_Super from the backend and
    // io.AddKeyEvent swaps it to KeyCtrl.
    io.ConfigMacOSXBehaviors = true;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();

    // ---- slider: plain drag is absolute ---------------------------------
    {
        auto w = makeWidget(bridge::UIWidgetKind::Slider, 0, 100, 25, 0);
        gW = &w;
        frames(3);
        mouseTo(0.5f);
        press();
        checkNear("slider click jumps to position", w.values[0], 50.0);
        mouseTo(0.75f);
        checkNear("slider drag follows", w.values[0], 75.0);

        // ---- option = fine (x0.1), anchored at the switch ----------------
        mod(ImGuiMod_Alt, true);
        mouseTo(0.95f);  // +0.2 of travel at 1/10 rate -> +0.02
        checkNear("option fine drag", w.values[0], 77.0, 0.75);
        // option+cmd = ultra fine (x0.01); Cmd arrives as Mod_Super
        mod(ImGuiMod_Super, true);
        mouseTo(0.45f);  // -0.5 of travel at 1/100 -> -0.005
        checkNear("option+cmd ultra fine drag", w.values[0], 76.5, 0.75);
        mod(ImGuiMod_Super, false);
        mod(ImGuiMod_Alt, false);
        release();
        check(w.gestureEnded, "drag release ends the gesture");
        settle();

        // ---- hover keys arrive as typed characters ------------------------
        typeText("3");
        checkNear("digit key 3 sets position 0.3", w.values[0], 30.0, 1e-3);
        typeText("9");
        checkNear("digit key 9 sets position 0.9", w.values[0], 90.0, 1e-3);
        typeText("c");
        checkNear("c key centers", w.values[0], 50.0, 1e-3);
        typeText(".");
        checkNear("period steps up 0.05", w.values[0], 55.0, 1e-3);
        typeText("9");  // leave at 0.9 for the text-entry test below
        settle();

        // ---- cmd-click text entry ----------------------------------------
        mouseTo(0.5f);
        mod(ImGuiMod_Super, true);   // physical cmd -> io.KeyCtrl
        press();
        release();
        mod(ImGuiMod_Super, false);
        check(io.WantTextInput, "cmd-click opens text entry");
        frames(5);
        check(io.WantTextInput, "text entry stays open");
        typeText("62.5");
        checkNear("typing does not apply yet", w.values[0], 90.0, 1e-3);
        pressKey(ImGuiKey_Enter);
        checkNear("enter commits the typed value", w.values[0], 62.5, 1e-6);
        check(!io.WantTextInput, "enter closes text entry");
        settle();
        gW = nullptr;
    }

    // ---- range slider -----------------------------------------------------
    {
        auto w = makeWidget(bridge::UIWidgetKind::Range, 0, 100, 20, 60);
        gW = &w;
        frames(3);

        // shift-drag adjusts the nearer end
        mouseTo(0.9f);
        mod(ImGuiMod_Shift, true);
        press();
        // The untouched end still round-trips through a float unmap/map,
        // so allow for single precision.
        checkNear("shift-drag lo end unchanged", w.values[0], 20.0, 1e-4);
        checkNear("shift-drag moves nearer end", w.values[1], 90.0);
        release();
        mod(ImGuiMod_Shift, false);
        settle();

        // plain drag sweeps out a new range
        mouseTo(0.3f);
        press();
        mouseTo(0.5f);
        checkNear("sweep sets lo", w.values[0], 30.0);
        checkNear("sweep sets hi", w.values[1], 50.0);
        release();
        settle();

        // option-drag moves the whole range, width preserved
        mod(ImGuiMod_Alt, true);
        mouseTo(0.7f);
        press();
        checkNear("option-drag moves lo", w.values[0], 60.0);
        checkNear("option-drag moves hi", w.values[1], 80.0);
        release();
        mod(ImGuiMod_Alt, false);
        settle();

        // cmd-click on the right half edits the hi end
        float labelXNormal = gLabelX;
        mouseTo(0.9f);
        mod(ImGuiMod_Super, true);
        press();
        release();
        mod(ImGuiMod_Super, false);
        check(io.WantTextInput, "range cmd-click opens text entry");
        typeText("95");
        pressKey(ImGuiKey_Enter);
        checkNear("range hi commits typed value", w.values[1], 95.0, 1e-6);
        checkNear("range lo untouched by hi edit", w.values[0], 60.0);
        settle();

        // cmd-click on the left half edits the lo end -- and the control
        // must not collapse to half width while the edit is open (the
        // InputText's half-rect ItemSize once shifted the label left).
        mouseTo(0.2f);
        mod(ImGuiMod_Super, true);
        press();
        release();
        mod(ImGuiMod_Super, false);
        check(io.WantTextInput, "range lo cmd-click opens text entry");
        checkNear("label holds position during lo edit", gLabelX,
                  labelXNormal, 0.5);
        typeText("10");
        pressKey(ImGuiKey_Enter);
        checkNear("range lo commits typed value", w.values[0], 10.0, 1e-6);
        checkNear("range hi untouched by lo edit", w.values[1], 95.0, 1e-6);
        checkNear("label holds position after lo edit", gLabelX,
                  labelXNormal, 0.5);
        settle();
        gW = nullptr;
    }

    // ---- xy pad sizes on both axes (fw x fh), not a forced square --------
    // A square pad ignored fh, so arrange mode's grip only responded to
    // horizontal drags. With fh honoured, y maps over the shorter height.
    {
        auto w = makeWidget(bridge::UIWidgetKind::XY, 0, 100, 0, 0);
        w.values.push_back(0.0);   // XY needs both axes
        w.fw = 200.0f;
        w.fh = 100.0f;             // deliberately not square
        gW = &w;
        frames(3);
        ImGui::GetIO().AddMousePosEvent(gItemPos.x + 150.0f,
                                        gItemPos.y + 25.0f);
        frames(2);
        press();
        checkNear("xy x maps over fw", w.values[0], 75.0);
        // 1 - 25/100 = 0.75. A square pad would give 1 - 25/200 = 0.875.
        checkNear("xy y maps over fh, not fw", w.values[1], 0.75, 0.02);
        release();
        settle();
        gW = nullptr;
    }

    // ---- arrange overlay: the whole widget is grabbable, not just its
    // label. A disabled item still claims HoveredId (imgui.cpp's
    // ItemHoverable sets it before the disabled early-out), which used to
    // block the move overlay everywhere the widget drew an item.
    {
        auto w = makeWidget(bridge::UIWidgetKind::Slider, 0, 100, 50, 0);
        gW = &w;
        gArrange = true;
        settle();

        auto pressAt = [](float x, float y) {
            ImGui::GetIO().AddMousePosEvent(x, y);
            frames(3);
            press();
        };
        float midx = gGroupMin.x + 40.0f;             // over the slider track
        float midy = (gGroupMin.y + gGroupMax.y) * 0.5f;
        float gripx = gGroupMax.x - 4.0f, gripy = gGroupMax.y - 4.0f;

        // Over the control itself -- the case that was broken.
        pressAt(midx, midy);
        check(gMoveActive, "arrange: move overlay grabbable over the control");
        check(!gSizeActive, "arrange: grip not grabbed mid-widget");
        release();
        settle();

        // The grip corner belongs to the grip, not the move overlay.
        pressAt(gripx, gripy);
        check(gSizeActive, "arrange: resize grip grabbable in its corner");
        check(!gMoveActive, "arrange: move overlay yields the grip corner");
        release();
        settle();

        // Without AllowOverlap the disabled widget keeps HoveredId: this is
        // the reported bug, and proves the checks above are not vacuous.
        gArrangeAllowOverlap = false;
        pressAt(midx, midy);
        check(!gMoveActive,
              "arrange: (regression probe) disabled widget blocks the overlay");
        release();
        gArrangeAllowOverlap = true;

        gArrange = false;
        gW = nullptr;
        settle();
    }

    ImGui::DestroyContext();
    if (failures) {
        std::printf("%d widget test(s) FAILED\n", failures);
        return 1;
    }
    std::printf("all widget tests passed\n");
    return 0;
}
