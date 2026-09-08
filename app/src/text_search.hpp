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
//  text_search.hpp
//  app
//
//  The pattern matcher behind the editor's Find bar and the sidebar's
//  Find in Files: one search term interpreted in one of the five Xcode
//  modes (Contains, Matches Word, Starts With, Ends With, Regular
//  Expression), case-sensitive or not.
//
//  Text is std::wstring so the offsets it reports line up with the
//  juce::String / CodeDocument character indices the editor uses: on macOS
//  and Linux wchar_t is UTF-32, so an index is one code point on both
//  sides. (On Windows wchar_t is UTF-16; only astral-plane characters
//  would shift an offset there.) No JUCE dependency, so the matcher is
//  unit-tested headlessly (tests/search_test.cpp).
//

#ifndef text_search_hpp
#define text_search_hpp

#include <regex>
#include <string>
#include <vector>

namespace tzplapp {

enum class MatchMode { contains = 0, matchesWord, startsWith, endsWith, regex };

inline constexpr char const* kMatchModeNames[] = {
    "Contains", "Matches Word", "Starts With", "Ends With",
    "Regular Expression",
};
inline constexpr int kNumMatchModes = 5;

struct TextMatch {
    int start = 0;
    int length = 0;
    int end() const { return start + length; }
};

class TextMatcher {
public:
    TextMatcher() = default;
    TextMatcher(std::wstring term, MatchMode mode, bool caseSensitive);

    bool empty() const { return term_.empty(); }
    // False when the regular expression failed to compile; error() says why.
    bool valid() const { return error_.empty(); }
    std::string const& error() const { return error_; }
    MatchMode mode() const { return mode_; }

    // Every match in `text`, in order, non-overlapping. Regular-expression
    // matches of zero length are dropped (they would select nothing).
    std::vector<TextMatch> findAll(std::wstring const& text) const;

    // The text that replaces `m`. Literal for every mode except regex, where
    // `$0`/`$&` is the whole match and `$1`..`$9` are capture groups.
    std::wstring expandReplacement(std::wstring const& text, TextMatch const& m,
                                   std::wstring const& replacement) const;

    // Identifier characters for the word-boundary modes: letters, digits,
    // `_`, and the backtick that prefixes dynamic-scope variables. A
    // trailing `!` belongs to the identifier too (push! vs push), so a word
    // match must not stop right before one.
    static bool isWordChar(wchar_t c);

private:
    bool boundaryBefore(std::wstring const& text, int pos) const;
    bool boundaryAfter(std::wstring const& text, int pos) const;

    std::wstring term_;
    MatchMode mode_ = MatchMode::contains;
    bool caseSensitive_ = false;
    std::wregex regex_;
    std::string error_;
};

}

#endif /* text_search_hpp */
