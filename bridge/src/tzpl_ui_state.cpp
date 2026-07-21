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

#include "tzpl_ui_state.hpp"

#include <algorithm>
#include <cmath>

namespace bridge {

// ---------------------------------------------------------------------------
// UISpec warp mapping
// ---------------------------------------------------------------------------

double UISpec::clamp(double v) const {
    double a = std::min(lo, hi), b = std::max(lo, hi);
    return std::min(std::max(v, a), b);
}

double UISpec::map(double p) const {
    p = std::min(std::max(p, 0.0), 1.0);
    switch (warp) {
        case UIWarp::Linear:
            return lo + (hi - lo) * p;
        case UIWarp::Exponential:
            // Requires lo and hi nonzero with the same sign; otherwise
            // degrade to linear rather than produce NaNs.
            if (lo * hi <= 0.0) return lo + (hi - lo) * p;
            return lo * std::pow(hi / lo, p);
        case UIWarp::Step: {
            double v = lo + (hi - lo) * p;
            if (warpParam > 0.0)
                v = lo + std::round((v - lo) / warpParam) * warpParam;
            return clamp(v);
        }
        case UIWarp::SignedSquare: {
            double u = 2.0 * p - 1.0;
            double w = u * std::fabs(u);
            return lo + (hi - lo) * (w + 1.0) * 0.5;
        }
        case UIWarp::Cubed: {
            double u = 2.0 * p - 1.0;
            double w = u * u * u;
            return lo + (hi - lo) * (w + 1.0) * 0.5;
        }
    }
    return lo + (hi - lo) * p;
}

double UISpec::unmap(double v) const {
    double p;
    double range = hi - lo;
    if (range == 0.0) return 0.0;
    switch (warp) {
        case UIWarp::Linear:
            p = (v - lo) / range;
            break;
        case UIWarp::Exponential:
            if (lo * hi <= 0.0 || v / lo <= 0.0) { p = (v - lo) / range; break; }
            p = std::log(v / lo) / std::log(hi / lo);
            break;
        case UIWarp::Step:
            p = (clamp(v) - lo) / range;
            break;
        case UIWarp::SignedSquare: {
            double w = 2.0 * (v - lo) / range - 1.0;
            double u = (w < 0.0) ? -std::sqrt(-w) : std::sqrt(w);
            p = (u + 1.0) * 0.5;
            break;
        }
        case UIWarp::Cubed: {
            double w = 2.0 * (v - lo) / range - 1.0;
            double u = std::cbrt(w);
            p = (u + 1.0) * 0.5;
            break;
        }
        default:
            p = (v - lo) / range;
            break;
    }
    return std::min(std::max(p, 0.0), 1.0);
}

// ---------------------------------------------------------------------------
// UIState registry
// ---------------------------------------------------------------------------

static int numValuesFor(UIWidgetKind kind) {
    switch (kind) {
        case UIWidgetKind::XY:    return 2;  // x, y
        case UIWidgetKind::Meter: return 2;  // rms, peak
        case UIWidgetKind::Range: return 2;  // lo, hi
        // MultiSlider/Matrix/ButtonMatrix are resized by their
        // constructors after upsert.
        default:                  return 1;
    }
}

UIWidget* UIState::findByName(std::string const& panel, std::string const& name) {
    for (auto& w : widgets) {
        if (w->name == name && w->panel == panel) return w.get();
    }
    return nullptr;
}

UIWidget* UIState::findById(std::uint64_t id) {
    for (auto& w : widgets) {
        if (w->id == id) return w.get();
    }
    return nullptr;
}

UIWidget* UIState::upsert(std::string const& panel, std::string const& name,
                          UIWidgetKind kind, UISpec const& spec,
                          UISpec const& spec2) {
    UIWidget* w = findByName(panel, name);
    if (w) {
        w->seq = nextSeq++;
        // Adopt: keep current values (clamped into the new range), take the
        // new kind/spec, drop stale bindings so the caller rebinds fresh.
        // Variable-count kinds (MultiSlider/Matrix/ButtonMatrix) keep their
        // sizes; their constructors adjust the count after the upsert.
        bool variableCount = kind == UIWidgetKind::MultiSlider
                          || kind == UIWidgetKind::Matrix
                          || kind == UIWidgetKind::ButtonMatrix;
        w->kind = kind;
        w->spec = spec;
        w->spec2 = spec2;
        if (!variableCount || w->values.empty())
            w->values.resize(numValuesFor(kind), 0.0);
        w->values[0] = spec.clamp(w->values[0]);
        if (kind == UIWidgetKind::XY) w->values[1] = spec2.clamp(w->values[1]);
        if (kind == UIWidgetKind::Range) {
            w->values[1] = spec.clamp(w->values[1]);
            if (w->values[0] > w->values[1])
                std::swap(w->values[0], w->values[1]);
        }
        w->target.reset();
        w->target2.reset();
        w->onChange = nullptr;
        w->onCell = nullptr;
        w->cellEvents.clear();
        w->dirtyEngine = false;
        w->dirtyCallback = false;
        return w;
    }
    auto ww = std::make_unique<UIWidget>();
    ww->id = nextId++;
    ww->seq = nextSeq++;
    ww->kind = kind;
    ww->name = name;
    ww->panel = panel;
    ww->spec = spec;
    ww->spec2 = spec2;
    ww->values.assign(numValuesFor(kind), 0.0);
    ww->values[0] = spec.clamp(spec.init);
    if (kind == UIWidgetKind::XY) ww->values[1] = spec2.clamp(spec2.init);
    if (kind == UIWidgetKind::Range) ww->values[1] = spec.clamp(spec.init);
    w = ww.get();
    widgets.push_back(std::move(ww));
    return w;
}

bool UIState::remove(std::uint64_t id) {
    auto it = std::find_if(widgets.begin(), widgets.end(),
                           [id](auto const& w) { return w->id == id; });
    if (it == widgets.end()) return false;
    widgets.erase(it);
    return true;
}

void UIState::clear() {
    widgets.clear();
}

} // namespace bridge
