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
//  diagnostic.cpp
//  lang
//
//  Enhanced error diagnostics implementation
//

#include "diagnostic.hpp"
#include <algorithm>
#include <sstream>
#include <cstring>

namespace ts {

// --- Helper: extract a source line from source text given a byte offset ---

struct LineInfo {
    std::string_view text;  // the line content (no newline)
    u32 lineStart;          // byte offset of line start
};

static LineInfo extractLine(const std::string& source, u32 offset) {
    if (source.empty() || offset > source.size()) {
        return {"", 0};
    }
    // Clamp offset
    if (offset == source.size()) offset = offset > 0 ? offset - 1 : 0;

    // Walk back to find line start
    u32 lineStart = offset;
    while (lineStart > 0 && source[lineStart - 1] != '\n') {
        --lineStart;
    }

    // Walk forward to find line end
    u32 lineEnd = offset;
    while (lineEnd < source.size() && source[lineEnd] != '\n') {
        ++lineEnd;
    }

    return {std::string_view(source.data() + lineStart, lineEnd - lineStart), lineStart};
}

// Extract a source line by 1-based line number (used as fallback when byte
// offset belongs to a different source file than the display text).
static LineInfo extractLineByNumber(const std::string& source, u32 lineNum) {
    if (source.empty() || lineNum == 0) return {"", 0};
    u32 currentLine = 1;
    u32 pos = 0;
    while (currentLine < lineNum && pos < source.size()) {
        if (source[pos] == '\n') ++currentLine;
        ++pos;
    }
    if (currentLine != lineNum) return {"", 0};
    u32 lineStart = pos;
    while (pos < source.size() && source[pos] != '\n') ++pos;
    return {std::string_view(source.data() + lineStart, pos - lineStart), lineStart};
}

// --- Levenshtein edit distance ---

int editDistance(std::string_view a, std::string_view b) {
    size_t m = a.size(), n = b.size();
    if (m == 0) return (int)n;
    if (n == 0) return (int)m;

    // Single-row DP optimization
    std::vector<int> prev(n + 1);
    for (size_t j = 0; j <= n; ++j) prev[j] = (int)j;

    for (size_t i = 1; i <= m; ++i) {
        int corner = prev[0];
        prev[0] = (int)i;
        for (size_t j = 1; j <= n; ++j) {
            int upper = prev[j];
            if (a[i - 1] == b[j - 1]) {
                prev[j] = corner;
            } else {
                prev[j] = 1 + std::min({corner, upper, prev[j - 1]});
            }
            corner = upper;
        }
    }
    return prev[n];
}

// --- Find closest match ---

std::string findClosestMatch(std::string_view name,
                             const std::vector<std::string>& candidates,
                             int maxDistance) {
    // Adaptive threshold: max(2, name.length()/3), capped by maxDistance.
    // Names of 1-2 chars only accept distance 1 -- at distance 2 every short
    // candidate matches (e.g. suggesting 'at' for 'x'), which is noise.
    int threshold = name.size() <= 2 ? 1 : std::max(2, (int)name.size() / 3);
    if (threshold > maxDistance) threshold = maxDistance;

    std::string best;
    int bestDist = threshold + 1;

    for (const auto& candidate : candidates) {
        // Quick length filter
        int lenDiff = (int)candidate.size() - (int)name.size();
        if (lenDiff > threshold || lenDiff < -threshold) continue;

        int dist = editDistance(name, candidate);
        if (dist < bestDist) {
            bestDist = dist;
            best = candidate;
        }
    }

    return best;
}

// --- ANSI color helpers ---

namespace ansi {
    static const char* boldRed    = "\033[1;31m";
    static const char* cyan       = "\033[36m";
    static const char* green      = "\033[32m";
    static const char* dim        = "\033[2m";
    static const char* reset      = "\033[0m";
}

// --- Format a single error ---

std::string formatError(const CompileError& err,
                        const std::string& source,
                        const std::string& filename,
                        bool useColor) {
    // Use per-error filename/source when available (e.g. errors from imported modules)
    const std::string& effectiveFilename = err.filename.empty() ? filename : err.filename;
    const std::string& effectiveSource = err.source.empty() ? source : err.source;

    const char* cBoldRed = useColor ? ansi::boldRed : "";
    const char* cCyan    = useColor ? ansi::cyan    : "";
    const char* cGreen   = useColor ? ansi::green   : "";
    const char* cDim     = useColor ? ansi::dim     : "";
    const char* cReset   = useColor ? ansi::reset   : "";

    // Error kind string
    const char* kind = "Error";
    switch (err.kind) {
        case CompileError::LexError:   kind = "Lex error"; break;
        case CompileError::ParseError: kind = "Parse error"; break;
        case CompileError::TypeError:  kind = "Type error"; break;
    }

    std::ostringstream out;

    u32 line = err.loc.start.line;
    u32 col  = err.loc.start.col;

    // Header: "Type error [filename:line:col]"
    out << cBoldRed << kind << cReset
        << " [" << effectiveFilename << ":" << line << ":" << col << "]\n";

    // Try to extract and display source context
    bool hasSource = !effectiveSource.empty() && line > 0;
    if (hasSource) {
        // Try offset-based extraction first; if the offset is out of bounds or
        // lands on a different line (can happen when the error location is from
        // a different source file, e.g. a template body in an imported module),
        // fall back to line-number-based extraction.
        auto [lineText, lineStart] = extractLine(effectiveSource, err.loc.start.offset);
        if (err.loc.start.offset > effectiveSource.size()) {
            auto fallback = extractLineByNumber(effectiveSource, line);
            lineText = fallback.text;
            lineStart = fallback.lineStart;
        } else {
            // Verify offset-based result is on the expected line by counting
            // newlines before lineStart.
            u32 computedLine = 1;
            for (u32 i = 0; i < lineStart && i < effectiveSource.size(); ++i) {
                if (effectiveSource[i] == '\n') ++computedLine;
            }
            if (computedLine != line) {
                auto fallback = extractLineByNumber(effectiveSource, line);
                lineText = fallback.text;
                lineStart = fallback.lineStart;
            }
        }

        // Gutter width = width of line number string
        std::string lineNumStr = std::to_string(line);
        size_t gutter = lineNumStr.size();
        std::string pad(gutter, ' ');

        // Empty gutter line
        out << cCyan << pad << cReset << " " << cDim << "|" << cReset << "\n";

        // Source line
        out << cCyan << lineNumStr << cReset << " " << cDim << "|" << cReset
            << " " << lineText << "\n";

        // Underline with carets
        // col is 1-based; compute caret start position (0-based)
        size_t caretStart = (col > 0) ? col - 1 : 0;

        // Determine caret length
        size_t caretLen = 1;
        if (err.loc.end.line == line && err.loc.end.col > err.loc.start.col) {
            // Same-line span
            caretLen = err.loc.end.col - err.loc.start.col;
        } else if (err.loc.end.line > line) {
            // Multi-line: underline to end of this line
            caretLen = lineText.size() > caretStart ? lineText.size() - caretStart : 1;
        }
        // Clamp caret to line bounds
        if (caretStart + caretLen > lineText.size() && lineText.size() > caretStart) {
            caretLen = lineText.size() - caretStart;
        }
        if (caretLen == 0) caretLen = 1;

        std::string spaces(caretStart, ' ');
        std::string underline;
        if (caretLen <= 3) {
            underline = std::string(caretLen, '^');
        } else {
            underline = "^" + std::string(caretLen - 2, '-') + "^";
        }

        out << cCyan << pad << cReset << " " << cDim << "|" << cReset
            << " " << spaces << cBoldRed << underline << cReset << "\n";

        // Message lines, indented under gutter
        // Split message on newlines
        std::istringstream msgStream(err.message);
        std::string msgLine;
        bool first = true;
        while (std::getline(msgStream, msgLine)) {
            if (first) {
                out << pad << " ";
                first = false;
            } else {
                out << pad << " ";
            }
            // Color "Did you mean" suggestions in green
            if (msgLine.find("Did you mean") != std::string::npos) {
                out << cGreen << msgLine << cReset << "\n";
            } else {
                out << msgLine << "\n";
            }
        }
    } else {
        // No source context available -- just print the message
        out << "  " << err.message << "\n";
    }

    // Render diagnostic notes (e.g. "to match '(' here")
    for (const auto& note : err.notes) {
        const std::string& noteFilename = note.filename.empty() ? filename : note.filename;
        const std::string& noteSource = note.source.empty() ? source : note.source;

        u32 noteLine = note.loc.start.line;
        u32 noteCol  = note.loc.start.col;

        out << cCyan << "note" << cReset
            << " [" << noteFilename << ":" << noteLine << ":" << noteCol << "]\n";

        bool hasNoteSource = !noteSource.empty() && noteLine > 0 && note.loc.start.offset <= noteSource.size();
        if (hasNoteSource) {
            auto [noteLineText, noteLineStart] = extractLine(noteSource, note.loc.start.offset);
            std::string noteLineNumStr = std::to_string(noteLine);
            size_t noteGutter = noteLineNumStr.size();
            std::string notePad(noteGutter, ' ');

            out << cCyan << notePad << cReset << " " << cDim << "|" << cReset << "\n";
            out << cCyan << noteLineNumStr << cReset << " " << cDim << "|" << cReset
                << " " << noteLineText << "\n";

            size_t noteCaretStart = (noteCol > 0) ? noteCol - 1 : 0;
            if (noteCaretStart > noteLineText.size()) noteCaretStart = noteLineText.size();
            std::string noteSpaces(noteCaretStart, ' ');
            out << cCyan << notePad << cReset << " " << cDim << "|" << cReset
                << " " << noteSpaces << cCyan << "^" << cReset << "\n";
            out << notePad << " " << note.message << "\n";
        } else {
            out << "  " << note.message << "\n";
        }
    }

    return out.str();
}

// --- Print all diagnostics ---

void printDiagnostics(const std::vector<CompileError>& errors,
                      const std::string& source,
                      const std::string& filename,
                      std::ostream& out,
                      bool useColor) {
    for (size_t i = 0; i < errors.size(); ++i) {
        if (i > 0) out << "\n";
        out << formatError(errors[i], source, filename, useColor);
    }
}

// --- Format errors as plain strings (for C API) ---

std::vector<std::string> formatErrorsPlain(
    const std::vector<CompileError>& errors,
    const std::string& source,
    const std::string& filename) {
    std::vector<std::string> result;
    result.reserve(errors.size());
    for (const auto& err : errors) {
        std::string formatted = formatError(err, source, filename, false);
        // Remove trailing newline if present
        while (!formatted.empty() && formatted.back() == '\n') {
            formatted.pop_back();
        }
        result.push_back(std::move(formatted));
    }
    return result;
}

} // namespace ts
