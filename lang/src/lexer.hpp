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
//  lexer.hpp
//  lang
//
//  Hand-written lexer for Tzopilotl
//

#ifndef lexer_hpp
#define lexer_hpp

#include "base_types.hpp"
#include "error.hpp"
#include <string>
#include <vector>

namespace ts {

// Token types
enum class TokenKind {
    // Literals
    IntLiteral,
    FloatLiteral,
    ImaginaryLiteral, // 4i, 3.14i
    FractionLiteral,  // 1/2, 3/4 (no whitespace around /)
    StringLiteral,
    SymbolLiteral,    // 'foo

    // Identifiers and keywords
    Identifier,
    DynamicVar,   // `varName (backtick-prefixed dynamic scope variable)

    // Keywords
    Fn, Let, Var, Const,
    If, Else, While, For, Break, Continue,
    Return, Case, Match,
    Struct, Enum, Import, Export, As, Where, Private, Coro, Yield, Constraint, Requires,
    True, False, Nil,
    // Type keywords
    KwInt, KwFloat, KwString, KwSymbol, KwBool, KwVoid,
    KwFraction, KwComplex, KwAny, KwType,
    // Operators
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    Ampersand,  // &
    Pipe,       // |
    Caret,      // ^
    Tilde,      // ~
    Bang,       // !
    Equals,     // =
    EqEq,       // ==
    BangEq,     // !=
    Less,       // <
    LessEq,     // <=
    Greater,    // >
    GreaterEq,  // >=
    PipeGreater,// |>
    Arrow,      // ->
    FatArrow,   // =>
    At,         // @
    Dollar,     // $
    Hash,       // # (prefix for immutable persistent collections: #[...])
    Dot,        // .
    DotDot,     // ..
    Ellipsis,   // ...
    Colon,      // :
    ColonColon, // ::
    LeftArrow,  // <-
    Comma,      // ,
    Semicolon,  // ;
    Question,   // ?
    SlashSlash, // //
    ShiftLeft,  // <<
    ShiftRight, // >>
    UShiftRight, // >>>
    AmpAmp,     // &&
    PipePipe,   // ||

    // Delimiters
    LParen,     // (
    RParen,     // )
    LBracket,   // [
    RBracket,   // ]
    LBrace,     // {
    RBrace,     // }

    // Special
    Eof,        // End of file
    Error,      // Lexer error
};

struct Token {
    TokenKind kind;
    SourceRange loc;
    std::string text;     // Raw text of the token

    // Literal values (parsed during lexing)
    i64 intValue = 0;
    f64 floatValue = 0.0;
    i64 denominator = 1;

    Token() : kind(TokenKind::Eof) {}
    Token(TokenKind k, SourceRange l, std::string t)
        : kind(k), loc(l), text(std::move(t)) {}
};

class Lexer {
public:
    Lexer(const std::string& source, const std::string& filename = "<input>");

    // Get the next token
    Token nextToken();

    // Peek at the next token without consuming it
    const Token& peek();

    // Get all tokens (for debugging)
    std::vector<Token> tokenizeAll();

    // Error access
    const std::vector<CompileError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

    // Source context access (for error reporting)
    const std::string& source() const { return source_; }
    const std::string& filename() const { return filename_; }

    // Save/restore state for tentative parsing
    struct State {
        u32 pos;
        u32 line;
        u32 col;
        Token peeked;
        bool hasPeeked;
        size_t errorCount;
    };
    State save() const {
        return State{pos_, line_, col_, peeked_, hasPeeked_, errors_.size()};
    }
    void restore(const State& s) {
        pos_ = s.pos;
        line_ = s.line;
        col_ = s.col;
        peeked_ = s.peeked;
        hasPeeked_ = s.hasPeeked;
        while (errors_.size() > s.errorCount) errors_.pop_back();
    }

private:
    const std::string& source_;
    std::string filename_;
    u32 pos_;
    u32 line_;
    u32 col_;
    Token peeked_;
    bool hasPeeked_;

    std::vector<CompileError> errors_;

    // Character access
    char current() const;
    char peekChar(u32 offset = 1) const;
    bool atEnd() const;
    char advance();
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();

    // Token scanning
    Token scanNumber();
    Token scanString();
    Token scanTripleQuotedString(SourceLoc start);
    Token scanGuillemetsString(SourceLoc start);
    Token scanIdentifierOrKeyword();
    Token scanSymbolLiteral();
    Token scanOperator();

    // Helpers
    SourceLoc currentLoc() const;
    Token makeToken(TokenKind kind, SourceLoc start, const std::string& text);
    Token errorToken(const std::string& msg);
    bool isDigit(char c) const;
    bool isAlpha(char c) const;
    bool isAlphaNumeric(char c) const;
    TokenKind keywordKind(const std::string& text) const;
};

// Human-readable name for a token kind (e.g. "';'", "'fn'", "identifier", "end of file")
const char* tokenKindString(TokenKind kind);

} // namespace ts

#endif /* lexer_hpp */
