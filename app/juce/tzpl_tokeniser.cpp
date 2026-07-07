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
//  tzpl_tokeniser.cpp
//  app (JUCE)
//

#include "tzpl_tokeniser.hpp"
#include <unordered_set>

namespace tzplapp {

namespace {

// Keyword set from EditorPanel::createTzopilotlDef() (editor_panel.cpp).
bool isKeyword(juce::String const& word) {
    static const std::unordered_set<std::string> keywords = {
        "fn", "let", "var", "const", "if", "else", "while", "for",
        "break", "continue", "return", "case", "match",
        "struct", "enum", "import", "as", "where", "private",
        "coro", "yield", "constraint", "requires",
        "true", "false", "nil",
        "Int", "Float", "String", "Symbol", "Bool", "Void",
        "Fraction", "Complex", "Any", "type",
    };
    return keywords.count(word.toStdString()) != 0;
}

bool isIdentStart(juce::juce_wchar c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentBody(juce::juce_wchar c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

bool isDigit(juce::juce_wchar c) { return c >= '0' && c <= '9'; }

void skipNumber(juce::CodeDocument::Iterator& source) {
    // 0x hex, or decimal with optional fraction / exponent / imaginary i
    juce::juce_wchar c = source.peekNextChar();
    if (c == '0') {
        source.skip();
        juce::juce_wchar n = source.peekNextChar();
        if (n == 'x' || n == 'X') {
            source.skip();
            while (juce::CppTokeniserFunctions::isHexDigit(source.peekNextChar()))
                source.skip();
            return;
        }
    }
    while (isDigit(source.peekNextChar())) source.skip();
    if (source.peekNextChar() == '.') {
        source.skip();
        while (isDigit(source.peekNextChar())) source.skip();
    }
    juce::juce_wchar e = source.peekNextChar();
    if (e == 'e' || e == 'E') {
        source.skip();
        juce::juce_wchar sign = source.peekNextChar();
        if (sign == '+' || sign == '-') source.skip();
        while (isDigit(source.peekNextChar())) source.skip();
    }
    if (source.peekNextChar() == 'i') source.skip(); // imaginary suffix
}

void skipString(juce::CodeDocument::Iterator& source) {
    // Opening quote already consumed. Stop at the closing quote or EOL --
    // per-line stopping keeps unterminated strings from eating the file.
    for (;;) {
        juce::juce_wchar c = source.nextChar();
        if (c == 0 || c == '"' || c == '\n') break;
        if (c == '\\') source.skip(); // escaped char
    }
}

void skipBlockComment(juce::CodeDocument::Iterator& source) {
    // "/*" already consumed. Tzopilotl block comments NEST.
    int depth = 1;
    for (;;) {
        juce::juce_wchar c = source.nextChar();
        if (c == 0) break;
        if (c == '*' && source.peekNextChar() == '/') {
            source.skip();
            if (--depth == 0) break;
        } else if (c == '/' && source.peekNextChar() == '*') {
            source.skip();
            ++depth;
        }
    }
}

}

int TzplTokeniser::readNextToken(juce::CodeDocument::Iterator& source) {
    source.skipWhitespace();
    juce::juce_wchar c = source.peekNextChar();
    if (c == 0) return tokenError;

    // Comments: -- line, /* */ block (nestable)
    if (c == '-') {
        source.skip();
        if (source.peekNextChar() == '-') {
            source.skipToEndOfLine();
            return tokenComment;
        }
        return tokenOperator;
    }
    if (c == '/') {
        source.skip();
        juce::juce_wchar n = source.peekNextChar();
        if (n == '*') {
            source.skip();
            skipBlockComment(source);
            return tokenComment;
        }
        return tokenOperator;
    }

    if (isDigit(c)) {
        skipNumber(source);
        return tokenNumber;
    }

    if (c == '"') {
        source.skip();
        skipString(source);
        return tokenString;
    }

    // Symbol literal: 'identifier
    if (c == '\'') {
        source.skip();
        if (isIdentStart(source.peekNextChar())) {
            while (isIdentBody(source.peekNextChar())) source.skip();
            return tokenSymbol;
        }
        return tokenOperator;
    }

    // Identifiers / keywords. `!` is part of the identifier (push! vs push);
    // a leading backtick marks dynamic-scope variables.
    if (isIdentStart(c) || c == '`') {
        juce::String word;
        if (c == '`') { word << c; source.skip(); }
        while (isIdentBody(source.peekNextChar()))
            word << source.nextChar();
        if (source.peekNextChar() == '!') { word << source.nextChar(); }
        return isKeyword(word) ? tokenKeyword : tokenIdentifier;
    }

    // Brackets vs other punctuation/operators
    if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
        source.skip();
        return tokenBracket;
    }

    source.skip();
    return tokenOperator;
}

juce::CodeEditorComponent::ColourScheme TzplTokeniser::getDefaultColourScheme() {
    // Colors match the ImGuiColorTextEdit dark palette closely enough for
    // side-by-side comparison; theme-specific schemes can override later.
    juce::CodeEditorComponent::ColourScheme scheme;
    auto add = [&](char const* name, juce::uint32 argb) {
        scheme.set(name, juce::Colour(argb));
    };
    add("Error",      0xffff5050);
    add("Comment",    0xff5c9b5c);
    add("Keyword",    0xff569cd6);
    add("Identifier", 0xffd4d4d4);
    add("Number",     0xffb5cea8);
    add("String",     0xffce9178);
    add("Symbol",     0xffc586c0);
    add("Operator",   0xffbababa);
    add("Bracket",    0xffd0d0a0);
    return scheme;
}

}
