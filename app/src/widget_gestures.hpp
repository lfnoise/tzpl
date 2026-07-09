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
//  widget_gestures.hpp
//  app
//
//  Toolkit-free gesture math for the `ui` widgets, extracted from
//  widget_draw.cpp so the ImGui and JUCE renderers share (and can unit
//  test) the same interaction semantics. Pure functions over normalized
//  [0,1] warp positions -- value mapping is UISpec::map/unmap.
//

#ifndef widget_gestures_hpp
#define widget_gestures_hpp

namespace ui_gesture {

// Fine-drag anchor: a slider drag operates on the [0,1] warp position.
// Normal drag maps the mouse x directly; fine modes (1 = 0.1x, 2 = 0.01x)
// slow it, anchored so entering/leaving fine mode mid-drag never jumps.
struct DragAnchor {
    float anchorPos = 0.0f;    // warp position when the (fine) anchor was set
    float anchorMouse = 0.0f;  // mouse x at that moment
    int fine = 0;
};

// Position (0..1, unclamped) for a drag from `anchor` given the track
// geometry (minx/usable px) and current mouse x. Caller clamps to [0,1].
float sliderDragPos(DragAnchor const& anchor, float minx, float usable,
                    float mouseX);

// Translate a whole range (positions lo/hi in 0..1) so its midpoint tracks
// `pos`, pinning at the 0/1 rails so the width is preserved.
void moveRange(float pos, float& lo, float& hi);

// Move whichever end of the range is nearer to `pos`; re-orders so lo<=hi.
void adjustRangeEnd(float pos, float& lo, float& hi);

// Row-major cell hit for a Matrix widget: pixel (px,py) within a
// (w,h) rect of `rows`x`cols` cells. Returns -1 if outside; else the
// flat index row*cols + col.
int matrixCellHit(float px, float py, float w, float h, int rows, int cols);

// Piano-roll grid quantization: a click at fractional (beat, pitchStep)
// snaps to a whole step. Returns the quantized pitch step (an integer,
// rollLowPitch + row) and leaves the beat as-is (caller decides duration).
int rollQuantizePitch(float pitchStepFractional);

}

#endif /* widget_gestures_hpp */
