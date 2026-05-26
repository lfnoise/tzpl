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
//  lexer.cpp
//  lang
//
//  Hand-written lexer implementation
//

#include "lexer.hpp"
#include <cmath>
#include <stdexcept>

namespace ts {

// Encode a Unicode code point as UTF-8 and append to the string.
static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static uint32_t hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    return 10 + c - 'A';
}

Lexer::Lexer(const std::string& source, const std::string& filename)
    : source_(source)
    , filename_(filename)
    , pos_(0)
    , line_(1)
    , col_(1)
    , hasPeeked_(false)
{}

char Lexer::current() const {
    if (pos_ >= source_.size()) return '\0';
    return source_[pos_];
}

char Lexer::peekChar(u32 offset) const {
    u32 idx = pos_ + offset;
    if (idx >= source_.size()) return '\0';
    return source_[idx];
}

bool Lexer::atEnd() const {
    return pos_ >= source_.size();
}

char Lexer::advance() {
    char c = current();
    pos_++;
    if (c == '\n') {
        line_++;
        col_ = 1;
    } else {
        col_++;
    }
    return c;
}

void Lexer::skipWhitespace() {
    while (!atEnd()) {
        char c = current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '-' && peekChar() == '-') {
            skipLineComment();
        } else if (c == '/' && peekChar() == '*') {
            skipBlockComment();
        } else {
            break;
        }
    }
}

void Lexer::skipLineComment() {
    // Skip -- (and any additional dashes like --- or ----)
    advance();
    advance();
    while (!atEnd() && current() == '-') {
        advance();
    }
    while (!atEnd() && current() != '\n') {
        advance();
    }
}

void Lexer::skipBlockComment() {
    // Skip /*
    advance();
    advance();
    int depth = 1;
    while (!atEnd() && depth > 0) {
        if (current() == '/' && peekChar() == '*') {
            advance();
            advance();
            depth++;
        } else if (current() == '*' && peekChar() == '/') {
            advance();
            advance();
            depth--;
        } else {
            advance();
        }
    }
}

SourceLoc Lexer::currentLoc() const {
    return SourceLoc{line_, col_, pos_};
}

Token Lexer::makeToken(TokenKind kind, SourceLoc start, const std::string& text) {
    SourceLoc end = currentLoc();
    return Token(kind, SourceRange(start, end), text);
}

Token Lexer::errorToken(const std::string& msg) {
    SourceLoc loc = currentLoc();
    errors_.push_back(CompileError(CompileError::LexError, SourceRange(loc, loc), msg));
    return Token(TokenKind::Error, SourceRange(loc, loc), msg);
}

bool Lexer::isDigit(char c) const {
    return c >= '0' && c <= '9';
}

bool Lexer::isAlpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::isAlphaNumeric(char c) const {
    return isAlpha(c) || isDigit(c);
}

TokenKind Lexer::keywordKind(const std::string& text) const {
    if (text == "fn") return TokenKind::Fn;
    if (text == "let") return TokenKind::Let;
    if (text == "var") return TokenKind::Var;
    if (text == "const") return TokenKind::Const;
    if (text == "if") return TokenKind::If;
    if (text == "else") return TokenKind::Else;
    if (text == "while") return TokenKind::While;
    if (text == "for") return TokenKind::For;
    if (text == "break") return TokenKind::Break;
    if (text == "continue") return TokenKind::Continue;
    if (text == "return") return TokenKind::Return;
    if (text == "case") return TokenKind::Case;
    if (text == "match") return TokenKind::Match;
    if (text == "struct") return TokenKind::Struct;
    if (text == "enum") return TokenKind::Enum;
    if (text == "import") return TokenKind::Import;
    if (text == "export") return TokenKind::Export;
    if (text == "as") return TokenKind::As;
    if (text == "where") return TokenKind::Where;
    if (text == "private") return TokenKind::Private;
    if (text == "coro") return TokenKind::Coro;
    if (text == "yield") return TokenKind::Yield;
    if (text == "constraint") return TokenKind::Constraint;
    if (text == "requires") return TokenKind::Requires;
    if (text == "true") return TokenKind::True;
    if (text == "false") return TokenKind::False;
    if (text == "nil") return TokenKind::Nil;
    if (text == "Int") return TokenKind::KwInt;
    if (text == "Float") return TokenKind::KwFloat;
    if (text == "String") return TokenKind::KwString;
    if (text == "Symbol") return TokenKind::KwSymbol;
    if (text == "Bool") return TokenKind::KwBool;
    if (text == "Void") return TokenKind::KwVoid;
    if (text == "Fraction") return TokenKind::KwFraction;
    if (text == "Complex") return TokenKind::KwComplex;
    if (text == "Any") return TokenKind::KwAny;
    if (text == "type") return TokenKind::KwType;
    return TokenKind::Identifier;
}

Token Lexer::scanNumber() {
    SourceLoc start = currentLoc();
    u32 startPos = pos_;
    bool isFloat = false;

    // Check for hexadecimal: 0x or 0X
    if (current() == '0' && (peekChar() == 'x' || peekChar() == 'X')) {
        advance(); // consume '0'
        advance(); // consume 'x'/'X'
        if (atEnd() || !isHexDigit(current())) {
            return errorToken("Expected hex digits after 0x");
        }
        while (!atEnd() && isHexDigit(current())) {
            advance();
        }
        std::string text = source_.substr(startPos, pos_ - startPos);
        Token tok = makeToken(TokenKind::IntLiteral, start, text);
        tok.intValue = std::stoll(text, nullptr, 16);
        return tok;
    }

    // Integer part
    while (!atEnd() && isDigit(current())) {
        advance();
    }

    // Check for decimal point (requires leading digit, e.g. 1.5 not .5)
    if (current() == '.' && peekChar() != '.' && isDigit(peekChar())) {
        isFloat = true;
        advance();
        // Fractional part
        while (!atEnd() && isDigit(current())) {
            advance();
        }
    }

    // Scientific notation
    if (current() == 'e' || current() == 'E') {
        isFloat = true;
        advance();
        if (current() == '+' || current() == '-') {
            advance();
        }
        while (!atEnd() && isDigit(current())) {
            advance();
        }
    }

    // Fraction literal: integer immediately followed by / and digits (no whitespace)
    if (!isFloat && current() == '/' && isDigit(peekChar())) {
        i64 numerator = std::stoll(source_.substr(startPos, pos_ - startPos));
        advance(); // consume '/'
        u32 denomStart = pos_;
        while (!atEnd() && isDigit(current())) advance();
        i64 denom = std::stoll(source_.substr(denomStart, pos_ - denomStart));
        std::string text = source_.substr(startPos, pos_ - startPos);
        // Disallow '/' immediately after a fraction literal to prevent
        // ambiguous expressions like 1/2/3/4.
        if (!atEnd() && current() == '/') {
            return errorToken("Ambiguous fraction literal; add spaces around '/' to use division (e.g. 1/2 / 3/4)");
        }
        Token tok = makeToken(TokenKind::FractionLiteral, start, text);
        tok.intValue = numerator;
        tok.denominator = denom;
        return tok;
    }

    // Check for imaginary suffix 'i' (not followed by alphanumeric)
    bool isImaginary = false;
    if (current() == 'i' && !isAlphaNumeric(peekChar())) {
        isImaginary = true;
        advance(); // consume 'i'
    }

    std::string text = source_.substr(startPos, pos_ - startPos);

    if (isImaginary) {
        // Strip trailing 'i' for numeric parsing
        std::string numText = text.substr(0, text.size() - 1);
        Token tok = makeToken(TokenKind::ImaginaryLiteral, start, text);
        try {
            tok.floatValue = std::stod(numText);
        } catch (const std::out_of_range&) {
            tok.floatValue = (numText[0] == '-') ? -INFINITY : INFINITY;
        }
        return tok;
    }

    Token tok = makeToken(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral, start, text);

    if (isFloat) {
        try {
            tok.floatValue = std::stod(text);
        } catch (const std::out_of_range&) {
            tok.floatValue = (text[0] == '-') ? -INFINITY : INFINITY;
        }
    } else {
        tok.intValue = std::stoll(text);
    }

    return tok;
}

Token Lexer::scanString() {
    SourceLoc start = currentLoc();
    advance(); // consume opening "

    // Check for triple-quoted raw string """
    if (current() == '"' && peekChar() == '"') {
        advance(); // consume second "
        advance(); // consume third "
        return scanTripleQuotedString(start);
    }

    std::string value;
    while (!atEnd() && current() != '"') {
        if (current() == '\\') {
            advance();
            switch (current()) {
                case 'n': value += '\n'; advance(); break;
                case 't': value += '\t'; advance(); break;
                case 'r': value += '\r'; advance(); break;
                case '\\': value += '\\'; advance(); break;
                case '"': value += '"'; advance(); break;
                case '0': value += '\0'; advance(); break;
                case 'u': {
                    advance(); // consume 'u'
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; i++) {
                        if (atEnd() || !isHexDigit(current())) {
                            return errorToken("Expected 4 hex digits after \\u");
                        }
                        cp = (cp << 4) | hexVal(current());
                        advance();
                    }
                    appendUtf8(value, cp);
                    break;
                }
                case 'U': {
                    advance(); // consume 'U'
                    uint32_t cp = 0;
                    for (int i = 0; i < 8; i++) {
                        if (atEnd() || !isHexDigit(current())) {
                            return errorToken("Expected 8 hex digits after \\U");
                        }
                        cp = (cp << 4) | hexVal(current());
                        advance();
                    }
                    if (cp > 0x10FFFF) {
                        return errorToken("Unicode code point out of range");
                    }
                    appendUtf8(value, cp);
                    break;
                }
                default:
                    value += '\\';
                    value += current();
                    advance();
                    break;
            }
        } else if (current() == '\n') {
            return errorToken("Unterminated string literal");
        } else {
            value += advance();
        }
    }

    if (atEnd()) {
        return errorToken("Unterminated string literal");
    }

    advance(); // consume closing "
    return makeToken(TokenKind::StringLiteral, start, value);
}

Token Lexer::scanTripleQuotedString(SourceLoc start) {
    // Opening """ already consumed. Raw string: no escape processing.
    std::string value;
    while (!atEnd()) {
        if (current() == '"' && peekChar(1) == '"' && peekChar(2) == '"') {
            advance(); // consume first "
            advance(); // consume second "
            advance(); // consume third "
            return makeToken(TokenKind::StringLiteral, start, value);
        }
        value += advance();
    }
    return errorToken("Unterminated triple-quoted string literal");
}

Token Lexer::scanGuillemetsString(SourceLoc start) {
    // Opening « (UTF-8: 0xC2 0xAB) already consumed.

    std::string value;
    while (!atEnd()) {
        // Check for closing » (UTF-8: 0xC2 0xBB)
        if (static_cast<unsigned char>(current()) == 0xC2 &&
            static_cast<unsigned char>(peekChar(1)) == 0xBB) {
            advance(); // consume 0xC2
            advance(); // consume 0xBB
            return makeToken(TokenKind::StringLiteral, start, value);
        }
        value += advance();
    }
    return errorToken("Unterminated guillemet string literal");
}

Token Lexer::scanIdentifierOrKeyword() {
    SourceLoc start = currentLoc();
    u32 startPos = pos_;

    while (!atEnd() && isAlphaNumeric(current())) {
        advance();
    }

    // Look up keyword on the base identifier (without any trailing '!').
    std::string text = source_.substr(startPos, pos_ - startPos);
    TokenKind kind = keywordKind(text);

    // Allow a single trailing '!' on identifiers (but not on keywords) for
    // mutating-function naming convention. The '!' is part of the identifier:
    // `foo` and `foo!` are distinct names. Refuse to consume the '!' if it
    // starts a `!=` token.
    if (kind == TokenKind::Identifier
        && !atEnd() && current() == '!' && peekChar() != '=') {
        advance();
        text = source_.substr(startPos, pos_ - startPos);
    }
    return makeToken(kind, start, text);
}

Token Lexer::scanSymbolLiteral() {
    SourceLoc start = currentLoc();
    advance(); // consume '\''

    // Symbol literal follows identifier rules
    if (!isAlpha(current())) {
        return errorToken("Expected identifier after '");
    }

    u32 startPos = pos_;
    while (!atEnd() && isAlphaNumeric(current())) {
        advance();
    }

    std::string text = source_.substr(startPos, pos_ - startPos);
    return makeToken(TokenKind::SymbolLiteral, start, text);
}

Token Lexer::nextToken() {
    if (hasPeeked_) {
        hasPeeked_ = false;
        return peeked_;
    }

    skipWhitespace();

    if (atEnd()) {
        return makeToken(TokenKind::Eof, currentLoc(), "");
    }

    char c = current();

    // Numbers
    if (isDigit(c)) {
        return scanNumber();
    }

    // Strings
    if (c == '"') {
        return scanString();
    }

    // Guillemet raw strings: « (UTF-8: 0xC2 0xAB)
    if (static_cast<unsigned char>(c) == 0xC2 &&
        static_cast<unsigned char>(peekChar()) == 0xAB) {
        SourceLoc guilStart = currentLoc();
        advance(); // consume 0xC2
        advance(); // consume 0xAB
        return scanGuillemetsString(guilStart);
    }

    // Identifiers and keywords
    if (isAlpha(c)) {
        return scanIdentifierOrKeyword();
    }

    // Symbol literal 'foo
    if (c == '\'') {
        return scanSymbolLiteral();
    }

    // Dynamic scope variable `varName
    if (c == '`') {
        SourceLoc start = currentLoc();
        advance(); // consume backtick
        if (!isAlpha(current())) {
            return errorToken("Expected identifier after '`'");
        }
        u32 startPos = pos_;
        while (!atEnd() && isAlphaNumeric(current())) {
            advance();
        }
        std::string text = source_.substr(startPos, pos_ - startPos);
        return makeToken(TokenKind::DynamicVar, start, text);
    }

    // Colon - could be :: or plain colon
    if (c == ':') {
        SourceLoc start = currentLoc();
        advance();
        if (current() == ':') {
            advance();
            return makeToken(TokenKind::ColonColon, start, "::");
        }
        return makeToken(TokenKind::Colon, start, ":");
    }

    // Multi-character operators and single-character tokens
    SourceLoc start = currentLoc();
    advance();

    switch (c) {
        case '+': return makeToken(TokenKind::Plus, start, "+");
        case '*': return makeToken(TokenKind::Star, start, "*");
        case '%': return makeToken(TokenKind::Percent, start, "%");
        case '^': return makeToken(TokenKind::Caret, start, "^");
        case '~': return makeToken(TokenKind::Tilde, start, "~");
        case '@': {
            // Check for @@ / @@@ (deep auto-mapping) or @1 / @2 (Cartesian)
            if (current() == '@') {
                // Consume consecutive '@' chars
                std::string text = "@";
                while (current() == '@') {
                    text += advance();
                }
                return makeToken(TokenKind::At, start, text);
            }
            if (isDigit(current()) && current() >= '1' && !isAlphaNumeric(peekChar())) {
                // @1, @2, ... @9
                std::string text = "@";
                text += advance();
                return makeToken(TokenKind::At, start, text);
            }
            return makeToken(TokenKind::At, start, "@");
        }
        case '$': return makeToken(TokenKind::Dollar, start, "$");
        case '#': return makeToken(TokenKind::Hash, start, "#");
        case ',': return makeToken(TokenKind::Comma, start, ",");
        case ';': return makeToken(TokenKind::Semicolon, start, ";");

        case '?': return makeToken(TokenKind::Question, start, "?");

        case '(':
            return makeToken(TokenKind::LParen, start, "(");
        case ')':
            return makeToken(TokenKind::RParen, start, ")");
        case '[':
            return makeToken(TokenKind::LBracket, start, "[");
        case ']':
            return makeToken(TokenKind::RBracket, start, "]");
        case '{':
            return makeToken(TokenKind::LBrace, start, "{");
        case '}':
            return makeToken(TokenKind::RBrace, start, "}");

        case '-':
            if (current() == '>') {
                advance();
                return makeToken(TokenKind::Arrow, start, "->");
            }
            return makeToken(TokenKind::Minus, start, "-");

        case '=':
            if (current() == '=') {
                advance();
                return makeToken(TokenKind::EqEq, start, "==");
            }
            if (current() == '>') {
                advance();
                return makeToken(TokenKind::FatArrow, start, "=>");
            }
            return makeToken(TokenKind::Equals, start, "=");

        case '!':
            if (current() == '=') {
                advance();
                return makeToken(TokenKind::BangEq, start, "!=");
            }
            return makeToken(TokenKind::Bang, start, "!");

        case '<':
            if (current() == '=') {
                advance();
                return makeToken(TokenKind::LessEq, start, "<=");
            }
            if (current() == '<') {
                advance();
                return makeToken(TokenKind::ShiftLeft, start, "<<");
            }
            if (current() == '-') {
                advance();
                return makeToken(TokenKind::LeftArrow, start, "<-");
            }
            return makeToken(TokenKind::Less, start, "<");

        case '>':
            if (current() == '=') {
                advance();
                return makeToken(TokenKind::GreaterEq, start, ">=");
            }
            if (current() == '>') {
                advance();
                if (current() == '>') {
                    advance();
                    return makeToken(TokenKind::UShiftRight, start, ">>>");
                }
                return makeToken(TokenKind::ShiftRight, start, ">>");
            }
            return makeToken(TokenKind::Greater, start, ">");

        case '|':
            if (current() == '>') {
                advance();
                return makeToken(TokenKind::PipeGreater, start, "|>");
            }
            if (current() == '|') {
                advance();
                return makeToken(TokenKind::PipePipe, start, "||");
            }
            return makeToken(TokenKind::Pipe, start, "|");

        case '&':
            if (current() == '&') {
                advance();
                return makeToken(TokenKind::AmpAmp, start, "&&");
            }
            return makeToken(TokenKind::Ampersand, start, "&");

        case '.':
            if (current() == '.') {
                advance();
                if (current() == '.') {
                    advance();
                    return makeToken(TokenKind::Ellipsis, start, "...");
                }
                return makeToken(TokenKind::DotDot, start, "..");
            }
            return makeToken(TokenKind::Dot, start, ".");

        case '/':
            if (current() == '/') {
                advance();
                return makeToken(TokenKind::SlashSlash, start, "//");
            }
            return makeToken(TokenKind::Slash, start, "/");

        default:
            return errorToken(std::string("Unexpected character: '") + c + "'");
    }
}

const Token& Lexer::peek() {
    if (!hasPeeked_) {
        peeked_ = nextToken();
        hasPeeked_ = true;
    }
    return peeked_;
}

std::vector<Token> Lexer::tokenizeAll() {
    std::vector<Token> tokens;
    while (true) {
        Token tok = nextToken();
        tokens.push_back(tok);
        if (tok.kind == TokenKind::Eof || tok.kind == TokenKind::Error) break;
    }
    return tokens;
}

const char* tokenKindString(TokenKind kind) {
    switch (kind) {
        case TokenKind::IntLiteral:      return "integer literal";
        case TokenKind::FloatLiteral:    return "float literal";
        case TokenKind::ImaginaryLiteral: return "imaginary literal";
        case TokenKind::FractionLiteral: return "fraction literal";
        case TokenKind::StringLiteral:   return "string literal";
        case TokenKind::SymbolLiteral:   return "symbol literal";
        case TokenKind::Identifier:      return "identifier";
        case TokenKind::DynamicVar:      return "dynamic variable";
        case TokenKind::Fn:              return "'fn'";
        case TokenKind::Let:             return "'let'";
        case TokenKind::Var:             return "'var'";
        case TokenKind::Const:           return "'const'";
        case TokenKind::If:              return "'if'";
        case TokenKind::Else:            return "'else'";
        case TokenKind::While:           return "'while'";
        case TokenKind::For:             return "'for'";
        case TokenKind::Break:           return "'break'";
        case TokenKind::Continue:        return "'continue'";
        case TokenKind::Return:          return "'return'";
        case TokenKind::Case:            return "'case'";
        case TokenKind::Match:           return "'match'";
        case TokenKind::Struct:          return "'struct'";
        case TokenKind::Enum:            return "'enum'";
        case TokenKind::Import:          return "'import'";
        case TokenKind::Export:          return "'export'";
        case TokenKind::As:              return "'as'";
        case TokenKind::Where:           return "'where'";
        case TokenKind::Private:         return "'private'";
        case TokenKind::Coro:            return "'coro'";
        case TokenKind::Yield:           return "'yield'";
        case TokenKind::Constraint:      return "'constraint'";
        case TokenKind::Requires:        return "'requires'";
        case TokenKind::True:            return "'true'";
        case TokenKind::False:           return "'false'";
        case TokenKind::Nil:             return "'nil'";
        case TokenKind::KwInt:           return "'Int'";
        case TokenKind::KwFloat:         return "'Float'";
        case TokenKind::KwString:        return "'String'";
        case TokenKind::KwSymbol:        return "'Symbol'";
        case TokenKind::KwBool:          return "'Bool'";
        case TokenKind::KwVoid:          return "'Void'";
        case TokenKind::KwFraction:      return "'Fraction'";
        case TokenKind::KwComplex:       return "'Complex'";
        case TokenKind::KwAny:           return "'Any'";
        case TokenKind::KwType:          return "'type'";
        case TokenKind::Plus:            return "'+'";
        case TokenKind::Minus:           return "'-'";
        case TokenKind::Star:            return "'*'";
        case TokenKind::Slash:           return "'/'";
        case TokenKind::Percent:         return "'%'";
        case TokenKind::Ampersand:       return "'&'";
        case TokenKind::Pipe:            return "'|'";
        case TokenKind::Caret:           return "'^'";
        case TokenKind::Tilde:           return "'~'";
        case TokenKind::Bang:            return "'!'";
        case TokenKind::Equals:          return "'='";
        case TokenKind::EqEq:            return "'=='";
        case TokenKind::BangEq:          return "'!='";
        case TokenKind::Less:            return "'<'";
        case TokenKind::LessEq:          return "'<='";
        case TokenKind::Greater:         return "'>'";
        case TokenKind::GreaterEq:       return "'>='";
        case TokenKind::PipeGreater:     return "'|>'";
        case TokenKind::Arrow:           return "'->'";
        case TokenKind::FatArrow:        return "'=>'";
        case TokenKind::At:              return "'@'";
        case TokenKind::Dollar:          return "'$'";
        case TokenKind::Hash:            return "'#'";
        case TokenKind::Dot:             return "'.'";
        case TokenKind::DotDot:          return "'..'";
        case TokenKind::Ellipsis:        return "'...'";
        case TokenKind::Colon:           return "':'";
        case TokenKind::ColonColon:      return "'::'";
        case TokenKind::LeftArrow:       return "'<-'";
        case TokenKind::Comma:           return "','";
        case TokenKind::Semicolon:       return "';'";
        case TokenKind::Question:        return "'?'";
        case TokenKind::SlashSlash:      return "'//'";
        case TokenKind::ShiftLeft:       return "'<<'";
        case TokenKind::ShiftRight:      return "'>>'";
        case TokenKind::UShiftRight:     return "'>>>'";
        case TokenKind::AmpAmp:          return "'&&'";
        case TokenKind::PipePipe:        return "'||'";
        case TokenKind::LParen:          return "'('";
        case TokenKind::RParen:          return "')'";
        case TokenKind::LBracket:        return "'['";
        case TokenKind::RBracket:        return "']'";
        case TokenKind::LBrace:          return "'{'";
        case TokenKind::RBrace:          return "'}'";
        case TokenKind::Eof:             return "end of file";
        case TokenKind::Error:           return "error";
    }
    return "unknown token";
}

} // namespace ts
