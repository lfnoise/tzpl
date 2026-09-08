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
//  symbol_scanner.cpp
//  app
//

#include "symbol_scanner.hpp"
#include <algorithm>
#include <climits>
#include <cwctype>
#include <unordered_set>

namespace tzplapp {

bool isIdentStartChar(wchar_t c) {
    return c == L'_' || std::iswalpha((wint_t)c) != 0;
}

bool isIdentBodyChar(wchar_t c) {
    return c == L'_' || std::iswalnum((wint_t)c) != 0;
}

char const* defKindName(DefKind kind) {
    switch (kind) {
    case DefKind::function:     return "fn";
    case DefKind::variable:     return "var";
    case DefKind::constant:     return "let";
    case DefKind::structType:   return "struct";
    case DefKind::enumType:     return "enum";
    case DefKind::enumCase:     return "case";
    case DefKind::typeAlias:    return "type";
    case DefKind::constraint:   return "constraint";
    case DefKind::parameter:    return "param";
    case DefKind::loopVariable: return "for";
    case DefKind::binding:      return "binding";
    }
    return "";
}

void lineColumnAt(std::wstring const& text, int offset, int& line, int& column) {
    line = 0;
    int lineStart = 0;
    int n = std::min(offset, (int)text.size());
    for (int i = 0; i < n; ++i)
        if (text[(size_t)i] == L'\n') { ++line; lineStart = i + 1; }
    column = n - lineStart;
}

std::wstring lineTextAt(std::wstring const& text, int offset, int* outLineStart) {
    int n = (int)text.size();
    offset = std::clamp(offset, 0, n);
    int s = offset;
    while (s > 0 && text[(size_t)s - 1] != L'\n') --s;
    int e = offset;
    while (e < n && text[(size_t)e] != L'\n') ++e;
    if (e > s && text[(size_t)e - 1] == L'\r') --e;
    if (outLineStart) *outLineStart = s;
    return text.substr((size_t)s, (size_t)(e - s));
}

std::wstring identifierAt(std::wstring const& text, int pos, int* outStart) {
    int n = (int)text.size();
    if (outStart) *outStart = -1;
    if (n == 0) return {};
    pos = std::clamp(pos, 0, n);
    auto body = [&](int i) {
        return i >= 0 && i < n && isIdentBodyChar(text[(size_t)i]);
    };
    // Pick the character run to anchor on: the one under the caret, else
    // the one ending right before it (allowing for a trailing `!`).
    int anchor = -1;
    if (body(pos)) anchor = pos;
    else if (body(pos - 1)) anchor = pos - 1;
    else if (pos >= 2 && text[(size_t)pos - 1] == L'!' && body(pos - 2)) anchor = pos - 2;
    else if (pos < n && text[(size_t)pos] == L'`' && body(pos + 1)) anchor = pos + 1;
    if (anchor < 0) return {};
    int s = anchor;
    while (body(s - 1)) --s;
    if (s > 0 && text[(size_t)s - 1] == L'`') --s;   // dynamic-scope variable
    int e = anchor;
    while (body(e + 1)) ++e;
    ++e;
    if (e < n && text[(size_t)e] == L'!') ++e;
    // A run that starts with a digit is a number, not an identifier.
    wchar_t first = text[(size_t)s];
    if (first != L'`' && !isIdentStartChar(first)) return {};
    if (outStart) *outStart = s;
    return text.substr((size_t)s, (size_t)(e - s));
}

bool definitionVisibleAt(Definition const& def, int pos) {
    if (def.topLevel) return true;
    return pos >= def.scopeStart && pos < def.scopeEnd;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

namespace {

struct Token {
    enum Kind { ident, op, punct, literal, eof };
    Kind kind = eof;
    std::wstring text;
    int start = 0;
    int end = 0;

    bool is(wchar_t c) const { return kind == punct && text.size() == 1 && text[0] == c; }
    bool isOp(wchar_t const* s) const { return kind == op && text == s; }
    bool isWord(wchar_t const* s) const { return kind == ident && text == s; }
};

bool isOpChar(wchar_t c) {
    static std::wstring const ops = L"+-*/%<>=!&|^~?:.$@#\\";
    return ops.find(c) != std::wstring::npos;
}

std::vector<Token> lex(std::wstring const& t) {
    std::vector<Token> out;
    int n = (int)t.size();
    auto at = [&](int i) -> wchar_t { return i < n ? t[(size_t)i] : L'\0'; };
    int i = 0;
    while (i < n) {
        wchar_t c = at(i);
        if (std::iswspace((wint_t)c)) { ++i; continue; }
        // Comments
        if (c == L'-' && at(i + 1) == L'-') {
            while (i < n && at(i) != L'\n') ++i;
            continue;
        }
        if (c == L'/' && at(i + 1) == L'*') {
            int depth = 1;
            i += 2;
            while (i < n && depth > 0) {
                if (at(i) == L'/' && at(i + 1) == L'*') { ++depth; i += 2; }
                else if (at(i) == L'*' && at(i + 1) == L'/') { --depth; i += 2; }
                else ++i;
            }
            continue;
        }
        Token tok;
        tok.start = i;
        // Strings
        if (c == L'"' && at(i + 1) == L'"' && at(i + 2) == L'"') {
            i += 3;
            while (i < n && !(at(i) == L'"' && at(i + 1) == L'"' && at(i + 2) == L'"')) ++i;
            i = std::min(n, i + 3);
            tok.kind = Token::literal;
        } else if (c == L'«') {   // « ... »
            ++i;
            while (i < n && at(i) != L'»') ++i;
            i = std::min(n, i + 1);
            tok.kind = Token::literal;
        } else if (c == L'"') {
            ++i;
            while (i < n && at(i) != L'"' && at(i) != L'\n') {
                if (at(i) == L'\\') ++i;
                ++i;
            }
            i = std::min(n, i + 1);
            tok.kind = Token::literal;
        } else if (c == L'\'' && isIdentStartChar(at(i + 1))) {   // 'symbol
            i += 2;
            while (i < n && isIdentBodyChar(at(i))) ++i;
            tok.kind = Token::literal;
        } else if (std::iswdigit((wint_t)c)) {
            ++i;
            while (i < n && (isIdentBodyChar(at(i))
                             || (at(i) == L'.' && std::iswdigit((wint_t)at(i + 1)))))
                ++i;
            tok.kind = Token::literal;
        } else if (isIdentStartChar(c) || (c == L'`' && isIdentStartChar(at(i + 1)))) {
            if (c == L'`') ++i;
            while (i < n && isIdentBodyChar(at(i))) ++i;
            if (at(i) == L'!') ++i;
            tok.kind = Token::ident;
        } else if (c == L'(' || c == L')' || c == L'[' || c == L']'
                   || c == L'{' || c == L'}' || c == L',' || c == L';') {
            ++i;
            tok.kind = Token::punct;
        } else if (isOpChar(c)) {
            while (i < n && isOpChar(at(i))) ++i;
            tok.kind = Token::op;
        } else {
            ++i;
            tok.kind = Token::op;
        }
        tok.end = i;
        tok.text = t.substr((size_t)tok.start, (size_t)(tok.end - tok.start));
        out.push_back(std::move(tok));
    }
    Token e;
    e.kind = Token::eof;
    e.start = e.end = n;
    out.push_back(e);
    return out;
}

bool isKeywordWord(std::wstring const& w) {
    static std::unordered_set<std::wstring> const kw = {
        L"fn", L"let", L"var", L"const", L"if", L"else", L"while", L"for",
        L"in", L"break", L"continue", L"return", L"case", L"match",
        L"struct", L"enum", L"import", L"export", L"as", L"where",
        L"private", L"coro", L"yield", L"constraint", L"requires",
        L"true", L"false", L"nil", L"type", L"async", L"await",
    };
    return kw.count(w) != 0;
}

// ---------------------------------------------------------------------------
// Scanner
// ---------------------------------------------------------------------------

struct Block {
    int start = 0;        // offset of `{`
    int end = INT_MAX;    // offset just past `}` (INT_MAX while open)
    int parent = -1;
    bool enumBody = false;
};

struct PendingDef {
    Definition def;
    int blockIndex = -2;   // -2: use explicit scope; -1: top-level; else block
    bool fromDefPos = false;   // scope starts at the name, not the block
};

class Scanner {
public:
    explicit Scanner(std::wstring const& text) : text_(text), toks_(lex(text)) {}

    ScanResult run() {
        int const count = (int)toks_.size();
        for (i_ = 0; i_ < count - 1;) {
            Token const& t = toks_[(size_t)i_];
            if (t.is(L'{')) { openBlock(t.start); ++i_; continue; }
            if (t.is(L'}')) { closeBlock(t.end); ++i_; continue; }
            if (t.is(L';')) {
                // A declaration without a body: nothing to attach to.
                if ((int)stack_.size() == pendingDepth_) dropPending();
                ++i_;
                continue;
            }
            if (t.kind != Token::ident) { ++i_; continue; }

            if (t.isWord(L"private")) { privateNext_ = true; ++i_; continue; }
            if (t.isWord(L"import") || t.isWord(L"export")) { parseImport(); continue; }
            if (t.isWord(L"fn")) { parseFn(); continue; }
            if (t.isWord(L"let") || t.isWord(L"var") || t.isWord(L"const")) {
                parseLet(t.isWord(L"var") ? DefKind::variable : DefKind::constant);
                continue;
            }
            if (t.isWord(L"struct")) { parseTypeDecl(DefKind::structType); continue; }
            if (t.isWord(L"enum")) { parseTypeDecl(DefKind::enumType); continue; }
            if (t.isWord(L"constraint")) { parseTypeDecl(DefKind::constraint); continue; }
            if (t.isWord(L"type") && toks_[(size_t)i_ + 1].kind == Token::ident) {
                parseTypeDecl(DefKind::typeAlias);
                continue;
            }
            if (t.isWord(L"for")) { parseFor(); continue; }
            if (t.isWord(L"case")) { parseCase(); continue; }
            privateNext_ = false;
            ++i_;
        }
        // Unterminated blocks run to the end of the text.
        finish();
        return std::move(result_);
    }

private:
    Token const& tok(int j) const {
        if (j < 0 || j >= (int)toks_.size()) return toks_.back();
        return toks_[(size_t)j];
    }

    void openBlock(int start) {
        Block b;
        b.start = start;
        b.parent = stack_.empty() ? -1 : stack_.back();
        b.enumBody = pendingEnum_ && (int)stack_.size() == pendingDepth_;
        int idx = (int)blocks_.size();
        blocks_.push_back(b);
        if ((int)stack_.size() == pendingDepth_) {
            for (auto& p : pending_) { p.blockIndex = idx; defs_.push_back(p); }
            dropPending();
        }
        stack_.push_back(idx);
    }

    void closeBlock(int end) {
        if (stack_.empty()) return;
        blocks_[(size_t)stack_.back()].end = end;
        stack_.pop_back();
    }

    void dropPending() {
        pending_.clear();
        pendingDepth_ = -1;
        pendingEnum_ = false;
    }

    // Queue definitions to attach to the next `{` opened at this depth.
    void queueForNextBlock(std::vector<Definition> defs, bool enumBody) {
        pending_.clear();
        for (auto& d : defs) { PendingDef p; p.def = std::move(d); pending_.push_back(std::move(p)); }
        pendingDepth_ = (int)stack_.size();
        pendingEnum_ = enumBody;
    }

    Definition makeDef(Token const& nameTok, DefKind kind) {
        Definition d;
        d.name = nameTok.text;
        d.kind = kind;
        d.start = nameTok.start;
        d.length = nameTok.end - nameTok.start;
        d.isPrivate = privateNext_ || (!d.name.empty() && d.name[0] == L'_');
        privateNext_ = false;
        return d;
    }

    void addInCurrentBlock(Definition d, bool fromDefPos) {
        PendingDef p;
        p.def = std::move(d);
        p.blockIndex = stack_.empty() ? -1 : stack_.back();
        p.fromDefPos = fromDefPos;
        defs_.push_back(std::move(p));
    }

    void addWithScope(Definition d, int scopeStart, int scopeEnd) {
        PendingDef p;
        p.def = std::move(d);
        p.def.scopeStart = scopeStart;
        p.def.scopeEnd = scopeEnd;
        p.blockIndex = -2;
        defs_.push_back(std::move(p));
    }

    // Skip a `<...>` generic parameter list starting at tok(j) (an op token
    // beginning with `<`). Returns the index after the closing `>`.
    int skipGenerics(int j) {
        int depth = 0;
        for (; tok(j).kind != Token::eof; ++j) {
            Token const& t = tok(j);
            if (t.kind == Token::op) {
                for (size_t k = 0; k < t.text.size(); ++k) {
                    wchar_t c = t.text[k];
                    if (c == L'<') ++depth;
                    else if (c == L'>' && !(k > 0 && t.text[k - 1] == L'-')) --depth;
                }
                if (depth <= 0) return j + 1;
            } else if (t.is(L'{') || t.is(L';')) {
                return j;   // malformed: give up before the body
            }
        }
        return j;
    }

    // Parse `(a T, b U = 5, ...)` at tok(j) == `(`. Parameter names are the
    // first identifier after `(` or a top-level `,`. Returns the index
    // after the closing `)`.
    int parseParams(int j, std::vector<Definition>& out) {
        if (!tok(j).is(L'(')) return j;
        int depth = 0;
        bool expectName = false;
        for (; tok(j).kind != Token::eof; ++j) {
            Token const& t = tok(j);
            if (t.is(L'(') || t.is(L'[') || t.is(L'{')) {
                ++depth;
                if (depth == 1) expectName = true;
                continue;
            }
            if (t.is(L')') || t.is(L']') || t.is(L'}')) {
                --depth;
                if (depth <= 0) return j + 1;
                continue;
            }
            if (depth == 1 && t.is(L',')) { expectName = true; continue; }
            if (depth == 1 && expectName && t.kind == Token::ident
                && !isKeywordWord(t.text)) {
                out.push_back(makeDef(t, DefKind::parameter));
                expectName = false;
            } else if (depth == 1 && expectName && t.kind == Token::op) {
                // `..rest` / `...rest` variadic markers: the name follows.
                continue;
            } else {
                expectName = false;
            }
        }
        return j;
    }

    // After a parameter list: attach `params` to the block body that
    // follows, or to an `= expr;` expression body. Returns the index to
    // resume scanning from (the `{`, the `=`, or the `;`).
    int attachBody(int j, std::vector<Definition> params) {
        int depth = 0;
        for (int k = j; tok(k).kind != Token::eof; ++k) {
            Token const& t = tok(k);
            if (t.is(L'(') || t.is(L'[')) { ++depth; continue; }
            if (t.is(L')') || t.is(L']')) { --depth; continue; }
            if (depth > 0) continue;
            if (t.is(L'{')) {
                queueForNextBlock(std::move(params), false);
                return k;
            }
            if (t.isOp(L"=")) {
                // Expression body: visible until the terminating `;`.
                int end = statementEnd(k + 1);
                for (auto& p : params) addWithScope(std::move(p), t.start, end);
                return k + 1;
            }
            if (t.is(L';') || t.is(L'}')) return k;
        }
        return j;
    }

    // Offset just past the `;` that ends the statement starting at tok(j),
    // skipping nested brackets. The end of the text if there is none.
    int statementEnd(int j) {
        int depth = 0;
        for (int k = j; tok(k).kind != Token::eof; ++k) {
            Token const& t = tok(k);
            if (t.is(L'(') || t.is(L'[') || t.is(L'{')) ++depth;
            else if (t.is(L')') || t.is(L']') || t.is(L'}')) {
                if (depth == 0) return t.start;
                --depth;
            }
            else if (depth == 0 && t.is(L';')) return t.end;
        }
        return (int)text_.size();
    }

    void parseFn() {
        int j = i_ + 1;
        Token const& nameTok = tok(j);
        std::vector<Definition> params;
        if (nameTok.is(L'(')) {
            // Lambda: no name, just parameters.
            privateNext_ = false;
            j = parseParams(j, params);
        } else if (nameTok.kind == Token::ident && !isKeywordWord(nameTok.text)) {
            addInCurrentBlock(makeDef(nameTok, DefKind::function), false);
            ++j;
            if (tok(j).kind == Token::op && tok(j).text[0] == L'<') j = skipGenerics(j);
            j = parseParams(j, params);
        } else if (nameTok.kind == Token::op && !tok(j + 1).is(L'{')) {
            // Operator overload: fn +(a Int, b Int) Int
            addInCurrentBlock(makeDef(nameTok, DefKind::function), false);
            ++j;
            j = parseParams(j, params);
        } else {
            privateNext_ = false;
            i_ = j;
            return;
        }
        i_ = attachBody(j, std::move(params));
        if (i_ <= j - 1) i_ = j;   // always make progress
    }

    // Identifiers bound by a let/var/const/for/case pattern between tok(j)
    // and the first token satisfying `stop` at depth 0. A name counts when
    // it follows the keyword, `(`, `,`, `{`, `[` or `::`, is not itself a
    // constructor (followed by `(`/`{`) or qualified (`.` on either side),
    // and is not `_`.
    template <typename Stop>
    int collectPattern(int j, DefKind kind, std::vector<Definition>& out, Stop stop) {
        int depth = 0;
        bool afterOpener = true;
        for (; tok(j).kind != Token::eof; ++j) {
            Token const& t = tok(j);
            if (depth == 0 && stop(t)) return j;
            if (t.is(L'(') || t.is(L'[') || t.is(L'{')) { ++depth; afterOpener = true; continue; }
            if (t.is(L')') || t.is(L']') || t.is(L'}')) { --depth; afterOpener = false; if (depth < 0) return j; continue; }
            if (t.is(L',') || t.isOp(L"::")) { afterOpener = true; continue; }
            if (t.kind == Token::ident && afterOpener && !isKeywordWord(t.text)
                && t.text != L"_") {
                Token const& next = tok(j + 1);
                bool ctor = next.is(L'(') || next.is(L'{');
                bool qualified = (next.kind == Token::op && !next.text.empty()
                                  && next.text[0] == L'.');
                if (!ctor && !qualified) out.push_back(makeDef(t, kind));
            }
            afterOpener = false;
        }
        return j;
    }

    void parseLet(DefKind kind) {
        std::vector<Definition> names;
        int j = collectPattern(i_ + 1, kind, names, [](Token const& t) {
            return t.isOp(L"=") || t.is(L';');
        });
        for (auto& d : names) addInCurrentBlock(std::move(d), true);
        privateNext_ = false;
        i_ = std::max(j, i_ + 1);
    }

    void parseTypeDecl(DefKind kind) {
        Token const& nameTok = tok(i_ + 1);
        if (nameTok.kind == Token::ident && !isKeywordWord(nameTok.text)) {
            addInCurrentBlock(makeDef(nameTok, kind), false);
            if (kind == DefKind::enumType) {
                // The `{` that follows is the enum's body: its `case`s
                // define constructors, not match bindings.
                queueForNextBlock({}, true);
            }
            i_ += 2;
        } else {
            privateNext_ = false;
            ++i_;
        }
    }

    void parseFor() {
        std::vector<Definition> names;
        int j = collectPattern(i_ + 1, DefKind::loopVariable, names, [](Token const& t) {
            return t.isWord(L"in") || t.is(L'{') || t.is(L';');
        });
        privateNext_ = false;
        if (tok(j).isWord(L"in")) queueForNextBlock(std::move(names), false);
        i_ = std::max(j, i_ + 1);
    }

    void parseCase() {
        privateNext_ = false;
        bool inEnum = !stack_.empty() && blocks_[(size_t)stack_.back()].enumBody;
        if (inEnum) {
            Token const& nameTok = tok(i_ + 1);
            if (nameTok.kind == Token::ident && !isKeywordWord(nameTok.text)) {
                // Visible wherever the enum itself is.
                PendingDef p;
                p.def = makeDef(nameTok, DefKind::enumCase);
                p.def.isPrivate = false;
                p.blockIndex = blocks_[(size_t)stack_.back()].parent;
                defs_.push_back(std::move(p));
                i_ += 2;
            } else {
                ++i_;
            }
            return;
        }
        // Match arm: bindings live from here to the end of the arm.
        std::vector<Definition> names;
        int j = collectPattern(i_ + 1, DefKind::binding, names, [](Token const& t) {
            return t.isOp(L"=>") || t.is(L';');
        });
        int armEnd = INT_MAX;
        if (tok(j).isOp(L"=>")) {
            // Up to the next `case` at this depth or the enclosing `}`.
            int depth = 0;
            for (int k = j + 1; tok(k).kind != Token::eof; ++k) {
                Token const& t = tok(k);
                if (t.is(L'(') || t.is(L'[') || t.is(L'{')) ++depth;
                else if (t.is(L')') || t.is(L']') || t.is(L'}')) {
                    if (depth == 0) { armEnd = t.start; break; }
                    --depth;
                } else if (depth == 0 && t.isWord(L"case")) { armEnd = t.start; break; }
            }
        }
        int armStart = tok(i_).start;
        for (auto& d : names) addWithScope(std::move(d), armStart, armEnd);
        i_ = std::max(j, i_ + 1);
    }

    void parseImport() {
        ImportSpec spec;
        spec.isExport = tok(i_).isWord(L"export");
        spec.start = tok(i_).start;
        int j = i_ + 1;
        // Dotted path
        while (tok(j).kind == Token::ident && !isKeywordWord(tok(j).text)) {
            spec.path.push_back(tok(j).text);
            ++j;
            if (tok(j).isOp(L".")) { ++j; continue; }
            if (tok(j).isOp(L".*")) { spec.wildcard = true; ++j; }
            break;
        }
        if (tok(j).is(L'{')) {
            // Selective list: { a, b as c }
            ++j;
            while (tok(j).kind != Token::eof && !tok(j).is(L'}') && !tok(j).is(L';')) {
                if (tok(j).kind == Token::ident && !isKeywordWord(tok(j).text)) {
                    std::wstring name = tok(j).text, alias;
                    ++j;
                    if (tok(j).isWord(L"as") && tok(j + 1).kind == Token::ident) {
                        alias = tok(j + 1).text;
                        j += 2;
                    }
                    spec.names.emplace_back(name, alias);
                } else {
                    ++j;
                }
            }
            if (tok(j).is(L'}')) ++j;
        } else if (tok(j).isWord(L"as") && tok(j + 1).kind == Token::ident) {
            spec.alias = tok(j + 1).text;
            j += 2;
        }
        while (tok(j).kind != Token::eof && !tok(j).is(L';')) ++j;
        if (tok(j).is(L';')) ++j;
        if (!spec.path.empty()) result_.imports.push_back(std::move(spec));
        privateNext_ = false;
        i_ = std::max(j, i_ + 1);
    }

    void finish() {
        int n = (int)text_.size();
        for (auto& b : blocks_) if (b.end == INT_MAX) b.end = n;
        for (auto& p : defs_) {
            Definition d = std::move(p.def);
            if (p.blockIndex == -1) {
                d.topLevel = true;
                d.scopeStart = 0;
                d.scopeEnd = INT_MAX;
            } else if (p.blockIndex >= 0) {
                Block const& b = blocks_[(size_t)p.blockIndex];
                d.scopeStart = p.fromDefPos ? d.start : b.start;
                d.scopeEnd = b.end;
            }
            lineColumnAt(text_, d.start, d.line, d.column);
            result_.defs.push_back(std::move(d));
        }
        std::stable_sort(result_.defs.begin(), result_.defs.end(),
                         [](Definition const& a, Definition const& b) {
                             return a.start < b.start;
                         });
    }

    std::wstring const& text_;
    std::vector<Token> toks_;
    int i_ = 0;
    std::vector<Block> blocks_;
    std::vector<int> stack_;
    std::vector<PendingDef> defs_;
    std::vector<PendingDef> pending_;
    int pendingDepth_ = -1;
    bool pendingEnum_ = false;
    bool privateNext_ = false;
    ScanResult result_;
};

}

ScanResult scanSource(std::wstring const& text) {
    return Scanner(text).run();
}

}
