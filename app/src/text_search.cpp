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
//  text_search.cpp
//  app
//

#include "text_search.hpp"
#include <cwctype>

namespace tzplapp {

namespace {

wchar_t fold(wchar_t c) { return (wchar_t)std::towlower((wint_t)c); }

// Offsets of every occurrence of `needle` in `hay` (non-overlapping).
std::vector<int> literalOffsets(std::wstring const& hay,
                                std::wstring const& needle,
                                bool caseSensitive) {
    std::vector<int> out;
    int n = (int)needle.size(), h = (int)hay.size();
    if (n == 0 || h < n) return out;
    for (int i = 0; i + n <= h;) {
        bool hit = true;
        for (int k = 0; k < n; ++k) {
            wchar_t a = hay[(size_t)(i + k)], b = needle[(size_t)k];
            if (caseSensitive ? a != b : fold(a) != fold(b)) { hit = false; break; }
        }
        if (hit) { out.push_back(i); i += n; }
        else ++i;
    }
    return out;
}

}

bool TextMatcher::isWordChar(wchar_t c) {
    return c == L'_' || c == L'`' || std::iswalnum((wint_t)c) != 0;
}

TextMatcher::TextMatcher(std::wstring term, MatchMode mode, bool caseSensitive)
    : term_(std::move(term)), mode_(mode), caseSensitive_(caseSensitive)
{
    if (mode_ != MatchMode::regex || term_.empty()) return;
    auto flags = std::regex_constants::ECMAScript
               | std::regex_constants::multiline;
    if (!caseSensitive_) flags |= std::regex_constants::icase;
    try {
        regex_ = std::wregex(term_, flags);
    } catch (std::regex_error const& e) {
        error_ = e.what();
        if (error_.empty()) error_ = "invalid regular expression";
    }
}

bool TextMatcher::boundaryBefore(std::wstring const& text, int pos) const {
    return pos <= 0 || !isWordChar(text[(size_t)pos - 1]);
}

bool TextMatcher::boundaryAfter(std::wstring const& text, int pos) const {
    if (pos >= (int)text.size()) return true;
    wchar_t c = text[(size_t)pos];
    return !isWordChar(c) && c != L'!';
}

std::vector<TextMatch> TextMatcher::findAll(std::wstring const& text) const {
    std::vector<TextMatch> out;
    if (term_.empty() || !valid()) return out;

    if (mode_ == MatchMode::regex) {
        auto begin = std::wsregex_iterator(text.begin(), text.end(), regex_);
        for (auto it = begin; it != std::wsregex_iterator(); ++it) {
            auto const& m = *it;
            if (m.length(0) == 0) continue;
            out.push_back({ (int)m.position(0), (int)m.length(0) });
        }
        return out;
    }

    int n = (int)term_.size();
    for (int start : literalOffsets(text, term_, caseSensitive_)) {
        bool ok = true;
        switch (mode_) {
        case MatchMode::contains: break;
        case MatchMode::matchesWord:
            ok = boundaryBefore(text, start) && boundaryAfter(text, start + n);
            break;
        case MatchMode::startsWith:
            ok = boundaryBefore(text, start);
            break;
        case MatchMode::endsWith:
            ok = boundaryAfter(text, start + n);
            break;
        case MatchMode::regex: break;
        }
        if (ok) out.push_back({ start, n });
    }
    return out;
}

std::wstring TextMatcher::expandReplacement(std::wstring const& text,
                                            TextMatch const& m,
                                            std::wstring const& replacement) const {
    if (mode_ != MatchMode::regex || !valid()) return replacement;
    // Re-run the expression on the matched span alone so the capture groups
    // are available for `$n` expansion.
    std::wsmatch groups;
    std::wstring span = text.substr((size_t)m.start, (size_t)m.length);
    if (!std::regex_match(span, groups, regex_)) return replacement;
    std::wstring out;
    for (size_t i = 0; i < replacement.size(); ++i) {
        wchar_t c = replacement[i];
        if (c != L'$' || i + 1 >= replacement.size()) { out += c; continue; }
        wchar_t d = replacement[i + 1];
        if (d == L'$') { out += L'$'; ++i; }
        else if (d == L'&' || d == L'0') { out += span; ++i; }
        else if (d >= L'1' && d <= L'9') {
            size_t g = (size_t)(d - L'0');
            if (g < groups.size()) out += groups[g].str();
            ++i;
        } else out += c;
    }
    return out;
}

}
