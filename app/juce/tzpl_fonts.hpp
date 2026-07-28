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
//  tzpl_fonts.hpp
//  app (JUCE)
//
//  One source of truth for the editor / console / widget monospaced font.
//  Centralising it here means cross-platform font selection (and, later,
//  swapping in an embedded BinaryData typeface for pixel-identical rendering)
//  is a one-line change instead of touching ~20 call sites.
//

#ifndef tzpl_fonts_hpp
#define tzpl_fonts_hpp

#include <juce_graphics/juce_graphics.h>

namespace tzplapp {

// The preferred monospaced family per platform, falling back to whatever
// JUCE reports as the system monospaced font. Consolas (Windows) and
// DejaVu Sans Mono (Linux) ship with their platforms; Menlo on macOS.
//
// Resolved once per run: Font::findAllTypefaceNames() enumerates every
// installed font through CoreText, which costs ~25 ms here -- ruinous from a
// paint routine, and the answer cannot usefully change mid-session. Call it
// from the message thread first (the initializer is not re-entrant-safe
// against JUCE's font machinery, though the static itself is thread-safe).
inline juce::String monoFontName() {
    static juce::String const resolved = [] {
       #if JUCE_MAC
        juce::String preferred = "Menlo";
       #elif JUCE_WINDOWS
        juce::String preferred = "Consolas";
       #else
        juce::String preferred = "DejaVu Sans Mono";
       #endif
        auto const& available = juce::Font::findAllTypefaceNames();
        if (available.contains(preferred)) return preferred;
        return juce::Font::getDefaultMonospacedFontName();
    }();
    return resolved;
}

// A monospaced FontOptions at the given pixel size. To embed a font, build a
// juce::Typeface from BinaryData once and return FontOptions(typeface) here.
inline juce::FontOptions monoFont(float px) {
    return juce::FontOptions(monoFontName(), px, juce::Font::plain);
}

}

#endif /* tzpl_fonts_hpp */
