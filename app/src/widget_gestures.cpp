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
//  widget_gestures.cpp
//  app
//

#include "widget_gestures.hpp"
#include <cmath>
#include <utility>

namespace ui_gesture {

float sliderDragPos(DragAnchor const& a, float minx, float usable,
                    float mouseX) {
    if (a.fine) {
        float rate = a.fine == 1 ? 0.1f : 0.01f;
        float anchorPx = minx + a.anchorPos * usable;
        return (anchorPx + rate * (mouseX - a.anchorMouse) - minx) / usable;
    }
    return (mouseX - minx) / usable;
}

void moveRange(float pos, float& lo, float& hi) {
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

void adjustRangeEnd(float pos, float& lo, float& hi) {
    if (std::fabs(pos - lo) < std::fabs(pos - hi)) lo = pos;
    else hi = pos;
    if (hi < lo) std::swap(lo, hi);
}

int matrixCellHit(float px, float py, float w, float h, int rows, int cols) {
    if (rows < 1 || cols < 1 || px < 0.0f || py < 0.0f || px >= w || py >= h)
        return -1;
    int col = (int)(px / (w / (float)cols));
    int row = (int)(py / (h / (float)rows));
    if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;
    return row * cols + col;
}

int rollQuantizePitch(float pitchStepFractional) {
    return (int)std::lround(pitchStepFractional);
}

}
