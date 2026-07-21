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

#include "widget_draw.hpp"

#include "tzpl_ui_state.hpp"

#include "imgui.h"
#include "imgui_internal.h"  // TempInputIsActive: cmd-click text entry

#include <algorithm>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <vector>

// All widget interaction happens with UIState::mtx held (GUI thread only).
// Never call into the VM from here (lock order: see tzpl_ui_state.hpp).

using bridge::UIWidget;
using bridge::UIWidgetKind;

static void markDirty(UIWidget& w) {
    w.dirtyEngine = true;
    w.dirtyCallback = true;
}

// Perform-mode magnification: explicit frames and per-kind default sizes
// multiply by this. Positions are scaled by the panel drawing code.
static float gUIScale = 1.0f;

void setUIDrawScale(float s) { gUIScale = s > 0.0f ? s : 1.0f; }

static float wFrameW(UIWidget const& w, float def) {
    return (w.fw > 0.0f ? w.fw : def) * gUIScale;
}
static float wFrameH(UIWidget const& w, float def) {
    return (w.fh > 0.0f ? w.fh : def) * gUIScale;
}

// One history commit per continuous interaction: flag the release edge of
// the last-drawn item (sliders, drags, checkboxes).
static void markGestureEdges(UIWidget& w) {
    if (ImGui::IsItemDeactivatedAfterEdit()) w.gestureEnded = true;
}

// Hover gestures (sliders and xy pads), all in UNMAPPED 0..1 position
// space unless noted:
//   c = center (0.5)     [ = lo (0.0)      ] = hi (1.0)
//   1..9 = position 0.1..0.9 (sliders only)
//   r = uniform random   j = jitter by uniform(-0.05, +0.05), bouncing
//   J = fine jitter by uniform(-0.005, +0.005)
//   , / . = step down / up by 0.05
//   - = negate the MAPPED value    / = its reciprocal (when in range)
//   wheel = step by 0.01 per tick (up = higher; on an xy pad the
//           horizontal wheel axis drives X)
// Keys arrive as TYPED CHARACTERS (ImGui's character queue, drained and
// consumed while the item is hovered), so they follow the keyboard
// layout and shift pairs like j/J come for free. Auto-repeat makes a
// held j walk, and key and wheel bursts alike coalesce into ONE history
// entry, committed when the adjustments go idle. Call right after the
// hoverable item.

// A hovered gesture widget OWNS the keyboard: typed characters act on
// the widget and go nowhere else -- not to focused cell editors, not to
// key bindings. To type, hover somewhere else. Widgets draw at various
// points in the frame (some after the editors have already read the
// character queue), so ownership is asserted a frame ahead:
// uiHoverKeysNewFrame() runs right after ImGui::NewFrame() and, when a
// widget was hovered last frame, moves the whole character queue into a
// private buffer before any editor can see it. The one exception is an
// open cmd-click value edit (temp input) -- that is the widget system's
// own text entry, so hover gestures stand down until it closes.
static bool gHoverOwnerLastFrame = false;
static bool gHoverOwnerThisFrame = false;
static ImVector<ImWchar> gStolenChars;

void uiHoverKeysNewFrame() {
    gHoverOwnerLastFrame = gHoverOwnerThisFrame;
    gHoverOwnerThisFrame = false;
    gStolenChars.resize(0);
    ImGuiContext& g = *GImGui;
    bool tempInputActive = g.TempInputId != 0 && g.ActiveId == g.TempInputId;
    if (gHoverOwnerLastFrame && !tempInputActive) {
        gStolenChars = ImGui::GetIO().InputQueueCharacters;
        ImGui::GetIO().InputQueueCharacters.resize(0);
    }
}

// Burst coalesce timer + hover gate. Runs the timer every frame
// (hovered or not); returns true when hover adjustments may fire, and
// claims keyboard ownership for the next frame when hovered.
static bool hoverAdjustBegin(UIWidget& w) {
    if (w.gestureActive && w.wheelTime > 0.0
        && ImGui::GetTime() - w.wheelTime > 0.4) {
        w.gestureActive = false;
        w.wheelTime = 0.0;
        w.gestureEnded = true;
    }
    ImGuiContext& g = *GImGui;
    if (g.TempInputId != 0 && g.ActiveId == g.TempInputId) return false;
    bool hovered = ImGui::IsItemHovered();
    if (hovered) gHoverOwnerThisFrame = true;
    return hovered;
}

static float hoverUniform(float lo, float hi) {
    static std::minstd_rand rng{std::random_device{}()};
    return lo + (hi - lo) * std::uniform_real_distribution<float>{}(rng);
}

// Bounce off the ends rather than clamping, so jitter keeps its energy
// at the rails: 0.02 - 0.03 -> 0.01, 1.03 -> 0.97.
static float hoverBounce(float p) {
    if (p < 0.0f) return -p;
    if (p > 1.0f) return 2.0f - p;
    return p;
}

static void hoverOwnWheel() {
    // Own BOTH wheel axes while hovered so trackpad scroll gestures
    // can't slide the window contents around underneath.
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelX);
}

// Feed the hovered item every typed character: the ones stolen at frame
// start (see uiHoverKeysNewFrame) plus any still in the live queue (the
// first frame of a hover, before ownership was asserted). `handle` is a
// per-widget switch over each character. At most one widget is hovered,
// so at most one drains.
template <typename Handle>
static void hoverChars(Handle&& handle) {
    for (int i = 0; i < gStolenChars.Size; ++i)
        handle((unsigned)gStolenChars[i]);
    gStolenChars.resize(0);
    ImVector<ImWchar>& q = ImGui::GetIO().InputQueueCharacters;
    for (int i = 0; i < q.Size; ++i) handle((unsigned)q[i]);
    q.resize(0);
}

static void hoverAdjustSlider(UIWidget& w) {
    if (!hoverAdjustBegin(w)) return;

    auto setPos = [&](float pos) {
        w.values[0] = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
        markDirty(w);
        w.gestureActive = true;  // ends via the idle timer
        w.wheelTime = ImGui::GetTime();
    };

    hoverChars([&](unsigned c) {
        // Recompute per character so a burst chains correctly.
        float pos = static_cast<float>(w.spec.unmap(w.values[0]));
        double v = w.values[0];
        switch (c) {
            case 'c': case 'C': setPos(0.5f); break;
            case '[': setPos(0.0f); break;
            case ']': setPos(1.0f); break;
            case 'r': case 'R': setPos(hoverUniform(0.0f, 1.0f)); break;
            case 'j': setPos(hoverBounce(pos + hoverUniform(-0.05f, 0.05f))); break;
            case 'J': setPos(hoverBounce(pos + hoverUniform(-0.005f, 0.005f))); break;
            case ',': setPos(pos - 0.05f); break;
            case '.': setPos(pos + 0.05f); break;
            // z / i / - / slash work in MAPPED value space: zero, the
            // spec's init, negation, and the reciprocal (the latter two
            // only when the result stays in range). Clamping before
            // unmapping keeps exponential warps away from log(0).
            case 'z': case 'Z':
                setPos((float)w.spec.unmap(w.spec.clamp(0.0)));
                break;
            case 'i': case 'I':
                setPos((float)w.spec.unmap(w.spec.clamp(w.spec.init)));
                break;
            case '-':
                if (w.spec.contains(-v)) setPos((float)w.spec.unmap(-v));
                break;
            case '/':
                if (v != 0.0 && w.spec.contains(1.0 / v))
                    setPos((float)w.spec.unmap(1.0 / v));
                break;
            default:
                // 1..9 jump to position 0.1..0.9.
                if (c >= '1' && c <= '9') setPos(0.1f * (float)(c - '0'));
                break;
        }
    });

    // Positive io.MouseWheel is a scroll-up gesture reading as "push
    // the content up"; for a value, pushing up should RAISE it.
    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        float pos = static_cast<float>(w.spec.unmap(w.values[0]));
        setPos(pos - wheel * 0.01f);
    }
    hoverOwnWheel();
}

static void hoverAdjustXY(UIWidget& w) {
    if (!hoverAdjustBegin(w)) return;

    auto stamp = [&] {
        markDirty(w);
        w.gestureActive = true;
        w.wheelTime = ImGui::GetTime();
    };
    auto setX = [&](float p) {
        w.values[0] = w.spec.map(std::clamp(p, 0.0f, 1.0f));
        stamp();
    };
    auto setY = [&](float p) {
        w.values[1] = w.spec2.map(std::clamp(p, 0.0f, 1.0f));
        stamp();
    };
    hoverChars([&](unsigned c) {
        float x = static_cast<float>(w.spec.unmap(w.values[0]));
        float y = static_cast<float>(w.spec2.unmap(w.values[1]));
        auto jitter = [&](float d) {
            setX(hoverBounce(x + hoverUniform(-d, d)));
            setY(hoverBounce(y + hoverUniform(-d, d)));
        };
        switch (c) {
            case 'c': case 'C': setX(0.5f); setY(0.5f); break;
            case '[': setX(0.0f); setY(0.0f); break;
            case ']': setX(1.0f); setY(1.0f); break;
            case 'r': case 'R':
                setX(hoverUniform(0.0f, 1.0f));
                setY(hoverUniform(0.0f, 1.0f));
                break;
            case 'j': jitter(0.05f); break;
            case 'J': jitter(0.005f); break;
            case 'z': case 'Z':
                setX((float)w.spec.unmap(w.spec.clamp(0.0)));
                setY((float)w.spec2.unmap(w.spec2.clamp(0.0)));
                break;
            case 'i': case 'I':
                setX((float)w.spec.unmap(w.spec.clamp(w.spec.init)));
                setY((float)w.spec2.unmap(w.spec2.clamp(w.spec2.init)));
                break;
            default: break;
        }
    });

    // Vertical wheel drives Y (push up = higher), horizontal drives X
    // (push right = higher). The two axes' deltas arrive with opposite
    // signs relative to finger direction.
    float x = static_cast<float>(w.spec.unmap(w.values[0]));
    float y = static_cast<float>(w.spec2.unmap(w.values[1]));
    float wheelY = ImGui::GetIO().MouseWheel;
    if (wheelY != 0.0f) setY(y - wheelY * 0.01f);
    float wheelX = ImGui::GetIO().MouseWheelH;
    if (wheelX != 0.0f) setX(x + wheelX * 0.01f);
    hoverOwnWheel();
}

// ---------------------------------------------------------------------------
// Custom slider / range slider
//
// Both render as a frame with a filled bar, a graduated tick scale on
// the background while hovered or active, and the value(s) as centered
// text. The drag has fine-control modes: option = 1/10 rate, option+cmd
// = 1/100, anchored so switching modes mid-drag doesn't jump.
// ---------------------------------------------------------------------------

static const float kGrabPad = 2.0f;

// 0 = absolute positioning, 1 = fine (x0.1), 2 = ultra fine (x0.01).
// NOTE on macOS modifiers: ImGui swaps Cmd<->Ctrl (io.KeyCtrl is the
// PHYSICAL Cmd key), and physical ctrl+click arrives as a right-click,
// so ctrl is unusable for slider gestures. Option = io.KeyAlt,
// option+cmd = KeyAlt+KeyCtrl.
static int fineControlMode() {
    ImGuiIO& io = ImGui::GetIO();
    return io.KeyAlt ? (io.KeyCtrl ? 2 : 1) : 0;
}

// Fine-drag anchor. One drag is active at a time, so shared state is fine.
struct SliderDrag {
    float anchorPos = 0.0f;    // grab position 0..1 at the anchor
    float anchorMouse = 0.0f;  // mouse x at the anchor
    int fine = 0;
};
static SliderDrag gSliderDrag;
static float gRangeAnchor = 0.0f;  // plain-drag sweep origin (position 0..1)

// Submit the slider/range item and route activation the way SliderScalar
// does. The ItemAdd MUST happen before TempInputScalar every frame --
// its InputText runs as a merged item that reuses this item's entry --
// and cmd-click routes to text entry (ImGui reports macOS Cmd as
// KeyCtrl). Option is excluded from that test so an option+cmd
// ultra-fine drag can start without opening the text editor.
struct SliderItem {
    ImRect bb;
    ImGuiID id = 0;
    bool ok = false;         // ItemAdd succeeded (not clipped)
    bool hovered = false;
    bool tempInput = false;  // a text edit owns the item this frame
    bool active = false;     // a mouse drag is in progress
    bool activated = false;  // ... and started this frame
};

static SliderItem sliderItemBehavior(char const* label, UIWidget const& w) {
    SliderItem it;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return it;
    ImGuiContext& g = *GImGui;

    ImGui::SetNextItemWidth((w.fw > 0.0f ? w.fw : -140.0f) * gUIScale);
    float width = std::max(ImGui::CalcItemWidth(), 40.0f);
    float height = ImGui::GetFrameHeight();
    ImVec2 origin = window->DC.CursorPos;
    it.bb = ImRect(origin, ImVec2(origin.x + width, origin.y + height));
    it.id = window->GetID(label);
    ImGui::ItemSize(it.bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(it.bb, it.id, &it.bb, ImGuiItemFlags_Inputable))
        return it;
    it.ok = true;

    it.hovered = ImGui::ItemHoverable(it.bb, it.id, g.LastItemData.InFlags);
    it.tempInput = ImGui::TempInputIsActive(it.id);
    if (!it.tempInput) {
        bool clicked = it.hovered
            && ImGui::IsMouseClicked(0, ImGuiInputFlags_None, it.id);
        if (clicked) {
            ImGui::SetKeyOwner(ImGuiKey_MouseLeft, it.id);
            if (g.IO.KeyCtrl && !g.IO.KeyAlt) {
                it.tempInput = true;
            } else {
                ImGui::SetActiveID(it.id, window);
                ImGui::SetFocusID(it.id, window);
                ImGui::FocusWindow(window);
                g.ActiveIdUsingNavDirMask |=
                    (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            }
        }
    }
    if (!it.tempInput && g.ActiveId == it.id) {
        if (g.ActiveIdSource == ImGuiInputSource_Mouse && !g.IO.MouseDown[0]) {
            ImGui::ClearActiveID();
        } else {
            it.active = true;
            it.activated = g.ActiveIdIsJustActivated;
        }
    }
    return it;
}

static void drawTick(ImDrawList* dl, ImRect const& z, float lenScale,
                     float pos, ImU32 color) {
    float x = z.Min.x + pos * z.GetWidth();
    dl->AddLine(ImVec2(x, z.Min.y),
                ImVec2(x, z.Min.y + z.GetHeight() * lenScale), color, 1.0f);
}

// Graduated scale on the slider background. Exponential warps get
// log-spaced ticks (1..9 per decade, longer at 1 and 5); everything
// else gets a decimal hierarchy (longest at 0, then every 10th, 5th,
// 1st step) with the step sized so the scale stays readable.
static void drawTickMarks(ImDrawList* dl, bridge::UISpec const& spec,
                          ImRect const& z) {
    double lo = spec.lo, hi = spec.hi;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi)) return;
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text, 0.35f);

    if (spec.warp == bridge::UIWarp::Exponential && lo > 0.0 && hi > 0.0) {
        int lowOrder = (int)std::floor(std::log10(lo));
        int highOrder = (int)std::floor(std::log10(hi));
        for (int i = lowOrder; i <= highOrder; ++i) {
            double decade = std::pow(10.0, i);
            for (int j = 1; j <= 9; ++j) {
                double v = decade * j;
                if (v <= lo || v >= hi) continue;
                float pos = (float)spec.unmap(v);
                // Drop ticks that would fuse with their neighbor.
                float next = (float)spec.unmap(decade * (j + 1));
                if (std::fabs(next - pos) < 0.005f) continue;
                float lenScale = j == 1 ? 0.75f : j == 5 ? 0.525f : 0.3f;
                drawTick(dl, z, lenScale, pos, color);
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
            float lenScale = index == 0        ? 1.0f
                           : index % 10 == 0   ? 0.75f
                           : index % 5 == 0    ? 0.525f
                                               : 0.3f;
            drawTick(dl, z, lenScale, (float)spec.unmap(v), color);
        }
    }
}

static void drawSlider(UIWidget& w) {
    std::string label = std::string("##") + w.name;
    ImGuiStyle const& style = ImGui::GetStyle();
    SliderItem it = sliderItemBehavior(label.c_str(), w);
    if (!it.ok) return;
    ImRect const& bb = it.bb;

    // Cmd-click text entry edits the DISPLAYED (mapped) value, while the
    // drag operates on the 0..1 warp position. Nothing is applied until
    // Enter / focus loss -- the per-keystroke edits ImGui reports are
    // discarded (v reseeds from the widget every frame; on the commit
    // frame the deactivating InputText applies its final buffer to v).
    if (it.tempInput) {
        double v = w.values[0];
        ImGui::TempInputScalar(bb, it.id, label.c_str(),
                               ImGuiDataType_Double, &v, "%.4g");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            w.values[0] = w.spec.map(w.spec.unmap(v));  // clamp + warp snap
            markDirty(w);
            w.gestureEnded = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(w.name.c_str());
        return;
    }

    float minx = bb.Min.x + kGrabPad;
    float usable = std::max(bb.GetWidth() - 2.0f * kGrabPad, 1.0f);

    if (it.active) {
        ImGuiIO& io = ImGui::GetIO();
        int fine = fineControlMode();
        if (it.activated || fine != gSliderDrag.fine) {
            // (Re-)anchor at the current value, so entering or leaving a
            // fine mode mid-drag never jumps.
            gSliderDrag = {(float)w.spec.unmap(w.values[0]), io.MousePos.x,
                           fine};
        }
        float pos;
        if (fine) {
            float rate = fine == 1 ? 0.1f : 0.01f;
            float anchorPx = minx + gSliderDrag.anchorPos * usable;
            pos = (anchorPx + rate * (io.MousePos.x - gSliderDrag.anchorMouse)
                   - minx) / usable;
        } else {
            pos = (io.MousePos.x - minx) / usable;
        }
        double v = w.spec.map(std::clamp(pos, 0.0f, 1.0f));
        if (v != w.values[0]) {
            w.values[0] = v;
            markDirty(w);
            ImGui::MarkItemEdited(it.id);
        }
        w.gestureActive = true;
        w.wheelTime = 0.0;  // a drag owns the gesture; hover timer off
    } else if (w.gestureActive && w.wheelTime == 0.0) {
        w.gestureActive = false;
        w.gestureEnded = true;
    }

    auto* dl = ImGui::GetWindowDrawList();
    ImU32 frameCol = ImGui::GetColorU32(it.active    ? ImGuiCol_FrameBgActive
                                        : it.hovered ? ImGuiCol_FrameBgHovered
                                                     : ImGuiCol_FrameBg);
    ImGui::RenderFrame(bb.Min, bb.Max, frameCol, true, style.FrameRounding);
    float gx = minx + (float)w.spec.unmap(w.values[0]) * usable;
    dl->AddRectFilled(ImVec2(minx, bb.Min.y + kGrabPad),
                      ImVec2(gx, bb.Max.y - kGrabPad),
                      ImGui::GetColorU32(it.active ? ImGuiCol_SliderGrabActive
                                                   : ImGuiCol_SliderGrab),
                      style.GrabRounding);
    if (it.hovered || it.active) {
        ImRect z = bb;
        z.Expand(-kGrabPad);
        drawTickMarks(dl, w.spec, z);
    }
    char display[64];
    std::snprintf(display, sizeof(display), "%.4g", w.values[0]);
    ImGui::RenderTextClipped(bb.Min, bb.Max, display, nullptr, nullptr,
                             ImVec2(0.5f, 0.5f));
    hoverAdjustSlider(w);

    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

// Translate a whole range (positions 0..1) so its midpoint tracks `pos`,
// pinning at the rails so the width is preserved.
static void moveRange(float pos, float& lo, float& hi) {
    float range = hi - lo;
    float adjust = pos - (hi + lo) * 0.5f;
    if (lo + adjust < 0.0f) {
        lo = 0.0f;
        hi = range;
    } else if (hi + adjust > 1.0f) {
        lo = 1.0f - range;
        hi = 1.0f;
    } else {
        lo += adjust;
        hi += adjust;
    }
}

// Move whichever end of the range is nearer to `pos`.
static void adjustRangeEnd(float pos, float& lo, float& hi) {
    if (std::fabs(pos - lo) < std::fabs(pos - hi)) lo = pos;
    else hi = pos;
    if (hi < lo) std::swap(lo, hi);
}

// Range hover gestures, mirroring the slider's where they make sense:
// keys that set one position act on both ends (collapsing the range),
// [ / ] send one end to the rail, , / . and the wheel slide the whole
// range (width preserved), - / slash negate / take reciprocals of both
// ends (re-ordered).
static void hoverAdjustRange(UIWidget& w) {
    if (!hoverAdjustBegin(w)) return;

    auto stamp = [&] {
        markDirty(w);
        w.gestureActive = true;
        w.wheelTime = ImGui::GetTime();
    };
    auto setPos = [&](float plo, float phi) {
        if (plo > phi) std::swap(plo, phi);
        w.values[0] = w.spec.map(std::clamp(plo, 0.0f, 1.0f));
        w.values[1] = w.spec.map(std::clamp(phi, 0.0f, 1.0f));
        stamp();
    };
    auto setVal = [&](double lo, double hi) {
        if (lo > hi) std::swap(lo, hi);
        w.values[0] = lo;
        w.values[1] = hi;
        stamp();
    };
    // , / . and the wheel slide the whole range (width preserved).
    auto slide = [&](float d) {
        float a = (float)w.spec.unmap(w.values[0]);
        float b = (float)w.spec.unmap(w.values[1]);
        moveRange((a + b) * 0.5f + d, a, b);
        setPos(a, b);
    };

    hoverChars([&](unsigned c) {
        float ulo = (float)w.spec.unmap(w.values[0]);
        float uhi = (float)w.spec.unmap(w.values[1]);
        double lo = w.values[0], hi = w.values[1];
        switch (c) {
            case 'c': case 'C': setPos(0.5f, 0.5f); break;
            case '[': setPos(0.0f, uhi); break;
            case ']': setPos(ulo, 1.0f); break;
            case 'r': case 'R':
                setPos(hoverUniform(0.0f, 1.0f), hoverUniform(0.0f, 1.0f));
                break;
            case 'j':
                setPos(hoverBounce(ulo + hoverUniform(-0.05f, 0.05f)),
                       hoverBounce(uhi + hoverUniform(-0.05f, 0.05f)));
                break;
            case 'J':
                setPos(hoverBounce(ulo + hoverUniform(-0.005f, 0.005f)),
                       hoverBounce(uhi + hoverUniform(-0.005f, 0.005f)));
                break;
            case 'z': case 'Z':
                if (w.spec.contains(0.0)) setVal(0.0, 0.0);
                break;
            case 'i': case 'I': {
                double init = w.spec.clamp(w.spec.init);
                setVal(init, init);
            }   break;
            case '-':
                if (w.spec.contains(-lo) && w.spec.contains(-hi))
                    setVal(-hi, -lo);
                break;
            case '/':
                if (lo != 0.0 && hi != 0.0 && w.spec.contains(1.0 / lo)
                    && w.spec.contains(1.0 / hi))
                    setVal(1.0 / lo, 1.0 / hi);
                break;
            case ',': slide(-0.05f); break;
            case '.': slide(0.05f); break;
            default: break;
        }
    });

    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) slide(-wheel * 0.01f);
    hoverOwnWheel();
}

static void drawRange(UIWidget& w) {
    std::string label = std::string("##") + w.name;
    ImGuiStyle const& style = ImGui::GetStyle();
    // Which end an in-progress cmd-click text edit targets. One temp
    // input exists at a time, so shared state is fine; latch it on the
    // frame the edit begins (the click frame).
    static int editEnd = 0;
    bool wasEditing = ImGui::TempInputIsActive(ImGui::GetID(label.c_str()));

    SliderItem it = sliderItemBehavior(label.c_str(), w);
    if (!it.ok) return;
    ImRect const& bb = it.bb;
    float halfx = bb.Min.x + bb.GetWidth() * 0.5f;
    ImRect loBB(bb.Min, ImVec2(halfx, bb.Max.y));
    ImRect hiBB(ImVec2(halfx, bb.Min.y), bb.Max);

    // Cmd-click text entry edits the end whose half was clicked (left =
    // lo, right = hi), clamped so the ends can't cross; commits on
    // Enter / focus loss like the slider.
    if (it.tempInput) {
        if (!wasEditing)
            editEnd = ImGui::GetIO().MousePos.x < halfx ? 0 : 1;
        // Keep the whole control visible while one end is edited: the
        // frame and the other end's value stay drawn; the InputText
        // covers only the edited half.
        ImGui::RenderFrame(bb.Min, bb.Max,
                           ImGui::GetColorU32(ImGuiCol_FrameBg), true,
                           style.FrameRounding);
        int other = 1 - editEnd;
        char display[64];
        std::snprintf(display, sizeof(display), "%.4g",
                      w.values[(size_t)other]);
        ImRect const& otherBB = other == 0 ? loBB : hiBB;
        ImGui::RenderTextClipped(otherBB.Min, otherBB.Max, display, nullptr,
                                 nullptr, ImVec2(0.5f, 0.5f));

        double v = w.values[(size_t)editEnd];
        double cmin = editEnd == 0 ? w.spec.lo : w.values[0];
        double cmax = editEnd == 0 ? w.values[1] : w.spec.hi;
        ImGui::TempInputScalar(editEnd == 0 ? loBB : hiBB, it.id,
                               label.c_str(), ImGuiDataType_Double, &v,
                               "%.4g", &cmin, &cmax);
        // The InputText re-ran ItemSize with its half-width rect; restore
        // the full-width line advance so the SameLine label below lands
        // where it does on every other frame.
        ImGui::GetCurrentWindow()->DC.CursorPosPrevLine.x = bb.Max.x;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            w.values[(size_t)editEnd] = w.spec.map(w.spec.unmap(v));
            if (w.values[0] > w.values[1])
                std::swap(w.values[0], w.values[1]);
            markDirty(w);
            w.gestureEnded = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(w.name.c_str());
        return;
    }

    float minx = bb.Min.x + kGrabPad;
    float usable = std::max(bb.GetWidth() - 2.0f * kGrabPad, 1.0f);

    if (it.active) {
        ImGuiIO& io = ImGui::GetIO();
        float pos = std::clamp((io.MousePos.x - minx) / usable, 0.0f, 1.0f);
        float ulo = (float)w.spec.unmap(w.values[0]);
        float uhi = (float)w.spec.unmap(w.values[1]);
        if (it.activated) gRangeAnchor = pos;
        // Modifiers are read every frame, so a drag can switch modes
        // mid-gesture (start a sweep, then option-slide it into place).
        // Shift (not ctrl/cmd) adjusts ends: physical ctrl+click arrives
        // as a right-click on macOS, and cmd-click opens text entry.
        if (io.KeyAlt) {
            moveRange(pos, ulo, uhi);           // slide the whole range
        } else if (io.KeyShift) {
            adjustRangeEnd(pos, ulo, uhi);      // adjust the nearer end
        } else {
            ulo = std::min(gRangeAnchor, pos);  // sweep out a new range
            uhi = std::max(gRangeAnchor, pos);
        }
        double lo = w.spec.map(ulo), hi = w.spec.map(uhi);
        if (lo != w.values[0] || hi != w.values[1]) {
            w.values[0] = lo;
            w.values[1] = hi;
            markDirty(w);
            ImGui::MarkItemEdited(it.id);
        }
        w.gestureActive = true;
        w.wheelTime = 0.0;
    } else if (w.gestureActive && w.wheelTime == 0.0) {
        w.gestureActive = false;
        w.gestureEnded = true;
    }

    auto* dl = ImGui::GetWindowDrawList();
    ImU32 frameCol = ImGui::GetColorU32(it.active    ? ImGuiCol_FrameBgActive
                                        : it.hovered ? ImGuiCol_FrameBgHovered
                                                     : ImGuiCol_FrameBg);
    ImGui::RenderFrame(bb.Min, bb.Max, frameCol, true, style.FrameRounding);
    // The bar spans lo..hi; the half-pixel outsets keep a collapsed
    // range visible as a thin line.
    float x0 = minx + (float)w.spec.unmap(w.values[0]) * usable - 0.5f;
    float x1 = minx + (float)w.spec.unmap(w.values[1]) * usable + 0.5f;
    dl->AddRectFilled(ImVec2(x0, bb.Min.y + kGrabPad),
                      ImVec2(x1, bb.Max.y - kGrabPad),
                      ImGui::GetColorU32(it.active ? ImGuiCol_SliderGrabActive
                                                   : ImGuiCol_SliderGrab),
                      style.GrabRounding);
    if (it.hovered || it.active) {
        ImRect z = bb;
        z.Expand(-kGrabPad);
        drawTickMarks(dl, w.spec, z);
    }
    char display[64];
    std::snprintf(display, sizeof(display), "%.4g", w.values[0]);
    ImGui::RenderTextClipped(loBB.Min, loBB.Max, display, nullptr, nullptr,
                             ImVec2(0.5f, 0.5f));
    std::snprintf(display, sizeof(display), "%.4g", w.values[1]);
    ImGui::RenderTextClipped(hiBB.Min, hiBB.Max, display, nullptr, nullptr,
                             ImVec2(0.5f, 0.5f));
    hoverAdjustRange(w);

    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawNumber(UIWidget& w) {
    std::string label = std::string("##") + w.name;
    double v = w.values[0];
    ImGui::SetNextItemWidth((w.fw > 0.0f ? w.fw : -140.0f) * gUIScale);
    bool typing = ImGui::TempInputIsActive(ImGui::GetID(label.c_str()));
    bool changed = ImGui::DragScalar(label.c_str(), ImGuiDataType_Double, &v,
                                     0.01f, nullptr, nullptr, "%.4g");
    if (!typing) {
        if (changed) {
            w.values[0] = w.spec.clamp(v);
            markDirty(w);
        }
        markGestureEdges(w);
    } else if (ImGui::IsItemDeactivatedAfterEdit()) {
        // Text entry commits once, on Enter / focus loss.
        w.values[0] = w.spec.clamp(v);
        markDirty(w);
        w.gestureEnded = true;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawButton(UIWidget& w) {
    // Momentary/gate: 1 while held, 0 on release; both edges dispatch.
    // A key binding shows as a label hint (### keeps the ImGui id stable).
    std::string label = w.keyChord.empty()
        ? w.name
        : w.name + " [" + w.keyChord + "]###" + w.name;
    ImGui::Button(label.c_str(), ImVec2(wFrameW(w, 120.0f), 0.0f));
    if (ImGui::IsItemActivated()) {
        w.values[0] = 1.0;
        markDirty(w);
    }
    if (ImGui::IsItemDeactivated()) {
        w.values[0] = 0.0;
        markDirty(w);
    }
}

static void drawToggle(UIWidget& w) {
    bool on = w.values[0] != 0.0;
    if (ImGui::Checkbox(w.name.c_str(), &on)) {
        w.values[0] = on ? 1.0 : 0.0;
        markDirty(w);
    }
    markGestureEdges(w);
}

static float ampToDbPos(float amp) {
    // Map amplitude to a 0..1 meter position on a -60..0 dB scale.
    if (amp <= 0.001f) return 0.0f;
    float db = 20.0f * std::log10(amp);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

static void drawMeter(UIWidget& w) {
    float rms = w.values.size() > 0 ? static_cast<float>(w.values[0]) : 0.0f;
    float peak = w.values.size() > 1 ? static_cast<float>(w.values[1]) : 0.0f;

    const float width = wFrameW(w, 200.0f);
    const float height = wFrameH(w, 14.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    float rmsW = ampToDbPos(rms) * width;
    dl->AddRectFilled(origin, ImVec2(origin.x + rmsW, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_PlotHistogram));
    float peakX = origin.x + ampToDbPos(peak) * width;
    ImU32 peakCol = peak >= 1.0f ? IM_COL32(255, 64, 64, 255)
                                 : ImGui::GetColorU32(ImGuiCol_SliderGrab);
    dl->AddLine(ImVec2(peakX, origin.y), ImVec2(peakX, origin.y + height),
                peakCol, 2.0f);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));

    ImGui::SameLine();
    float db = peak > 0.001f ? 20.0f * std::log10(peak) : -60.0f;
    ImGui::Text("%s  %5.1f dB", w.name.c_str(), db);
}

static void drawScope(UIWidget& w) {
    const int displayN = 512;
    int chans = std::max(1, w.scopeChans);
    if (w.scopeChannel >= chans) w.scopeChannel = -1;

    // The ring holds interleaved frames; de-interleave the tail.
    int frames = (int)(w.scopeRing.size() / chans);

    // Trigger on a rising zero crossing of the reference channel (the
    // selected one, or channel 0 in "all" view) so periodic signals hold
    // still; all channels share the trigger to keep phase relationships.
    int refCh = w.scopeChannel < 0 ? 0 : w.scopeChannel;
    auto sample = [&](int frame, int ch) {
        return w.scopeRing[(size_t)frame * chans + ch];
    };
    int start = std::max(0, frames - displayN);
    for (int f = std::max(0, frames - 2 * displayN); f < frames - displayN; ++f) {
        if (sample(f, refCh) <= 0.0f && sample(f + 1, refCh) > 0.0f) {
            start = f;
            break;
        }
    }
    int count = std::min(displayN, frames - start);

    static std::vector<float> lane;  // GUI thread only
    auto plotLane = [&](int ch, float height) {
        lane.resize((size_t)std::max(count, 0));
        for (int f = 0; f < count; ++f) lane[f] = sample(start + f, ch);
        float scopeW = wFrameW(w, 320.0f);
        if (count > 1) {
            ImGui::PlotLines(("##" + w.name + std::to_string(ch)).c_str(),
                             lane.data(), count, 0, nullptr,
                             -1.0f, 1.0f, ImVec2(scopeW, height));
        } else {
            ImGui::Dummy(ImVec2(scopeW, height));
        }
    };

    float scopeH = wFrameH(w, 100.0f);
    ImGui::BeginGroup();
    if (w.scopeChannel < 0) {
        // All channels, stacked lanes sharing one trigger.
        float laneH = std::max(scopeH / (float)chans, 24.0f);
        for (int ch = 0; ch < chans; ++ch) plotLane(ch, laneH);
    } else {
        plotLane(w.scopeChannel, scopeH);
    }
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(w.name.c_str());
    if (chans > 1) {
        // Cycle: all -> 0 -> 1 -> ... -> all
        char label[16];
        if (w.scopeChannel < 0) std::snprintf(label, sizeof(label), "ch: all");
        else std::snprintf(label, sizeof(label), "ch: %d", w.scopeChannel);
        if (ImGui::SmallButton(label)) {
            w.scopeChannel = (w.scopeChannel + 1 < chans) ? w.scopeChannel + 1
                                                          : -1;
        }
    }
    ImGui::EndGroup();
}

static void drawPlot(UIWidget& w) {
    if (w.plotData.size() > 1) {
        float lo = w.plotData[0], hi = w.plotData[0];
        for (float v : w.plotData) {
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (hi == lo) { hi += 1.0f; lo -= 1.0f; }
        float pad = (hi - lo) * 0.05f;
        ImGui::PlotLines((std::string("##") + w.name).c_str(),
                         w.plotData.data(), (int)w.plotData.size(), 0, nullptr,
                         lo - pad, hi + pad,
                         ImVec2(wFrameW(w, 320.0f), wFrameH(w, 100.0f)));
    } else {
        ImGui::Dummy(ImVec2(wFrameW(w, 320.0f), wFrameH(w, 100.0f)));
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawWaveform(UIWidget& w) {
    const float width = wFrameW(w, 320.0f);
    const float height = wFrameH(w, 80.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    int bins = (int)w.waveMin.size();
    if (bins > 0) {
        float midY = origin.y + height * 0.5f;
        float halfH = height * 0.5f;
        ImU32 col = ImGui::GetColorU32(ImGuiCol_PlotLines);
        for (int px = 0; px < (int)width; ++px) {
            int b0 = bins * px / (int)width;
            int b1 = bins * (px + 1) / (int)width;
            if (b1 <= b0) b1 = b0 + 1;
            float lo = w.waveMin[b0], hi = w.waveMax[b0];
            for (int b = b0 + 1; b < b1 && b < bins; ++b) {
                lo = std::min(lo, w.waveMin[b]);
                hi = std::max(hi, w.waveMax[b]);
            }
            float x = origin.x + px;
            dl->AddLine(ImVec2(x, midY - hi * halfH),
                        ImVec2(x, midY - lo * halfH), col);
        }
        dl->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + width, midY),
                    ImGui::GetColorU32(ImGuiCol_Border));
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::Text("%s  (%lld frames)", w.name.c_str(),
                (long long)w.waveFrames);
}

static void drawXY(UIWidget& w) {
    // The two axes size independently (fw / fh), like every other 2D
    // widget: a pad forced square ignores the frame's height, so arrange
    // mode's resize grip would only ever respond to horizontal drags.
    const float width = wFrameW(w, 160.0f);
    const float height = wFrameH(w, 160.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    bool active = ImGui::IsItemActive();
    if (active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        float px = std::clamp((m.x - origin.x) / width, 0.0f, 1.0f);
        float py = 1.0f - std::clamp((m.y - origin.y) / height, 0.0f, 1.0f);
        w.values[0] = w.spec.map(px);
        w.values[1] = w.spec2.map(py);
        markDirty(w);
    }
    if (active) {
        w.gestureActive = true;
        w.wheelTime = 0.0;  // a drag owns the gesture; hover timer off
    } else if (w.gestureActive && w.wheelTime == 0.0) {
        // Drag release. Hover bursts (wheelTime > 0) end via the
        // hover idle timer instead.
        w.gestureActive = false;
        w.gestureEnded = true;
    }
    // Pad frame + crosshair
    auto* dl = ImGui::GetWindowDrawList();
    ImU32 frameCol = ImGui::GetColorU32(active ? ImGuiCol_FrameBgActive
                                               : ImGuiCol_FrameBg);
    ImVec2 farCorner(origin.x + width, origin.y + height);
    dl->AddRectFilled(origin, farCorner, frameCol);
    dl->AddRect(origin, farCorner, ImGui::GetColorU32(ImGuiCol_Border));
    float cx = origin.x + static_cast<float>(w.spec.unmap(w.values[0])) * width;
    float cy = origin.y
             + (1.0f - static_cast<float>(w.spec2.unmap(w.values[1]))) * height;
    ImU32 cursorCol = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    dl->AddLine(ImVec2(origin.x, cy), ImVec2(farCorner.x, cy), cursorCol);
    dl->AddLine(ImVec2(cx, origin.y), ImVec2(cx, farCorner.y), cursorCol);
    dl->AddCircleFilled(ImVec2(cx, cy), 4.0f, cursorCol);

    // The pad InvisibleButton is still the last item here (the lines
    // above only touch the draw list), so hover gestures see the pad.
    hoverAdjustXY(w);

    ImGui::Text("%s  (%.4g, %.4g)", w.name.c_str(), w.values[0], w.values[1]);
}

// Multislider hover gestures: keys act on ALL bars (independent
// randoms per bar for r and j); the wheel adjusts the single bar
// under the cursor.
static void hoverAdjustMultiSlider(UIWidget& w, ImVec2 origin,
                                   float width) {
    if (!hoverAdjustBegin(w)) return;
    int n = (int)w.values.size();
    if (n < 1) return;

    auto stamp = [&] {
        markDirty(w);
        w.gestureActive = true;
        w.wheelTime = ImGui::GetTime();
    };
    auto setBar = [&](int i, float p) {
        w.values[(size_t)i] = w.spec.map(std::clamp(p, 0.0f, 1.0f));
    };
    auto setAll = [&](auto posOf) {
        for (int i = 0; i < n; ++i) setBar(i, posOf(i));
        stamp();
    };

    auto jitterAll = [&](float d) {
        setAll([&](int i) {
            float p = (float)w.spec.unmap(w.values[(size_t)i]);
            return hoverBounce(p + hoverUniform(-d, d));
        });
    };

    hoverChars([&](unsigned c) {
        switch (c) {
            case 'c': case 'C': setAll([](int) { return 0.5f; }); break;
            case '[': setAll([](int) { return 0.0f; }); break;
            case ']': setAll([](int) { return 1.0f; }); break;
            case 'r': case 'R':
                setAll([&](int) { return hoverUniform(0.0f, 1.0f); });
                break;
            case 'j': jitterAll(0.05f); break;
            case 'J': jitterAll(0.005f); break;
            case 'z': case 'Z':
                setAll([&](int) {
                    return (float)w.spec.unmap(w.spec.clamp(0.0));
                });
                break;
            case 'i': case 'I':
                setAll([&](int) {
                    return (float)w.spec.unmap(w.spec.clamp(w.spec.init));
                });
                break;
            default: break;
        }
    });

    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
        int idx = std::clamp(
            (int)((ImGui::GetIO().MousePos.x - origin.x) / width * n),
            0, n - 1);
        float p = (float)w.spec.unmap(w.values[(size_t)idx]);
        setBar(idx, p - wheel * 0.01f);
        stamp();
    }
    hoverOwnWheel();
}

static void drawMultiSlider(UIWidget& w) {
    int n = (int)w.values.size();
    if (n < 1) return;
    const float width = wFrameW(w, 240.0f);
    const float height = wFrameH(w, 80.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    bool active = ImGui::IsItemActive();
    if (active) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int idx = std::clamp((int)((m.x - origin.x) / width * n), 0, n - 1);
        float pos = 1.0f - std::clamp((m.y - origin.y) / height, 0.0f, 1.0f);
        w.values[(size_t)idx] = w.spec.map(pos);
        markDirty(w);
    }
    if (active) {
        w.gestureActive = true;
        w.wheelTime = 0.0;  // a drag owns the gesture; hover timer off
    } else if (w.gestureActive && w.wheelTime == 0.0) {
        w.gestureActive = false;
        w.gestureEnded = true;
    }
    hoverAdjustMultiSlider(w, origin, width);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImU32 barCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    float barW = width / (float)n;
    for (int i = 0; i < n; ++i) {
        float p = (float)w.spec.unmap(w.values[(size_t)i]);
        float x0 = origin.x + i * barW;
        dl->AddRectFilled(ImVec2(x0 + 1.0f, origin.y + (1.0f - p) * height),
                          ImVec2(x0 + barW - 1.0f, origin.y + height), barCol);
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height),
                ImGui::GetColorU32(ImGuiCol_Border));
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawMatrix(UIWidget& w) {
    int rows = std::max(1, w.rows), cols = std::max(1, w.cols);
    const float width = wFrameW(w, cols * 22.0f);
    const float height = wFrameH(w, rows * 22.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int c = std::clamp((int)((m.x - origin.x) / width * cols), 0, cols - 1);
        int r = std::clamp((int)((m.y - origin.y) / height * rows), 0, rows - 1);
        size_t i = (size_t)r * cols + c;
        if (i < w.values.size()) {
            w.values[i] = w.values[i] > 0.5 ? 0.0 : 1.0;
            w.pushCellEvent((int)i, w.values[i]);
            markDirty(w);
            w.gestureEnded = true;  // one history commit per toggle
        }
    }
    auto* dl = ImGui::GetWindowDrawList();
    float cw = width / (float)cols, chh = height / (float)rows;
    ImU32 onCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border);
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            size_t i = (size_t)r * cols + c;
            if (i < w.values.size() && w.values[i] > 0.5) {
                dl->AddRectFilled(
                    ImVec2(origin.x + c * cw + 1, origin.y + r * chh + 1),
                    ImVec2(origin.x + (c + 1) * cw - 1,
                           origin.y + (r + 1) * chh - 1), onCol);
            }
        }
    }
    for (int c = 0; c <= cols; ++c)
        dl->AddLine(ImVec2(origin.x + c * cw, origin.y),
                    ImVec2(origin.x + c * cw, origin.y + height), gridCol);
    for (int r = 0; r <= rows; ++r)
        dl->AddLine(ImVec2(origin.x, origin.y + r * chh),
                    ImVec2(origin.x + width, origin.y + r * chh), gridCol);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawButtonMatrix(UIWidget& w) {
    int rows = std::max(1, w.rows), cols = std::max(1, w.cols);
    const float width = wFrameW(w, cols * 64.0f);
    const float height = wFrameH(w, rows * 26.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    auto cellAt = [&](ImVec2 m) {
        int c = std::clamp((int)((m.x - origin.x) / width * cols), 0, cols - 1);
        int r = std::clamp((int)((m.y - origin.y) / height * rows), 0, rows - 1);
        return r * cols + c;
    };
    if (w.momentary) {
        // Press = 1, release = 0; both edges dispatch (like drawButton).
        // Dragging onto another cell releases the held one and presses
        // the new one. No history commit: values return to rest.
        if (ImGui::IsItemActivated()) {
            int i = cellAt(ImGui::GetIO().MousePos);
            if (i < (int)w.values.size()) {
                w.activeCell = i;
                w.values[(size_t)i] = 1.0;
                w.pushCellEvent(i, 1.0);
                markDirty(w);
            }
        } else if (ImGui::IsItemActive() && w.activeCell >= 0) {
            int i = cellAt(ImGui::GetIO().MousePos);
            if (i != w.activeCell && i < (int)w.values.size()) {
                if (w.activeCell < (int)w.values.size()) {
                    w.values[(size_t)w.activeCell] = 0.0;
                    w.pushCellEvent(w.activeCell, 0.0);
                }
                w.activeCell = i;
                w.values[(size_t)i] = 1.0;
                w.pushCellEvent(i, 1.0);
                markDirty(w);
            }
        }
        if (ImGui::IsItemDeactivated() && w.activeCell >= 0) {
            if (w.activeCell < (int)w.values.size()) {
                w.values[(size_t)w.activeCell] = 0.0;
                w.pushCellEvent(w.activeCell, 0.0);
                markDirty(w);
            }
            w.activeCell = -1;
        }
    } else if (ImGui::IsItemClicked()) {
        int i = cellAt(ImGui::GetIO().MousePos);
        if (i < (int)w.values.size()) {
            double v = w.values[(size_t)i] > 0.5 ? 0.0 : 1.0;
            w.values[(size_t)i] = v;
            w.pushCellEvent(i, v);
            markDirty(w);
            w.gestureEnded = true;  // one history commit per toggle
        }
    }
    auto* dl = ImGui::GetWindowDrawList();
    float cw = width / (float)cols, chh = height / (float)rows;
    ImU32 onCol = ImGui::GetColorU32(ImGuiCol_ButtonActive);
    ImU32 offCol = ImGui::GetColorU32(ImGuiCol_Button);
    ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            size_t i = (size_t)r * cols + c;
            ImVec2 p0(origin.x + c * cw + 1, origin.y + r * chh + 1);
            ImVec2 p1(origin.x + (c + 1) * cw - 1, origin.y + (r + 1) * chh - 1);
            bool on = i < w.values.size() && w.values[i] > 0.5;
            dl->AddRectFilled(p0, p1, on ? onCol : offCol, 3.0f);
            if (i < w.cellLabels.size() && !w.cellLabels[i].empty()) {
                char const* txt = w.cellLabels[i].c_str();
                ImVec2 ts = ImGui::CalcTextSize(txt);
                ImVec2 tp(p0.x + std::max(0.0f, (p1.x - p0.x - ts.x) * 0.5f),
                          p0.y + std::max(0.0f, (p1.y - p0.y - ts.y) * 0.5f));
                ImVec4 clip(p0.x, p0.y, p1.x, p1.y);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), tp,
                            textCol, txt, nullptr, 0.0f, &clip);
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawPianoRoll(UIWidget& w) {
    // The vertical axis is CONTINUOUS pitch in steps of 1/edo octave
    // (edo 12 = MIDI numbers, 1200 = cents, 1 = octaves). A note at
    // pitch p fills the band [p, p+1); fractional pitches draw at their
    // true position. The click grid adds whole steps; per-step grid
    // lines and step shading disappear when steps are subpixel (cents).
    const float stepBeats = 0.25f;  // 16th grid
    int rows = std::max(1, w.rollRows);
    int edo = std::max(1, w.rollEdo);
    int cols = std::max(1, (int)std::lround(w.rollBeats / stepBeats));
    const float width = wFrameW(w, 320.0f);
    const float height = wFrameH(w, 200.0f);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton((std::string("##") + w.name).c_str(),
                           ImVec2(width, height));
    float cw = width / (float)cols, chh = height / (float)rows;
    // y(p) = bottom edge of pitch p's band; bands stack upward.
    float top = (float)(w.rollLowPitch + rows);
    auto yOf = [&](float p) { return origin.y + (top - p) * chh; };

    if (ImGui::IsItemClicked()) {
        ImVec2 m = ImGui::GetIO().MousePos;
        int c = std::clamp((int)((m.x - origin.x) / cw), 0, cols - 1);
        float start = c * stepBeats;
        // Pitch whose band center is at the cursor (continuous).
        float clickPitch = top - (m.y - origin.y) / chh - 0.5f;
        // Delete the nearest note covering this beat within a half
        // semitone (1/24 octave) of the click; otherwise add a whole
        // step (matching exact-cell deletion for integer notes).
        float tol = (float)edo / 24.0f;
        size_t best = w.noteData.size();
        float bestDist = tol;
        for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
            if (start < w.noteData[i + 1]
                || start >= w.noteData[i + 1] + w.noteData[i + 2]) continue;
            float d = std::fabs(w.noteData[i] - clickPitch);
            if (d <= bestDist) {
                bestDist = d;
                best = i;
            }
        }
        if (best < w.noteData.size()) {
            w.noteData.erase(w.noteData.begin() + best,
                             w.noteData.begin() + best + 3);
        } else {
            int r = std::clamp((int)((m.y - origin.y) / chh), 0, rows - 1);
            w.noteData.push_back((float)(w.rollLowPitch + (rows - 1 - r)));
            w.noteData.push_back(start);
            w.noteData.push_back(stepBeats);
        }
        w.dirtyCallback = true;
        w.gestureEnded = true;  // one history commit per note edit
    }

    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                      ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_TableBorderLight);
    ImU32 beatCol = ImGui::GetColorU32(ImGuiCol_Border);
    ImU32 noteCol = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
    // Reference-step shading for orientation (the C rows in 12-edo),
    // when a step is wide enough to see.
    if (chh >= 2.0f) {
        for (int r = 0; r < rows; ++r) {
            int pitch = w.rollLowPitch + (rows - 1 - r);
            if (pitch % edo == 0) {
                dl->AddRectFilled(
                    ImVec2(origin.x, origin.y + r * chh),
                    ImVec2(origin.x + width, origin.y + (r + 1) * chh),
                    ImGui::GetColorU32(ImGuiCol_TableRowBgAlt));
            }
        }
    }
    for (int c = 0; c <= cols; ++c) {
        dl->AddLine(ImVec2(origin.x + c * cw, origin.y),
                    ImVec2(origin.x + c * cw, origin.y + height),
                    (c % 4 == 0) ? beatCol : gridCol);
    }
    // Per-step gridlines only for divisions coarse enough that a line
    // per pitch is readable; denser edos rely on the octave lines and
    // reference-band shading.
    if (edo <= 24 && chh >= 3.0f) {
        for (int r = 0; r <= rows; ++r)
            dl->AddLine(ImVec2(origin.x, origin.y + r * chh),
                        ImVec2(origin.x + width, origin.y + r * chh), gridCol);
    }
    // Octave boundary lines keep dense divisions (cents) navigable.
    for (int p = ((w.rollLowPitch + edo - 1) / edo) * edo;
         p <= w.rollLowPitch + rows; p += edo)
        dl->AddLine(ImVec2(origin.x, yOf((float)p)),
                    ImVec2(origin.x + width, yOf((float)p)), beatCol);
    for (size_t i = 0; i + 2 < w.noteData.size(); i += 3) {
        float p = w.noteData[i];
        if (p + 1.0f < (float)w.rollLowPitch || p > top) continue;
        float y1 = std::min(yOf(p), origin.y + height);
        float y0 = std::max(yOf(p) - std::max(chh, 2.0f), origin.y);
        float x0 = origin.x + w.noteData[i + 1] / stepBeats * cw;
        float x1 = x0 + w.noteData[i + 2] / stepBeats * cw;
        dl->AddRectFilled(ImVec2(x0 + 1, y0 + 1), ImVec2(x1 - 1, y1 - 1),
                          noteCol);
    }
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height), beatCol);
    ImGui::SameLine();
    ImGui::TextUnformatted(w.name.c_str());
}

static void drawLabel(UIWidget& w) {
    ImGui::TextUnformatted(w.labelText.empty() ? w.name.c_str()
                                               : w.labelText.c_str());
}

bool drawUIWidget(bridge::UIWidget& w) {
    ImGui::PushID(static_cast<int>(w.id));
    bool tap = false;
    switch (w.kind) {
        case UIWidgetKind::Slider:   drawSlider(w); break;
        case UIWidgetKind::Range:    drawRange(w); break;
        case UIWidgetKind::Number:   drawNumber(w); break;
        case UIWidgetKind::Button:   drawButton(w); break;
        case UIWidgetKind::Toggle:   drawToggle(w); break;
        case UIWidgetKind::XY:       drawXY(w); break;
        case UIWidgetKind::Meter:    drawMeter(w); tap = true; break;
        case UIWidgetKind::Scope:    drawScope(w); tap = true; break;
        case UIWidgetKind::Plot:     drawPlot(w); break;
        case UIWidgetKind::Waveform: drawWaveform(w); break;
        case UIWidgetKind::MultiSlider: drawMultiSlider(w); break;
        case UIWidgetKind::Matrix:      drawMatrix(w); break;
        case UIWidgetKind::ButtonMatrix: drawButtonMatrix(w); break;
        case UIWidgetKind::PianoRoll:   drawPianoRoll(w); break;
        case UIWidgetKind::Label:       drawLabel(w); break;
    }
    ImGui::PopID();
    return tap;
}

bool drawPanelWidgets(bridge::UIState& ui, std::string const& panel) {
    // Last-upsert order, matching the notebook panel canvas flow.
    std::vector<bridge::UIWidget*> ws;
    for (auto& wp : ui.widgets)
        if (wp->panel == panel) ws.push_back(wp.get());
    std::sort(ws.begin(), ws.end(),
              [](bridge::UIWidget* a, bridge::UIWidget* b) {
                  return a->seq < b->seq;
              });
    bool anyTaps = false;
    for (auto* w : ws) anyTaps |= drawUIWidget(*w);
    return anyTaps;
}

// ---------------------------------------------------------------------------
// Key bindings (ui.bindKey)
// ---------------------------------------------------------------------------

static ImGuiKey chordKey(std::string const& chord) {
    if (chord == "space") return ImGuiKey_Space;
    if (chord.size() != 1) return ImGuiKey_None;
    char c = chord[0];
    if (c >= 'a' && c <= 'z') return (ImGuiKey)(ImGuiKey_A + (c - 'a'));
    if (c >= '0' && c <= '9') return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
    return ImGuiKey_None;
}

void dispatchWidgetKeys(bridge::UIState& ui) {
    // A hovered gesture widget owns the keyboard: bindings stand down.
    if (gHoverOwnerLastFrame || gHoverOwnerThisFrame) return;
    // Typing anywhere (cell editors, slider text entry, panel names)
    // owns the keyboard; bindings fire only outside text input.
    if (ImGui::GetIO().WantTextInput) return;
    std::lock_guard<std::mutex> lock(ui.mtx);
    for (auto& wp : ui.widgets) {
        UIWidget& w = *wp;
        if (w.keyChord.empty()) continue;
        ImGuiKey key = chordKey(w.keyChord);
        if (key == ImGuiKey_None) continue;
        if (w.kind == UIWidgetKind::Button) {
            // Momentary, like a mouse press: down = 1, up = 0.
            if (ImGui::IsKeyPressed(key, false)) {
                w.values[0] = 1.0;
                markDirty(w);
            }
            if (ImGui::IsKeyReleased(key)) {
                w.values[0] = 0.0;
                markDirty(w);
            }
        } else if (w.kind == UIWidgetKind::Toggle) {
            if (ImGui::IsKeyPressed(key, false)) {
                w.values[0] = w.values[0] != 0.0 ? 0.0 : 1.0;
                markDirty(w);
                w.gestureEnded = true;  // one history commit per flip
            }
        }
    }
}
