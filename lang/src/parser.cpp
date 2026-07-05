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
//  parser.cpp
//  lang
//
//  Recursive descent parser with Pratt expression parsing
//

#include "parser.hpp"
#include <set>

namespace ts {

// Helper: returns true if the token kind is a type keyword (Int, Float, String, etc.)
static bool isTypeKeyword(TokenKind kind) {
    return kind == TokenKind::KwInt || kind == TokenKind::KwFloat ||
           kind == TokenKind::KwString || kind == TokenKind::KwBool ||
           kind == TokenKind::KwSymbol || kind == TokenKind::KwVoid ||
           kind == TokenKind::KwFraction || kind == TokenKind::KwComplex ||
           kind == TokenKind::KwAny;
}

Parser::Parser(Lexer& lexer)
    : lexer_(lexer)
{
    advance();
}

Token Parser::advance() {
    previous_ = current_;
    current_ = lexer_.nextToken();
    return previous_;
}

bool Parser::check(TokenKind kind) const {
    return current_.kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenKind kind, const std::string& msg) {
    if (check(kind)) {
        return advance();
    }
    error(msg + ", got " + tokenKindString(current_.kind));
    return current_;
}

Token Parser::expectClosing(TokenKind kind, const char* open, SourceRange openLoc) {
    if (check(kind)) {
        return advance();
    }
    std::string msg = std::string("Expected ") + tokenKindString(kind)
        + ", got " + tokenKindString(current_.kind);
    error(current_.loc, msg);
    // Add a note pointing to the opening delimiter
    if (!errors_.empty()) {
        errors_.back().notes.emplace_back(openLoc,
            std::string("to match ") + open + " here",
            lexer_.filename(), lexer_.source());
    }
    return current_;
}

bool Parser::matchGreater() {
    if (check(TokenKind::Greater)) {
        advance();
        return true;
    }
    // Split ">>" into ">" + ">", consume the first
    if (check(TokenKind::ShiftRight)) {
        current_.kind = TokenKind::Greater;
        current_.text = ">";
        current_.loc.start.col++;
        current_.loc.start.offset++;
        return true;
    }
    // Split ">=" into ">" + "=", consume the first
    if (check(TokenKind::GreaterEq)) {
        current_.kind = TokenKind::Equals;
        current_.text = "=";
        current_.loc.start.col++;
        current_.loc.start.offset++;
        return true;
    }
    return false;
}

void Parser::expectTerminator() {
    if (current_.kind == TokenKind::Semicolon) {
        advance();
        return;
    }
    if (current_.kind == TokenKind::Eof) {
        return;
    }
    if (current_.kind == TokenKind::RBrace) {
        return;  // Block end is an implicit terminator
    }
    error(std::string("Expected ';', got ") + tokenKindString(current_.kind));
}

void Parser::error(const std::string& msg) {
    error(current_.loc, msg);
}

void Parser::error(SourceRange loc, const std::string& msg) {
    // Suppress cascading errors at the same source position
    if (!errors_.empty() && errors_.back().loc.start.offset == loc.start.offset) return;
    errors_.push_back(CompileError(CompileError::ParseError, loc, msg,
                                   lexer_.filename(), lexer_.source()));
}

void Parser::synchronize() {
    // synchronize() is always invoked from a no-progress error-recovery guard,
    // so it must consume at least one token before honoring its stop conditions.
    // Without this, a sequence like `case A; case B;` inside an enum body
    // leaves previous_ pinned at Semicolon after the first recovery pass;
    // subsequent calls would early-return and the outer loop would spin.
    //
    // Stop conditions (only after we have advanced at least once):
    //   - current_ is a statement-starter sync token (stop AT, do not consume)
    //   - previous_ is a Semicolon (stop just AFTER a statement terminator)
    bool advanced = false;
    while (current_.kind != TokenKind::Eof) {
        if (advanced) {
            if (previous_.kind == TokenKind::Semicolon) return;
            switch (current_.kind) {
                case TokenKind::Fn:
                case TokenKind::Let:
                case TokenKind::Var:
                case TokenKind::Const:
                case TokenKind::Struct:
                case TokenKind::Enum:
                case TokenKind::Import:
                case TokenKind::If:
                case TokenKind::While:
                case TokenKind::For:
                case TokenKind::Return:
                case TokenKind::Match:
                case TokenKind::KwType:
                case TokenKind::Private:
                case TokenKind::Coro:
                case TokenKind::RBrace:
                    return;
                default:
                    break;
            }
        }
        advance();
        advanced = true;
    }
}

SourceRange Parser::currentLoc() const {
    return current_.loc;
}

// --- Program ---

Program Parser::parseProgram() {
    Program prog;

    while (current_.kind != TokenKind::Eof) {
        u32 offsetBefore = current_.loc.start.offset;
        auto node = parseDeclaration();
        if (node) {
            prog.items.push_back(std::move(node));
        }
        if (current_.loc.start.offset == offsetBefore) {
            synchronize();
        }
    }

    return prog;
}

// --- Declarations ---

ASTPtr Parser::parseDeclaration() {
    // Handle 'private' prefix
    if (current_.kind == TokenKind::Private) {
        advance(); // consume 'private'
        auto decl = parseDeclaration();
        if (decl) {
            switch (decl->kind) {
                case ASTNode::FnDecl:
                    static_cast<FnDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::LetDecl:
                    static_cast<LetDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::VarDecl:
                    static_cast<VarDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::ConstDecl:
                    static_cast<ConstDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::StructDecl:
                    static_cast<StructDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::UnionDecl:
                    static_cast<UnionDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::TypeAliasDecl:
                    static_cast<TypeAliasDeclNode*>(decl.get())->isPrivate = true; break;
                case ASTNode::ConstraintDecl:
                    static_cast<ConstraintDeclNode*>(decl.get())->isPrivate = true; break;
                default:
                    error(decl->loc, "'private' can only be applied to declarations");
                    break;
            }
        }
        return decl;
    }

    switch (current_.kind) {
        case TokenKind::Import:
        case TokenKind::Export:
            return parseImportDecl();
        case TokenKind::Coro: {
            advance(); // consume 'coro'
            if (!check(TokenKind::Fn)) { error("Expected 'fn' after 'coro'"); return nullptr; }
            auto node = parseFnDecl();
            if (node) static_cast<FnDeclNode*>(node.get())->isCoroutine = true;
            return node;
        }
        case TokenKind::Async: {
            advance(); // consume 'async'
            if (!check(TokenKind::Fn)) { error("Expected 'fn' after 'async'"); return nullptr; }
            auto node = parseFnDecl();
            if (node) static_cast<FnDeclNode*>(node.get())->isAsync = true;
            return node;
        }
        case TokenKind::Fn:
            // If next token is '(' it's a lambda expression, not a named declaration
            if (lexer_.peek().kind == TokenKind::LParen)
                return parseStatement();
            return parseFnDecl();
        case TokenKind::Let:
            return parseLetDecl();
        case TokenKind::Var:
            return parseVarDecl();
        case TokenKind::Const:
            return parseConstDecl();
        case TokenKind::Struct:
            return parseStructDecl();
        case TokenKind::Enum:
            return parseUnionDecl();
        case TokenKind::KwType:
            return parseTypeAliasDecl();
        case TokenKind::Constraint:
            return parseConstraintDecl();
        default:
            return parseStatement();
    }
}

ASTPtr Parser::parseImportDecl() {
    SourceRange start = currentLoc();
    // `import ...` brings symbols into the current module's scope without
    // re-exporting them. `export ...` does the same and also re-exports them.
    // The rest of the grammar is identical.
    bool isReExport = (current_.kind == TokenKind::Export);
    const char* kw = isReExport ? "export" : "import";
    advance(); // consume 'import' or 'export'

    // Parse dotted path: import std.math.foo
    std::vector<std::string> modulePath;
    Token first = expect(TokenKind::Identifier,
        std::string("Expected module name after '") + kw + "'");
    modulePath.push_back(first.text);

    while (check(TokenKind::Dot)) {
        advance(); // consume '.'

        // Check for wildcard: import std.math.*
        if (check(TokenKind::Star)) {
            advance(); // consume '*'
            expectTerminator();
            auto node = std::make_unique<ImportDeclNode>(start, std::move(modulePath),
                ImportKind::Wildcard, "", std::vector<ImportName>{});
            node->isReExport = isReExport;
            return node;
        }

        // Check for named imports: import std.math.{sin, cos}
        if (check(TokenKind::LBrace)) {
            SourceRange openLoc = currentLoc();
            advance(); // consume '{'
            std::vector<ImportName> names;
            do {
                Token name = expect(TokenKind::Identifier, "Expected name in import list");
                ImportName imp;
                imp.name = name.text;
                if (match(TokenKind::As)) {
                    Token alias = expect(TokenKind::Identifier, "Expected alias after 'as'");
                    imp.alias = alias.text;
                }
                names.push_back(std::move(imp));
            } while (match(TokenKind::Comma));
            expectClosing(TokenKind::RBrace, "{", openLoc);
            expectTerminator();
            auto node = std::make_unique<ImportDeclNode>(start, std::move(modulePath),
                ImportKind::Named, "", std::move(names));
            node->isReExport = isReExport;
            return node;
        }

        // Otherwise another path component
        Token next = expect(TokenKind::Identifier, "Expected module name after '.'");
        modulePath.push_back(next.text);
    }

    // Whole module import, possibly with alias
    std::string alias;
    if (match(TokenKind::As)) {
        Token aliasTok = expect(TokenKind::Identifier, "Expected alias after 'as'");
        alias = aliasTok.text;
    }

    expectTerminator();
    auto node = std::make_unique<ImportDeclNode>(start, std::move(modulePath),
        ImportKind::Whole, std::move(alias), std::vector<ImportName>{});
    node->isReExport = isReExport;
    return node;
}

ASTPtr Parser::parseLetDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'let'

    // Check for pattern destructuring: let (x, y) = ..., let [a, b] = ..., let Name { ... } = ...
    if (check(TokenKind::LParen) || check(TokenKind::LBracket)) {
        PatternPtr pat = parsePattern();
        expect(TokenKind::Equals, "Expected '=' after pattern in let declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<LetDeclNode>(start, std::move(pat), std::move(init));
    }

    Token name = expect(TokenKind::Identifier, "Expected variable name after 'let'");

    // Check for struct pattern: let Name { field: pat, ... } = ...
    if (check(TokenKind::LBrace)) {
        // Re-parse as struct pattern using the name we already consumed
        SourceRange openLoc = currentLoc();
        advance(); // consume {
        std::vector<StructPatternField> fields;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            u32 offsetBefore = current_.loc.start.offset;
            StructPatternField field;
            field.loc = currentLoc();
            Token fieldName = expect(TokenKind::Identifier, "Expected field name in struct pattern");
            field.name = fieldName.text;
            expect(TokenKind::Colon, "Expected ':' after field name in struct pattern");
            field.pattern = parsePattern();
            fields.push_back(std::move(field));
            if (check(TokenKind::Comma)) advance();
            if (current_.loc.start.offset == offsetBefore) { synchronize(); }
        }
        expectClosing(TokenKind::RBrace, "{", openLoc);
        auto pat = std::make_unique<StructPattern>(name.loc, name.text, std::move(fields));
        expect(TokenKind::Equals, "Expected '=' after pattern in let declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<LetDeclNode>(start, std::move(pat), std::move(init));
    }

    // Check for tuple struct pattern: let Name(pat, pat) = ...
    // But NOT a type annotation like: let add (Int, Int) Int = ...
    // Disambiguate by peeking at what follows '(' — type keywords or ')' mean type annotation
    if (check(TokenKind::LParen) && !isTypeKeyword(lexer_.peek().kind)
        && lexer_.peek().kind != TokenKind::RParen) {
        SourceRange openLoc = currentLoc();
        advance(); // consume (
        std::vector<PatternPtr> elements;
        bool hasRest = false;
        std::string restName;
        if (!check(TokenKind::RParen)) {
            while (true) {
                if (check(TokenKind::Ellipsis)) {
                    advance(); hasRest = true;
                    if (check(TokenKind::Identifier) && current_.text != "_") {
                        restName = current_.text; advance();
                    }
                    break;
                }
                elements.push_back(parsePattern());
                if (!match(TokenKind::Comma)) {
                    if (check(TokenKind::Ellipsis)) {
                        advance(); hasRest = true;
                        if (check(TokenKind::Identifier) && current_.text != "_") {
                            restName = current_.text; advance();
                        }
                    }
                    break;
                }
            }
        }
        expectClosing(TokenKind::RParen, "(", openLoc);
        auto pat = std::make_unique<TuplePattern>(name.loc, std::move(elements),
            hasRest, std::move(restName), name.text);
        expect(TokenKind::Equals, "Expected '=' after pattern in let declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<LetDeclNode>(start, std::move(pat), std::move(init));
    }

    // Optional type annotation (type follows name: `let x int = 42`)
    TypeExprPtr typeExpr;
    if (current_.kind != TokenKind::Equals
        && current_.kind != TokenKind::Eof) {
        // Check if next token looks like a type (identifier that is a type keyword)
        if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
            current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
            current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
            current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
            current_.kind == TokenKind::KwAny ||
            current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::Hash ||
            current_.kind == TokenKind::LParen) {
            typeExpr = parseTypeExpr();
        }
    }

    expect(TokenKind::Equals, "Expected '=' in let declaration");

    ExprPtr init = parseExpression();
    expectTerminator();

    return std::make_unique<LetDeclNode>(start, name.text, std::move(typeExpr), std::move(init));
}

ASTPtr Parser::parseVarDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'var'

    // Check for pattern destructuring
    if (check(TokenKind::LParen) || check(TokenKind::LBracket)) {
        PatternPtr pat = parsePattern();
        expect(TokenKind::Equals, "Expected '=' after pattern in var declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<VarDeclNode>(start, std::move(pat), std::move(init));
    }

    // Dynamic scope variable: var `name [Type] = expr;
    if (check(TokenKind::DynamicVar)) {
        Token dynName = advance();
        TypeExprPtr typeExpr;
        if (current_.kind != TokenKind::Equals
            && current_.kind != TokenKind::Eof) {
            if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
                current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
                current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
                current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
                current_.kind == TokenKind::KwAny ||
                current_.kind == TokenKind::Identifier ||
                current_.kind == TokenKind::LBracket ||
                current_.kind == TokenKind::LParen) {
                typeExpr = parseTypeExpr();
            }
        }
        expect(TokenKind::Equals, "Expected '=' in dynamic variable declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        auto decl = std::make_unique<VarDeclNode>(start, dynName.text, std::move(typeExpr), std::move(init));
        decl->isDynamic = true;
        return decl;
    }

    Token name = expect(TokenKind::Identifier, "Expected variable name after 'var'");

    // Check for struct pattern
    if (check(TokenKind::LBrace)) {
        SourceRange openLoc = currentLoc();
        advance(); // consume {
        std::vector<StructPatternField> fields;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            u32 offsetBefore = current_.loc.start.offset;
            StructPatternField field;
            field.loc = currentLoc();
            Token fieldName = expect(TokenKind::Identifier, "Expected field name in struct pattern");
            field.name = fieldName.text;
            expect(TokenKind::Colon, "Expected ':' after field name in struct pattern");
            field.pattern = parsePattern();
            fields.push_back(std::move(field));
            if (check(TokenKind::Comma)) advance();
            if (current_.loc.start.offset == offsetBefore) { synchronize(); }
        }
        expectClosing(TokenKind::RBrace, "{", openLoc);
        auto pat = std::make_unique<StructPattern>(name.loc, name.text, std::move(fields));
        expect(TokenKind::Equals, "Expected '=' after pattern in var declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<VarDeclNode>(start, std::move(pat), std::move(init));
    }

    // Check for tuple struct pattern: var Name(pat, pat) = ...
    if (check(TokenKind::LParen) && !isTypeKeyword(lexer_.peek().kind)
        && lexer_.peek().kind != TokenKind::RParen) {
        SourceRange openLoc = currentLoc();
        advance(); // consume (
        std::vector<PatternPtr> elements;
        bool hasRest = false;
        std::string restName;
        if (!check(TokenKind::RParen)) {
            while (true) {
                if (check(TokenKind::Ellipsis)) {
                    advance(); hasRest = true;
                    if (check(TokenKind::Identifier) && current_.text != "_") {
                        restName = current_.text; advance();
                    }
                    break;
                }
                elements.push_back(parsePattern());
                if (!match(TokenKind::Comma)) {
                    if (check(TokenKind::Ellipsis)) {
                        advance(); hasRest = true;
                        if (check(TokenKind::Identifier) && current_.text != "_") {
                            restName = current_.text; advance();
                        }
                    }
                    break;
                }
            }
        }
        expectClosing(TokenKind::RParen, "(", openLoc);
        auto pat = std::make_unique<TuplePattern>(name.loc, std::move(elements),
            hasRest, std::move(restName), name.text);
        expect(TokenKind::Equals, "Expected '=' after pattern in var declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<VarDeclNode>(start, std::move(pat), std::move(init));
    }

    TypeExprPtr typeExpr;
    if (current_.kind != TokenKind::Equals
        && current_.kind != TokenKind::Eof) {
        if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
            current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
            current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
            current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
            current_.kind == TokenKind::KwAny ||
            current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::Hash ||
            current_.kind == TokenKind::LParen) {
            typeExpr = parseTypeExpr();
        }
    }

    expect(TokenKind::Equals, "Expected '=' in var declaration");

    ExprPtr init = parseExpression();
    expectTerminator();

    return std::make_unique<VarDeclNode>(start, name.text, std::move(typeExpr), std::move(init));
}

ASTPtr Parser::parseConstDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'const'

    // Check for pattern destructuring
    if (check(TokenKind::LParen) || check(TokenKind::LBracket)) {
        PatternPtr pat = parsePattern();
        expect(TokenKind::Equals, "Expected '=' after pattern in const declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<ConstDeclNode>(start, std::move(pat), std::move(init));
    }

    Token name = expect(TokenKind::Identifier, "Expected variable name after 'const'");

    // Check for struct pattern
    if (check(TokenKind::LBrace)) {
        SourceRange openLoc = currentLoc();
        advance(); // consume {
        std::vector<StructPatternField> fields;
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            u32 offsetBefore = current_.loc.start.offset;
            StructPatternField field;
            field.loc = currentLoc();
            Token fieldName = expect(TokenKind::Identifier, "Expected field name in struct pattern");
            field.name = fieldName.text;
            expect(TokenKind::Colon, "Expected ':' after field name in struct pattern");
            field.pattern = parsePattern();
            fields.push_back(std::move(field));
            if (check(TokenKind::Comma)) advance();
            if (current_.loc.start.offset == offsetBefore) { synchronize(); }
        }
        expectClosing(TokenKind::RBrace, "{", openLoc);
        auto pat = std::make_unique<StructPattern>(name.loc, name.text, std::move(fields));
        expect(TokenKind::Equals, "Expected '=' after pattern in const declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<ConstDeclNode>(start, std::move(pat), std::move(init));
    }

    // Check for tuple struct pattern: const Name(pat, pat) = ...
    if (check(TokenKind::LParen) && !isTypeKeyword(lexer_.peek().kind)
        && lexer_.peek().kind != TokenKind::RParen) {
        SourceRange openLoc = currentLoc();
        advance(); // consume (
        std::vector<PatternPtr> elements;
        bool hasRest = false;
        std::string restName;
        if (!check(TokenKind::RParen)) {
            while (true) {
                if (check(TokenKind::Ellipsis)) {
                    advance(); hasRest = true;
                    if (check(TokenKind::Identifier) && current_.text != "_") {
                        restName = current_.text; advance();
                    }
                    break;
                }
                elements.push_back(parsePattern());
                if (!match(TokenKind::Comma)) {
                    if (check(TokenKind::Ellipsis)) {
                        advance(); hasRest = true;
                        if (check(TokenKind::Identifier) && current_.text != "_") {
                            restName = current_.text; advance();
                        }
                    }
                    break;
                }
            }
        }
        expectClosing(TokenKind::RParen, "(", openLoc);
        auto pat = std::make_unique<TuplePattern>(name.loc, std::move(elements),
            hasRest, std::move(restName), name.text);
        expect(TokenKind::Equals, "Expected '=' after pattern in const declaration");
        ExprPtr init = parseExpression();
        expectTerminator();
        return std::make_unique<ConstDeclNode>(start, std::move(pat), std::move(init));
    }

    TypeExprPtr typeExpr;
    if (current_.kind != TokenKind::Equals
        && current_.kind != TokenKind::Eof) {
        if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
            current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
            current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
            current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
            current_.kind == TokenKind::KwAny ||
            current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::Hash ||
            current_.kind == TokenKind::LParen) {
            typeExpr = parseTypeExpr();
        }
    }

    expect(TokenKind::Equals, "Expected '=' in const declaration");

    ExprPtr init = parseExpression();
    expectTerminator();

    return std::make_unique<ConstDeclNode>(start, name.text, std::move(typeExpr), std::move(init));
}

ASTPtr Parser::parseFnDecl() {
    SourceRange start = currentLoc();
    size_t errorsBefore = errors_.size();
    advance(); // consume 'fn'

    // Allow operator tokens as function names for operator overloading
    Token name;
    if (current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus ||
        current_.kind == TokenKind::Star || current_.kind == TokenKind::Slash ||
        current_.kind == TokenKind::Percent || current_.kind == TokenKind::EqEq ||
        current_.kind == TokenKind::BangEq || current_.kind == TokenKind::Less ||
        current_.kind == TokenKind::LessEq || current_.kind == TokenKind::Greater ||
        current_.kind == TokenKind::GreaterEq ||
        current_.kind == TokenKind::Tilde || current_.kind == TokenKind::Ampersand ||
        current_.kind == TokenKind::Pipe || current_.kind == TokenKind::Caret ||
        current_.kind == TokenKind::ShiftLeft || current_.kind == TokenKind::ShiftRight ||
        current_.kind == TokenKind::UShiftRight ||
        current_.kind == TokenKind::Bang || current_.kind == TokenKind::SlashSlash ||
        current_.kind == TokenKind::Dollar || current_.kind == TokenKind::LeftArrow ||
        current_.kind == TokenKind::Arrow) {
        name = advance();
    } else {
        name = expect(TokenKind::Identifier, "Expected function name after 'fn'");
    }

    // Template type parameters: fn name<T, U>(...)  or  fn name<T: Comparable>(...)
    std::vector<std::string> typeParams;
    std::vector<WhereConstraint> whereConstraints;
    if (check(TokenKind::Less)) {
        advance(); // consume '<'
        do {
            Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
            typeParams.push_back(tp.text);
            parseInlineConstraints(tp.text, tp, whereConstraints);
        } while (match(TokenKind::Comma));
        if (!matchGreater()) {
            error("Expected '>' after type parameters");
        }
    }

    // Parameters
    SourceRange parenLoc = currentLoc();
    expect(TokenKind::LParen, "Expected '(' after function name");
    std::vector<FnParam> params;
    if (!check(TokenKind::RParen)) {
        do {
            FnParam param;
            param.loc = currentLoc();
            // Check for variadic parameter: ...name or ...name Type
            if (match(TokenKind::Ellipsis)) {
                param.isVariadic = true;
                Token paramName = expect(TokenKind::Identifier, "Expected parameter name after '...'");
                param.name = paramName.text;
                // Optional element type annotation
                if (!check(TokenKind::Comma) && !check(TokenKind::RParen)) {
                    param.typeExpr = parseTypeExpr();
                }
                params.push_back(std::move(param));
                break;  // variadic must be last parameter
            }
            Token paramName = expect(TokenKind::Identifier, "Expected parameter name");
            param.name = paramName.text;
            // If next token is ',' or ')' or '=', the type is omitted (sugar for template param)
            if (!check(TokenKind::Comma) && !check(TokenKind::RParen) && !check(TokenKind::Equals)) {
                param.typeExpr = parseTypeExpr();
            }
            // Optional default value: = expr
            if (match(TokenKind::Equals)) {
                param.defaultExpr = parseExpression();
            }
            params.push_back(std::move(param));
        } while (match(TokenKind::Comma));
    }
    expectClosing(TokenKind::RParen, "(", parenLoc);

    // Validate default argument ordering:
    // - Once a default is seen, all subsequent non-variadic params must have defaults
    // - Variadic params cannot have defaults
    {
        bool seenDefault = false;
        for (auto& param : params) {
            if (param.isVariadic && param.defaultExpr) {
                error("Variadic parameters cannot have default values");
            }
            if (param.defaultExpr) {
                seenDefault = true;
            } else if (seenDefault && !param.isVariadic) {
                error("Non-default parameter '" + param.name + "' follows a parameter with a default value");
            }
        }
        // (Default params without type annotations are allowed —
        // their type is inferred from the default value during type checking.)
    }

    // Generate synthetic type parameters for untyped parameters.
    // fn foo(a, b) desugars to fn foo<A,B>(a A, b B)
    // Skip variadic params without type — their type is determined per call site.
    // Skip params with defaults — their type is inferred from the default value.
    {
        std::set<std::string> usedNames(typeParams.begin(), typeParams.end());
        for (auto& param : params) {
            if (!param.typeExpr && !param.isVariadic && !param.defaultExpr) {
                std::string tpName;
                for (char c = 'A'; c <= 'Z'; c++) {
                    tpName = std::string(1, c);
                    if (usedNames.find(tpName) == usedNames.end()) break;
                }
                usedNames.insert(tpName);
                typeParams.push_back(tpName);
                param.typeExpr = std::make_unique<NamedTypeNode>(param.loc, tpName);
            }
        }
    }

    // Return type (optional - omitted means inferred)
    TypeExprPtr returnType;
    if (current_.kind != TokenKind::LBrace && current_.kind != TokenKind::Equals
        && current_.kind != TokenKind::Where) {
        returnType = parseTypeExpr();
    }

    // Where clause: fn foo<T>(x T) T where T: Comparable { ... }
    parseWhereClause(whereConstraints);

    // Expression-body function: fn name(params) type = expr;
    if (match(TokenKind::Equals)) {
        ASTList stmts;
        if (check(TokenKind::Match)) {
            // match as single-expression body
            stmts.push_back(parseMatchStmt());
        } else {
            ExprPtr bodyExpr = parseExpression();
            auto exprStmt = std::make_unique<ExprStmtNode>(bodyExpr->loc, std::move(bodyExpr));
            exprStmt->isTrailing = true;
            stmts.push_back(std::move(exprStmt));
        }
        expectTerminator();
        // Wrap in a block
        auto body = std::make_unique<BlockStmt>(start, std::move(stmts));
        auto fn = std::make_unique<FnDeclNode>(start, name.text, std::move(typeParams),
                                                std::move(params), std::move(returnType), std::move(body));
        fn->whereConstraints = std::move(whereConstraints);
        if (errors_.size() > errorsBefore) fn->hasParseError = true;
        return fn;
    }

    // Body
    ASTPtr body = parseBlock();

    auto fn = std::make_unique<FnDeclNode>(start, name.text, std::move(typeParams),
                                            std::move(params), std::move(returnType), std::move(body));
    fn->whereConstraints = std::move(whereConstraints);
    if (errors_.size() > errorsBefore) fn->hasParseError = true;
    return fn;
}

ASTPtr Parser::parseStructDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'struct'

    Token name = expect(TokenKind::Identifier, "Expected struct name after 'struct'");

    // Template type parameters: struct Name<T, U> { ... }  or  struct Name<T: Comparable> { ... }
    std::vector<std::string> typeParams;
    std::vector<WhereConstraint> whereConstraints;
    if (check(TokenKind::Less)) {
        advance(); // consume '<'
        do {
            Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
            typeParams.push_back(tp.text);
            parseInlineConstraints(tp.text, tp, whereConstraints);
        } while (match(TokenKind::Comma));
        if (!matchGreater()) {
            error("Expected '>' after type parameters");
        }
    }

    // Where clause: struct Name<T> where T: Comparable { ... }
    parseWhereClause(whereConstraints);

    // Tuple struct: struct Name(Type, Type, ...);
    if (check(TokenKind::LParen)) {
        SourceRange openLoc = currentLoc();
        advance(); // consume (
        std::vector<StructField> fields;
        int fieldIdx = 0;
        if (!check(TokenKind::RParen)) {
            do {
                StructField field;
                field.loc = currentLoc();
                field.name = "_" + std::to_string(fieldIdx++);
                field.typeExpr = parseTypeExpr();
                fields.push_back(std::move(field));
            } while (match(TokenKind::Comma));
        }
        expectClosing(TokenKind::RParen, "(", openLoc);
        expectTerminator();
        auto decl = std::make_unique<StructDeclNode>(start, name.text, std::move(typeParams), std::move(fields));
        decl->isTupleStruct = true;
        decl->whereConstraints = std::move(whereConstraints);
        return decl;
    }

    SourceRange braceLoc = currentLoc();
    expect(TokenKind::LBrace, "Expected '{' after struct name");

    std::vector<StructField> fields;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        u32 offsetBefore = current_.loc.start.offset;
        StructField field;
        field.loc = currentLoc();
        Token fieldName = expect(TokenKind::Identifier, "Expected field name");
        field.name = fieldName.text;
        // Consume erroneous ':' between field name and type (e.g. "a: Int" instead of "a Int")
        if (check(TokenKind::Colon)) {
            error("Unexpected ':' in struct field declaration (use 'name Type' not 'name: Type')");
            advance(); // skip the colon so parsing can recover
        }
        field.typeExpr = parseTypeExpr();
        fields.push_back(std::move(field));

        // Fields separated by comma or semicolon
        if (check(TokenKind::Comma)) {
            advance();
        } else if (check(TokenKind::Semicolon)) {
            advance();
        }
        if (current_.loc.start.offset == offsetBefore) { synchronize(); }
    }

    expectClosing(TokenKind::RBrace, "{", braceLoc);
    match(TokenKind::Semicolon); // optional trailing semicolon

    auto decl = std::make_unique<StructDeclNode>(start, name.text, std::move(typeParams), std::move(fields));
    decl->whereConstraints = std::move(whereConstraints);
    return decl;
}

ASTPtr Parser::parseUnionDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'enum'

    Token name = expect(TokenKind::Identifier, "Expected enum name after 'enum'");

    // Template type parameters: enum Name<T, U> { ... }  or  enum Name<T: Comparable> { ... }
    std::vector<std::string> typeParams;
    std::vector<WhereConstraint> whereConstraints;
    if (check(TokenKind::Less)) {
        advance(); // consume '<'
        do {
            Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
            typeParams.push_back(tp.text);
            parseInlineConstraints(tp.text, tp, whereConstraints);
        } while (match(TokenKind::Comma));
        if (!matchGreater()) {
            error("Expected '>' after type parameters");
        }
    }

    // Where clause: enum Name<T> where T: Numeric { ... }
    parseWhereClause(whereConstraints);

    SourceRange braceLoc = currentLoc();
    expect(TokenKind::LBrace, "Expected '{' after enum name");

    std::vector<UnionCase> cases;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        u32 offsetBefore = current_.loc.start.offset;
        UnionCase ucase;
        ucase.loc = currentLoc();
        Token caseName = expect(TokenKind::Identifier, "Expected case name");
        ucase.name = caseName.text;

        // Optional type (if next token is a type keyword or identifier, not comma/semicolon/rbrace)
        if (current_.kind != TokenKind::Comma && current_.kind != TokenKind::Semicolon &&
            current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
            ucase.typeExpr = parseTypeExpr();
            // Reject 1-element tuple types -- use the element type directly
            if (ucase.typeExpr && ucase.typeExpr->kind == TypeExpr::TupleType) {
                auto* tt = static_cast<TupleTypeNode*>(ucase.typeExpr.get());
                if (tt->elemTypes.size() == 1) {
                    error("Enum case type cannot be a 1-element tuple; use the element type directly");
                }
            }
        }

        cases.push_back(std::move(ucase));

        // Cases separated by comma or semicolon
        if (check(TokenKind::Comma)) {
            advance();
        } else if (check(TokenKind::Semicolon)) {
            advance();
        }

        // Error recovery: if no progress was made, synchronize to avoid infinite loop
        if (current_.loc.start.offset == offsetBefore) {
            synchronize();
        }
    }

    expectClosing(TokenKind::RBrace, "{", braceLoc);
    match(TokenKind::Semicolon); // optional trailing semicolon

    auto decl = std::make_unique<UnionDeclNode>(start, name.text, std::move(typeParams), std::move(cases));
    decl->whereConstraints = std::move(whereConstraints);
    return decl;
}

ASTPtr Parser::parseTypeAliasDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'type'
    Token name = expect(TokenKind::Identifier, "Expected type alias name after 'type'");

    // Optional type parameters: type Pair<T, U> = ...  or  type Pair<T: Comparable, U> = ...
    std::vector<std::string> typeParams;
    std::vector<WhereConstraint> whereConstraints;
    if (check(TokenKind::Less)) {
        advance(); // consume '<'
        do {
            Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
            typeParams.push_back(tp.text);
            parseInlineConstraints(tp.text, tp, whereConstraints);
        } while (match(TokenKind::Comma));
        if (!matchGreater()) {
            error("Expected '>' after type parameters");
        }
    }

    expect(TokenKind::Equals, "Expected '=' after type alias name");
    auto typeExpr = parseTypeExpr();
    expectTerminator();
    auto decl = std::make_unique<TypeAliasDeclNode>(
        SourceRange{start.start, previous_.loc.end},
        name.text, std::move(typeParams), std::move(typeExpr));
    decl->whereConstraints = std::move(whereConstraints);
    return decl;
}

void Parser::parseInlineConstraints(const std::string& typeParam, const Token& tp,
                                    std::vector<WhereConstraint>& whereConstraints) {
    // Inline constraints: <T: Comparable & Numeric>
    if (check(TokenKind::Colon)) {
        advance(); // consume ':'
        do {
            Token cn = expect(TokenKind::Identifier, "Expected constraint name");
            whereConstraints.push_back({typeParam, cn.text, cn.loc});
        } while (match(TokenKind::Ampersand));
    }
}

void Parser::parseWhereClause(std::vector<WhereConstraint>& whereConstraints) {
    // where T: Comparable, U: Numeric & Printable
    if (check(TokenKind::Where)) {
        advance(); // consume 'where'
        do {
            Token paramName = expect(TokenKind::Identifier, "Expected type param name in where clause");
            expect(TokenKind::Colon, "Expected ':' after type param in where clause");
            do {
                Token cn = expect(TokenKind::Identifier, "Expected constraint name");
                whereConstraints.push_back({paramName.text, cn.text, cn.loc});
            } while (match(TokenKind::Ampersand));
        } while (match(TokenKind::Comma));
    }
}

ASTPtr Parser::parseConstraintDecl() {
    SourceRange start = currentLoc();
    advance(); // consume 'constraint'

    Token name = expect(TokenKind::Identifier, "Expected constraint name after 'constraint'");

    // Optional type parameters: constraint Comparable<T> = ...
    std::vector<std::string> typeParams;
    if (check(TokenKind::Less)) {
        advance(); // consume '<'
        do {
            Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
            typeParams.push_back(tp.text);
        } while (match(TokenKind::Comma));
        if (!matchGreater()) {
            error("Expected '>' after type parameters");
        }
    }

    expect(TokenKind::Equals, "Expected '=' after constraint name");

    auto decl = std::make_unique<ConstraintDeclNode>(start, name.text, std::move(typeParams));

    // Peek to determine form
    if (check(TokenKind::Requires)) {
        // Structural form: constraint Comparable<T> = requires { <(T, T) Bool, ... }
        advance(); // consume 'requires'
        SourceRange reqBraceLoc = currentLoc();
        expect(TokenKind::LBrace, "Expected '{' after 'requires'");
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            RequiredFnSig sig;
            sig.loc = currentLoc();
            // Function name: identifier or operator token
            if (current_.kind == TokenKind::Identifier ||
                current_.kind == TokenKind::Plus || current_.kind == TokenKind::Minus ||
                current_.kind == TokenKind::Star || current_.kind == TokenKind::Slash ||
                current_.kind == TokenKind::Percent || current_.kind == TokenKind::EqEq ||
                current_.kind == TokenKind::BangEq || current_.kind == TokenKind::Less ||
                current_.kind == TokenKind::LessEq || current_.kind == TokenKind::Greater ||
                current_.kind == TokenKind::GreaterEq || current_.kind == TokenKind::Tilde ||
                current_.kind == TokenKind::Ampersand || current_.kind == TokenKind::Pipe ||
                current_.kind == TokenKind::Caret || current_.kind == TokenKind::ShiftLeft ||
                current_.kind == TokenKind::ShiftRight || current_.kind == TokenKind::UShiftRight ||
                current_.kind == TokenKind::Bang || current_.kind == TokenKind::SlashSlash ||
                current_.kind == TokenKind::Dollar) {
                sig.name = advance().text;
            } else {
                error("Expected function name or operator in requires block");
                break;
            }
            // Parameter types in parens
            SourceRange reqParenLoc = currentLoc();
            expect(TokenKind::LParen, "Expected '(' after function name in requires block");
            if (!check(TokenKind::RParen)) {
                do {
                    sig.paramTypes.push_back(parseTypeExpr());
                } while (match(TokenKind::Comma));
            }
            expectClosing(TokenKind::RParen, "(", reqParenLoc);
            // Return type
            sig.returnType = parseTypeExpr();
            decl->requiredFns.push_back(std::move(sig));
            // Separator: comma or semicolon (optional before '}')
            if (check(TokenKind::Comma)) {
                advance();
            } else if (check(TokenKind::Semicolon)) {
                advance();
            }
        }
        expectClosing(TokenKind::RBrace, "{", reqBraceLoc);
    } else {
        // Type-set or composition form
        // Parse first component
        auto firstType = parseTypeExpr();
        if (check(TokenKind::Pipe)) {
            // Type-set form: Int | Float | ...
            decl->items.push_back(std::move(firstType));
            while (match(TokenKind::Pipe)) {
                decl->items.push_back(parseTypeExpr());
            }
        } else if (check(TokenKind::Ampersand)) {
            // Composition form: Numeric & Comparable<T>
            // Convert firstType to a ConstraintComponent
            ConstraintComponent comp;
            comp.loc = firstType->loc;
            if (firstType->kind == ASTNode::NamedType) {
                comp.name = static_cast<NamedTypeNode*>(firstType.get())->name;
            } else if (firstType->kind == ASTNode::TemplateType) {
                auto* tt = static_cast<TemplateTypeNode*>(firstType.get());
                comp.name = tt->name;
                comp.typeArgs = std::move(tt->typeArgs);
            } else {
                error("Expected constraint name in composition");
            }
            decl->components.push_back(std::move(comp));

            while (match(TokenKind::Ampersand)) {
                ConstraintComponent comp2;
                comp2.loc = currentLoc();
                auto typeExpr = parseTypeExpr();
                if (typeExpr->kind == ASTNode::NamedType) {
                    comp2.name = static_cast<NamedTypeNode*>(typeExpr.get())->name;
                } else if (typeExpr->kind == ASTNode::TemplateType) {
                    auto* tt = static_cast<TemplateTypeNode*>(typeExpr.get());
                    comp2.name = tt->name;
                    comp2.typeArgs = std::move(tt->typeArgs);
                } else {
                    error("Expected constraint name in composition");
                }
                decl->components.push_back(std::move(comp2));
            }
        } else {
            // Single type-set element or single constraint reference
            decl->items.push_back(std::move(firstType));
        }
    }

    expectTerminator();
    return decl;
}

// --- Statements ---

ASTPtr Parser::parseStatement() {
    switch (current_.kind) {
        case TokenKind::LBrace:
            return parseBlock();
        case TokenKind::If:
            return parseIfStmt();
        case TokenKind::While:
            return parseWhileStmt();
        case TokenKind::For:
            return parseForStmt();
        case TokenKind::Match:
            return parseMatchStmt();
        case TokenKind::Return:
            return parseReturnStmt();
        case TokenKind::Break:
            return parseBreakStmt();
        case TokenKind::Continue:
            return parseContinueStmt();
        default:
            return parseExprStmtOrAssign();
    }
}

ASTPtr Parser::parseBlock() {
    SourceRange start = currentLoc();
    SourceRange openLoc = currentLoc();
    expect(TokenKind::LBrace, "Expected '{'");

    ASTList stmts;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        u32 offsetBefore = current_.loc.start.offset;
        if (check(TokenKind::Import)) {
            error("Import declarations are only allowed at the top level");
            auto node = parseDeclaration(); // parse to avoid cascading errors
        } else {
            auto stmt = parseDeclaration();
            if (stmt) {
                stmts.push_back(std::move(stmt));
            }
        }
        if (current_.loc.start.offset == offsetBefore) { synchronize(); }
    }

    expectClosing(TokenKind::RBrace, "{", openLoc);
    return std::make_unique<BlockStmt>(start, std::move(stmts));
}

ASTPtr Parser::parseIfStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'if'

    SourceRange openLoc = currentLoc();
    expect(TokenKind::LParen, "Expected '(' after 'if'");
    ExprPtr cond = parseExpression();
    expectClosing(TokenKind::RParen, "(", openLoc);

    ASTPtr thenBranch = parseBlock();

    ASTPtr elseBranch;
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            elseBranch = parseIfStmt();
        } else {
            elseBranch = parseBlock();
        }
    }

    return std::make_unique<IfStmtNode>(start, std::move(cond),
                                         std::move(thenBranch), std::move(elseBranch));
}

ASTPtr Parser::parseWhileStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'while'

    SourceRange openLoc = currentLoc();
    expect(TokenKind::LParen, "Expected '(' after 'while'");
    ExprPtr cond = parseExpression();
    expectClosing(TokenKind::RParen, "(", openLoc);

    ASTPtr body = parseBlock();

    return std::make_unique<WhileStmtNode>(start, std::move(cond), std::move(body));
}

ASTPtr Parser::parseForStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'for'

    SourceRange openLoc = currentLoc();
    expect(TokenKind::LParen, "Expected '(' after 'for'");
    Token varName = expect(TokenKind::Identifier, "Expected variable name in for loop");
    expect(TokenKind::Colon, "Expected ':' after variable name in for loop");
    ExprPtr iterable = parseExpression();
    expectClosing(TokenKind::RParen, "(", openLoc);

    ASTPtr body = parseBlock();

    return std::make_unique<ForStmtNode>(start, varName.text,
                                          std::move(iterable), std::move(body));
}

ASTPtr Parser::parseReturnStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'return'

    ExprPtr value;
    // Check if there's an expression following return
    if (current_.kind != TokenKind::Semicolon &&
        current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        value = parseExpression();
    }

    expectTerminator();
    return std::make_unique<ReturnStmtNode>(start, std::move(value));
}

ASTPtr Parser::parseBreakStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'break'
    expectTerminator();
    return std::make_unique<BreakStmtNode>(start);
}

ASTPtr Parser::parseContinueStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'continue'
    expectTerminator();
    return std::make_unique<ContinueStmtNode>(start);
}

ASTPtr Parser::parseExprStmtOrAssign() {
    SourceRange start = currentLoc();

    ExprPtr expr = parseExpression();

    // Check for assignment: identifier = expr  or  `dynVar = expr  or  expr[index] = value
    if (match(TokenKind::Equals)) {
        if (expr->kind == ASTNode::Identifier) {
            auto* ident = static_cast<IdentifierExpr*>(expr.get());
            std::string name = ident->name;
            ExprPtr value = parseExpression();
            expectTerminator();
            return std::make_unique<AssignStmtNode>(start, std::move(name), std::move(value));
        } else if (expr->kind == ASTNode::DynamicVar) {
            auto* dynVar = static_cast<DynamicVarExpr*>(expr.get());
            std::string name = dynVar->name;
            ExprPtr value = parseExpression();
            expectTerminator();
            auto stmt = std::make_unique<AssignStmtNode>(start, std::move(name), std::move(value));
            stmt->isDynamic = true;
            return stmt;
        } else if (expr->kind == ASTNode::IndexExpr) {
            auto* idx = static_cast<IndexExpr_*>(expr.get());
            ExprPtr value = parseExpression();
            expectTerminator();
            return std::make_unique<IndexAssignStmtNode>(
                start, std::move(idx->object), std::move(idx->index), std::move(value));
        } else {
            error("Left side of assignment must be a variable or indexed container");
        }
    }

    auto node = std::make_unique<ExprStmtNode>(start, std::move(expr));

    if (check(TokenKind::Semicolon)) {
        advance();
        // Has semicolon — not a trailing expression
    } else if (check(TokenKind::RBrace) || check(TokenKind::Eof)) {
        // No semicolon, followed by } or EOF — this is a trailing expression
        node->isTrailing = true;
    }

    return node;
}

// --- Expressions (Pratt parsing) ---

int Parser::getPrecedence(TokenKind kind) const {
    switch (kind) {
        case TokenKind::LeftArrow:   return 0;   // <- (right-associative, lowest)
        case TokenKind::Arrow:       return 0;   // -> (right-associative, lowest)
        case TokenKind::PipeGreater: return 1;   // |>
        case TokenKind::PipePipe:    return 2;   // ||
        case TokenKind::AmpAmp:      return 3;   // &&
        case TokenKind::ColonColon:  return 4;   // :: (right-associative)
        case TokenKind::EqEq:
        case TokenKind::BangEq:      return 5;   // == !=
        case TokenKind::Less:
        case TokenKind::LessEq:
        case TokenKind::Greater:
        case TokenKind::GreaterEq:   return 6;   // < <= > >=
        case TokenKind::Pipe:        return 7;   // |  (bitwise or)
        case TokenKind::Caret:       return 8;   // ^  (bitwise xor)
        case TokenKind::Ampersand:   return 9;   // &  (bitwise and)
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
        case TokenKind::UShiftRight: return 10;  // << >> >>>
        case TokenKind::Dollar:      return 11;  // $
        case TokenKind::Plus:
        case TokenKind::Minus:       return 11;  // + -
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::SlashSlash:  return 12;  // * / % //
        default:                     return -1;  // Not a binary operator
    }
}

// Operator-as-function sugar in call-argument position: an operator token
// immediately followed by ',' or ')' is the operator as a function value,
// desugared to the equivalent untyped lambda -- fold(0, +) is
// fold(0, fn(a, b) { a + b }) -- which then rides the deferred-lambda
// backward inference exactly like a handwritten template lambda. The
// two-token lookahead keeps ordinary prefix expressions like f(-x)
// unambiguous. Unary-only operators (!, ~) desugar to fn(a) { op a };
// dual-arity operators (+, -) desugar to their binary form here (arity is
// inferred from sibling arguments, which cannot see the callee's signature).
// Pipeline / assignment arrows are excluded -- they are not functions.
ExprPtr Parser::parseCallArgument() {
    TokenKind k = current_.kind;
    TokenKind next = lexer_.peek().kind;
    if (next == TokenKind::Comma || next == TokenKind::RParen) {
        SourceRange loc = currentLoc();
        auto makeParam = [](const char* name) {
            LambdaExprNode::Param p;
            p.name = name;
            return p;
        };
        auto makeBody = [&](ExprPtr e) {
            ASTList stmts;
            auto stmt = std::make_unique<ExprStmtNode>(loc, std::move(e));
            stmt->isTrailing = true;
            stmts.push_back(std::move(stmt));
            return std::make_unique<BlockStmt>(loc, std::move(stmts));
        };
        if (k == TokenKind::Bang || k == TokenKind::Tilde) {
            advance();
            UnaryOpExpr::Op op = (k == TokenKind::Bang) ? UnaryOpExpr::Not
                                                        : UnaryOpExpr::BitNot;
            auto body = std::make_unique<UnaryOpExpr>(loc, op,
                std::make_unique<IdentifierExpr>(loc, "a"));
            std::vector<LambdaExprNode::Param> params;
            params.push_back(makeParam("a"));
            return std::make_unique<LambdaExprNode>(loc, std::move(params),
                nullptr, makeBody(std::move(body)));
        }
        if (getPrecedence(k) > 0 &&
            k != TokenKind::PipeGreater && k != TokenKind::LeftArrow &&
            k != TokenKind::Arrow) {
            advance();
            auto body = std::make_unique<BinaryOpExpr>(loc, getBinaryOp(k),
                std::make_unique<IdentifierExpr>(loc, "a"),
                std::make_unique<IdentifierExpr>(loc, "b"));
            std::vector<LambdaExprNode::Param> params;
            params.push_back(makeParam("a"));
            params.push_back(makeParam("b"));
            return std::make_unique<LambdaExprNode>(loc, std::move(params),
                nullptr, makeBody(std::move(body)));
        }
    }
    return parseExpression();
}

BinaryOpExpr::Op Parser::getBinaryOp(TokenKind kind) const {
    switch (kind) {
        case TokenKind::Plus:        return BinaryOpExpr::Add;
        case TokenKind::Minus:       return BinaryOpExpr::Sub;
        case TokenKind::Star:        return BinaryOpExpr::Mul;
        case TokenKind::Slash:       return BinaryOpExpr::Div;
        case TokenKind::Percent:     return BinaryOpExpr::Mod;
        case TokenKind::SlashSlash:  return BinaryOpExpr::IntDiv;
        case TokenKind::Ampersand:   return BinaryOpExpr::BitAnd;
        case TokenKind::Pipe:        return BinaryOpExpr::BitOr;
        case TokenKind::Caret:       return BinaryOpExpr::BitXor;
        case TokenKind::ShiftLeft:   return BinaryOpExpr::ShiftL;
        case TokenKind::ShiftRight:  return BinaryOpExpr::ShiftR;
        case TokenKind::UShiftRight: return BinaryOpExpr::UShiftR;
        case TokenKind::AmpAmp:      return BinaryOpExpr::And;
        case TokenKind::PipePipe:    return BinaryOpExpr::Or;
        case TokenKind::EqEq:        return BinaryOpExpr::Eq;
        case TokenKind::BangEq:      return BinaryOpExpr::Ne;
        case TokenKind::Less:        return BinaryOpExpr::Lt;
        case TokenKind::LessEq:      return BinaryOpExpr::Le;
        case TokenKind::Greater:     return BinaryOpExpr::Gt;
        case TokenKind::GreaterEq:   return BinaryOpExpr::Ge;
        case TokenKind::PipeGreater: return BinaryOpExpr::Pipeline;
        case TokenKind::Dollar:      return BinaryOpExpr::Concat;
        case TokenKind::ColonColon:  return BinaryOpExpr::Cons;
        case TokenKind::LeftArrow:   return BinaryOpExpr::LeftArrow;
        case TokenKind::Arrow:       return BinaryOpExpr::RightArrow;
        default:                     return BinaryOpExpr::Add; // unreachable
    }
}

ExprPtr Parser::parseExpression(int minPrec) {
    ExprPtr left = parsePrimary();
    left = parsePostfix(std::move(left));

    while (true) {
        // Check for ternary operator: expr ? then : else
        if (current_.kind == TokenKind::Question && minPrec <= 0) {
            SourceRange loc = currentLoc();
            advance(); // consume ?
            ASTPtr thenExpr = std::make_unique<ExprStmtNode>(currentLoc(),
                parseExpression());
            // Wrap thenExpr in a block
            ASTList thenStmts;
            static_cast<ExprStmtNode*>(thenExpr.get())->isTrailing = true;
            thenStmts.push_back(std::move(thenExpr));
            auto thenBlock = std::make_unique<BlockStmt>(loc, std::move(thenStmts));

            expect(TokenKind::Colon, "Expected ':' in ternary expression");

            ASTPtr elseExpr = std::make_unique<ExprStmtNode>(currentLoc(),
                parseExpression());
            ASTList elseStmts;
            static_cast<ExprStmtNode*>(elseExpr.get())->isTrailing = true;
            elseStmts.push_back(std::move(elseExpr));
            auto elseBlock = std::make_unique<BlockStmt>(loc, std::move(elseStmts));

            left = std::make_unique<IfExprNode>(loc, std::move(left),
                std::move(thenBlock), std::move(elseBlock));
            continue;
        }

        int prec = getPrecedence(current_.kind);
        if (prec < minPrec) break;

        // Pipeline operator: desugar `x |> f` to `f(x)` and `x |> f(y)` to `f(x, y)`
        if (current_.kind == TokenKind::PipeGreater) {
            advance(); // consume |>

            // Handle |> @ ... (pipe into auto-mapped operation)
            // Wrap the pipe input in AutoMapExpr and continue via postfix chain.
            if (check(TokenKind::At)) {
                SourceRange loc = currentLoc();
                Token atTok = advance(); // consume @, @@, @1, etc.

                int depth = 0;
                int cartesianIndex = 0;
                const std::string& text = atTok.text;

                if (text.size() >= 2 && text[1] >= '1' && text[1] <= '9') {
                    depth = 1;
                    cartesianIndex = text[1] - '0';
                } else {
                    depth = (int)text.size();
                }

                left = std::make_unique<AutoMapExpr>(loc, std::move(left), depth, cartesianIndex);
                left = parsePostfix(std::move(left));
                continue;
            }

            ExprPtr right = parsePrimary();

            // Only parse explicit parenthesized args after |>, not the full
            // postfix chain.  Space-pipeline calls like `println` that follow
            // should bind to the *result* of the pipe, not to the function.
            ExprList args;
            args.push_back(std::move(left));
            if (check(TokenKind::LParen)) {
                SourceRange pipeOpenLoc = currentLoc();
                advance(); // consume (
                if (!check(TokenKind::RParen)) {
                    do {
                        args.push_back(parseCallArgument());
                    } while (match(TokenKind::Comma));
                }
                expectClosing(TokenKind::RParen, "(", pipeOpenLoc);
            }
            SourceRange loc = right->loc;
            left = std::make_unique<CallExpr_>(loc, std::move(right), std::move(args));

            // Now let the outer loop handle further postfix (space pipeline, etc.)
            left = parsePostfix(std::move(left));
            continue;
        }

        TokenKind opKind = current_.kind;
        BinaryOpExpr::Op op = getBinaryOp(opKind);
        SourceRange opLoc = currentLoc();
        advance(); // consume operator

        // Right-associative for :: (cons) and <- (ref set): use prec instead of prec + 1
        int nextPrec = (opKind == TokenKind::ColonColon || opKind == TokenKind::LeftArrow || opKind == TokenKind::Arrow)
                        ? prec : prec + 1;
        ExprPtr right = parseExpression(nextPrec);
        left = std::make_unique<BinaryOpExpr>(opLoc, op, std::move(left), std::move(right));
    }

    return left;
}

ExprPtr Parser::parseBracketLiteral(SourceRange loc, bool isImmutable) {
    // The opening '[' has already been consumed by the caller.

    // Typed array constructor [Type](...) / #[Type](...): the element type sits
    // in the bracket slot and the elements follow in parens. Works for both the
    // mutable ([...]) and persistent (#[...]) forms; the trailing `](` after the
    // type disambiguates it from an ordinary literal.
    {
        // Detect typed array constructor.
        // Never commit on the leading token alone -- only the trailing `](`
        // proves the constructor form. Even type keywords are ambiguous:
        // Complex and Fraction are also constructor functions, so
        // [Complex(1.0, 2.0), ...] must parse as an ordinary array literal
        // while [Complex](...) is the typed constructor. For type keywords,
        // '[', '(', 'fn', capitalized Identifiers, and the `some C`
        // existential introducer, use tentative parsing.
        bool tryTypedConstructor = false;
        bool isSomeExistential = current_.kind == TokenKind::Identifier &&
            current_.text == "some" && lexer_.peek().kind == TokenKind::Identifier;
        if (isTypeKeyword(current_.kind) ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::LParen ||
            current_.kind == TokenKind::Fn ||
            isSomeExistential ||
            (current_.kind == TokenKind::Identifier && !current_.text.empty() && std::isupper(current_.text[0]))) {
            // Tentative parse: save state, try type + ]( , restore if it fails
            Token savedCurrent = current_;
            Token savedPrevious = previous_;
            auto lexerState = lexer_.save();
            size_t savedErrors = errors_.size();

            parseTypeExpr();
            if (check(TokenKind::RBracket) && lexer_.peek().kind == TokenKind::LParen) {
                tryTypedConstructor = true;
            }
            // Restore state regardless — we'll re-parse cleanly below
            current_ = savedCurrent;
            previous_ = savedPrevious;
            lexer_.restore(lexerState);
            while (errors_.size() > savedErrors) errors_.pop_back();
        }

        if (tryTypedConstructor) {
            auto typeExpr = parseTypeExpr();
            expectClosing(TokenKind::RBracket, "[", loc);
            SourceRange typedParenLoc = currentLoc();
            expect(TokenKind::LParen, "Expected '(' after typed array constructor [Type]");
            ExprList elements;
            if (!check(TokenKind::RParen)) {
                do {
                    elements.push_back(parseExpression());
                } while (match(TokenKind::Comma));
            }
            expectClosing(TokenKind::RParen, "(", typedParenLoc);
            auto arr = std::make_unique<ArrayLiteralExpr>(loc, std::move(elements));
            arr->elemTypeExpr = std::move(typeExpr);
            arr->isImmutable = isImmutable;
            return arr;
        }
    }

    // Empty map: [:] / #[:]
    if (check(TokenKind::Colon)) {
        advance(); // consume :
        expectClosing(TokenKind::RBracket, "[", loc);
        std::vector<MapLiteralExpr::Entry> entries;
        auto m = std::make_unique<MapLiteralExpr>(loc, std::move(entries));
        m->isImmutable = isImmutable;
        return m;
    }

    // Empty array: [] / #[]
    if (check(TokenKind::RBracket)) {
        advance(); // consume ]
        ExprList elements;
        auto arr = std::make_unique<ArrayLiteralExpr>(loc, std::move(elements));
        arr->isImmutable = isImmutable;
        return arr;
    }

    // Parse first expression, then check if map or array
    ExprPtr first = parseExpression();

    if (check(TokenKind::Colon)) {
        // Map literal: [key: value, ...]
        advance(); // consume :
        ExprPtr firstValue = parseExpression();
        std::vector<MapLiteralExpr::Entry> entries;
        entries.push_back({std::move(first), std::move(firstValue)});
        while (match(TokenKind::Comma)) {
            if (check(TokenKind::RBracket)) break; // trailing comma
            ExprPtr key = parseExpression();
            expect(TokenKind::Colon, "Expected ':' in map literal entry");
            ExprPtr value = parseExpression();
            entries.push_back({std::move(key), std::move(value)});
        }
        expectClosing(TokenKind::RBracket, "[", loc);
        auto m = std::make_unique<MapLiteralExpr>(loc, std::move(entries));
        m->isImmutable = isImmutable;
        return m;
    }

    // Array literal: [expr, expr, ...]
    ExprList elements;
    elements.push_back(std::move(first));
    while (match(TokenKind::Comma)) {
        if (check(TokenKind::RBracket)) break; // trailing comma
        elements.push_back(parseExpression());
    }
    expectClosing(TokenKind::RBracket, "[", loc);
    auto arr = std::make_unique<ArrayLiteralExpr>(loc, std::move(elements));
    arr->isImmutable = isImmutable;
    return arr;
}

ExprPtr Parser::parsePrimary() {
    switch (current_.kind) {
        case TokenKind::IntLiteral: {
            Token tok = advance();
            return std::make_unique<IntLiteralExpr>(tok.loc, tok.intValue);
        }
        case TokenKind::FloatLiteral: {
            Token tok = advance();
            return std::make_unique<FloatLiteralExpr>(tok.loc, tok.floatValue);
        }
        case TokenKind::ImaginaryLiteral: {
            Token tok = advance();
            return std::make_unique<ImaginaryLiteralExpr>(tok.loc, tok.floatValue);
        }
        case TokenKind::FractionLiteral: {
            Token tok = advance();
            return std::make_unique<FractionLiteralExpr>(tok.loc, tok.intValue, tok.denominator);
        }
        case TokenKind::StringLiteral: {
            Token tok = advance();
            return std::make_unique<StringLiteralExpr>(tok.loc, tok.text);
        }
        case TokenKind::True: {
            Token tok = advance();
            return std::make_unique<BoolLiteralExpr>(tok.loc, true);
        }
        case TokenKind::False: {
            Token tok = advance();
            return std::make_unique<BoolLiteralExpr>(tok.loc, false);
        }
        case TokenKind::SymbolLiteral: {
            Token tok = advance();
            return std::make_unique<SymbolLiteralExpr>(tok.loc, tok.text);
        }
        case TokenKind::Nil: {
            Token tok = advance();
            return std::make_unique<NilLiteralExpr>(tok.loc);
        }
        case TokenKind::DynamicVar: {
            Token tok = advance();
            return std::make_unique<DynamicVarExpr>(tok.loc, tok.text);
        }
        case TokenKind::Identifier:
        case TokenKind::KwFraction:
        case TokenKind::KwComplex: {
            Token tok = advance();
            // Check for List(...) constructor
            if (tok.kind == TokenKind::Identifier && tok.text == "List" && check(TokenKind::LParen)) {
                SourceRange loc = tok.loc;
                SourceRange openLoc = currentLoc();
                advance(); // consume (
                ExprList elements;
                if (!check(TokenKind::RParen)) {
                    do {
                        elements.push_back(parseExpression());
                    } while (match(TokenKind::Comma));
                }
                expectClosing(TokenKind::RParen, "(", openLoc);
                return std::make_unique<ListLiteralExpr>(loc, std::move(elements));
            }
            // Check for Set(...) constructor
            if (tok.kind == TokenKind::Identifier && tok.text == "Set" && check(TokenKind::LParen)) {
                SourceRange loc = tok.loc;
                SourceRange openLoc = currentLoc();
                advance(); // consume (
                ExprList elements;
                if (!check(TokenKind::RParen)) {
                    do {
                        elements.push_back(parseExpression());
                    } while (match(TokenKind::Comma));
                }
                expectClosing(TokenKind::RParen, "(", openLoc);
                return std::make_unique<SetLiteralExpr>(loc, std::move(elements));
            }
            // Check for Name<Types>{...} (template struct literal) or Name<Types>.Case (template enum)
            if (tok.kind == TokenKind::Identifier && check(TokenKind::Less)) {
                std::vector<TypeExprPtr> typeArgs;
                if (tryParseTypeArgs(typeArgs)) {
                    if (check(TokenKind::LBrace)) {
                        // Template struct literal: Name<Types> { ... }
                        auto result = parseStructLiteral(tok);
                        auto* lit = static_cast<StructLiteralExpr*>(result.get());
                        lit->typeArgs = std::move(typeArgs);
                        return result;
                    }
                    if (check(TokenKind::Dot)) {
                        // Template enum construction: Name<Types>.Case or Name<Types>.Case(arg)
                        advance(); // consume .
                        Token caseTok = expect(TokenKind::Identifier, "Expected case name after '.'");
                        // Return as identifier for now; postfix will handle call or field access
                        // We need to create an EnumConstructExpr directly
                        ExprPtr arg;
                        if (check(TokenKind::LParen)) {
                            SourceRange ecOpenLoc = currentLoc();
                            advance(); // consume (
                            if (!check(TokenKind::RParen)) {
                                SourceRange tupLoc = currentLoc();
                                arg = parseExpression();
                                // If comma follows, collect multi-arg into a TupleLiteralExpr
                                if (check(TokenKind::Comma)) {
                                    ExprList elems;
                                    elems.push_back(std::move(arg));
                                    while (check(TokenKind::Comma)) {
                                        advance(); // consume comma
                                        if (check(TokenKind::RParen)) break; // trailing comma
                                        elems.push_back(parseExpression());
                                    }
                                    arg = std::make_unique<TupleLiteralExpr>(tupLoc, std::move(elems));
                                }
                            }
                            expectClosing(TokenKind::RParen, "(", ecOpenLoc);
                        }
                        auto ec = std::make_unique<EnumConstructExpr>(
                            tok.loc, tok.text, caseTok.text, std::move(arg));
                        ec->typeArgs = std::move(typeArgs);
                        return ec;
                    }
                    if (check(TokenKind::LParen)) {
                        // Explicit call-site type args: Name<Types>(args)
                        SourceRange openLoc = currentLoc();
                        advance(); // consume (
                        ExprList callArgs;
                        if (!check(TokenKind::RParen)) {
                            do {
                                callArgs.push_back(parseCallArgument());
                            } while (match(TokenKind::Comma));
                        }
                        expectClosing(TokenKind::RParen, "(", openLoc);
                        auto callee = std::make_unique<IdentifierExpr>(tok.loc, tok.text);
                        auto call = std::make_unique<CallExpr_>(
                            tok.loc, std::move(callee), std::move(callArgs));
                        call->typeArgs = std::move(typeArgs);
                        return call;
                    }
                    // Parsed type args but not followed by {, . or ( — this
                    // shouldn't happen in well-formed code, but treat as
                    // struct literal attempt
                    error("Expected '{', '.' or '(' after type arguments");
                    return std::make_unique<IdentifierExpr>(tok.loc, tok.text);
                }
                // tryParseTypeArgs failed — fall through to normal handling
            }
            // Check for struct literal: Name { field: expr, ... }
            // Identifier followed by { is always a struct literal (not valid otherwise)
            if (tok.kind == TokenKind::Identifier && check(TokenKind::LBrace)) {
                return parseStructLiteral(tok);
            }
            return std::make_unique<IdentifierExpr>(tok.loc, tok.text);
        }
        case TokenKind::LParen: {
            SourceRange loc = currentLoc();
            advance(); // consume (

            // Empty tuple: ()
            if (check(TokenKind::RParen)) {
                advance(); // consume )
                ExprList empty;
                return std::make_unique<TupleLiteralExpr>(loc, std::move(empty));
            }

            ExprPtr first = parseExpression();

            // Check for range syntax: (start..end), (start..), (start,next..end), (start,next..)
            if (check(TokenKind::DotDot)) {
                advance(); // consume ..
                ExprPtr endExpr;
                bool isInfinite = true;
                if (!check(TokenKind::RParen)) {
                    endExpr = parseExpression();
                    isInfinite = false;
                }
                expectClosing(TokenKind::RParen, "(", loc);
                return std::make_unique<RangeExprNode>(loc, std::move(first), nullptr,
                                                        std::move(endExpr), isInfinite);
            }

            if (check(TokenKind::Comma)) {
                advance(); // consume ,

                // 1-tuple: (expr,)
                if (check(TokenKind::RParen)) {
                    advance(); // consume )
                    ExprList elements;
                    elements.push_back(std::move(first));
                    return std::make_unique<TupleLiteralExpr>(loc, std::move(elements));
                }

                ExprPtr second = parseExpression();

                // Check for range with step: (start,next..end) or (start,next..)
                if (check(TokenKind::DotDot)) {
                    advance(); // consume ..
                    ExprPtr endExpr;
                    bool isInfinite = true;
                    if (!check(TokenKind::RParen)) {
                        endExpr = parseExpression();
                        isInfinite = false;
                    }
                    expectClosing(TokenKind::RParen, "(", loc);
                    return std::make_unique<RangeExprNode>(loc, std::move(first), std::move(second),
                                                            std::move(endExpr), isInfinite);
                }

                // Tuple literal: (expr, expr, ...)
                ExprList elements;
                elements.push_back(std::move(first));
                elements.push_back(std::move(second));
                while (match(TokenKind::Comma)) {
                    if (check(TokenKind::RParen)) break; // trailing comma
                    elements.push_back(parseExpression());
                }
                expectClosing(TokenKind::RParen, "(", loc);
                return std::make_unique<TupleLiteralExpr>(loc, std::move(elements));
            }

            expectClosing(TokenKind::RParen, "(", loc);
            return first;
        }
        case TokenKind::Minus: {
            SourceRange loc = currentLoc();
            advance(); // consume -
            ExprPtr operand = parseTightPostfix(parsePrimary());
            return std::make_unique<UnaryOpExpr>(loc, UnaryOpExpr::Neg, std::move(operand));
        }
        case TokenKind::Bang: {
            SourceRange loc = currentLoc();
            advance(); // consume !
            ExprPtr operand = parseTightPostfix(parsePrimary());
            return std::make_unique<UnaryOpExpr>(loc, UnaryOpExpr::Not, std::move(operand));
        }
        case TokenKind::Tilde: {
            SourceRange loc = currentLoc();
            advance(); // consume ~
            ExprPtr operand = parseTightPostfix(parsePrimary());
            return std::make_unique<UnaryOpExpr>(loc, UnaryOpExpr::BitNot, std::move(operand));
        }
        case TokenKind::Ampersand: {
            SourceRange loc = currentLoc();
            advance(); // consume &
            ExprPtr operand = parseTightPostfix(parsePrimary());
            return std::make_unique<UnaryOpExpr>(loc, UnaryOpExpr::Ref, std::move(operand));
        }
        case TokenKind::Star: {
            SourceRange loc = currentLoc();
            advance(); // consume *
            ExprPtr operand = parseTightPostfix(parsePrimary());
            return std::make_unique<UnaryOpExpr>(loc, UnaryOpExpr::Deref, std::move(operand));
        }
        case TokenKind::Hash: {
            // Immutable persistent collection literal: #[...] / #[:]
            SourceRange loc = currentLoc();
            advance(); // consume #
            expect(TokenKind::LBracket, "Expected '[' after '#' for a persistent collection literal");
            return parseBracketLiteral(loc, /*isImmutable=*/true);
        }
        case TokenKind::LBracket: {
            // Array or Map literal, or typed array constructor [Type](...)
            SourceRange loc = currentLoc();
            advance(); // consume [
            return parseBracketLiteral(loc, /*isImmutable=*/false);
        }
        case TokenKind::Coro:
        case TokenKind::Async:
        case TokenKind::Fn: {
            // Lambda expression: fn(params) retType { body }
            // or coroutine lambda: coro fn(params) retType { body }
            // or async lambda: async fn(params) retType { body }
            // or template lambda: fn<T, U: Constraint>(params) retType { body }
            bool isCoro = false;
            bool isAsync = false;
            if (current_.kind == TokenKind::Coro) {
                isCoro = true;
                advance(); // consume 'coro'
                if (!check(TokenKind::Fn)) { error("Expected 'fn' after 'coro'"); return nullptr; }
            } else if (current_.kind == TokenKind::Async) {
                isAsync = true;
                advance(); // consume 'async'
                if (!check(TokenKind::Fn)) { error("Expected 'fn' after 'async'"); return nullptr; }
            }
            SourceRange loc = currentLoc();
            advance(); // consume 'fn'

            // Optional type parameters: fn<T, U: Constraint>(...)
            std::vector<std::string> typeParams;
            std::vector<WhereConstraint> whereConstraints;
            if (check(TokenKind::Less)) {
                advance(); // consume '<'
                do {
                    Token tp = expect(TokenKind::Identifier, "Expected type parameter name");
                    typeParams.push_back(tp.text);
                    parseInlineConstraints(tp.text, tp, whereConstraints);
                } while (match(TokenKind::Comma));
                if (!matchGreater()) {
                    error("Expected '>' after type parameters");
                }
            }

            SourceRange lambdaParenLoc = currentLoc();
            expect(TokenKind::LParen, "Expected '(' after 'fn' in lambda");
            std::vector<LambdaExprNode::Param> params;
            if (!check(TokenKind::RParen)) {
                do {
                    LambdaExprNode::Param param;
                    Token paramName = expect(TokenKind::Identifier, "Expected parameter name");
                    param.name = paramName.text;
                    // Type is optional -- if omitted, inferred from call context
                    if (!check(TokenKind::Comma) && !check(TokenKind::RParen)) {
                        param.typeExpr = parseTypeExpr();
                    }
                    params.push_back(std::move(param));
                } while (match(TokenKind::Comma));
            }
            expectClosing(TokenKind::RParen, "(", lambdaParenLoc);

            // Generate synthetic type parameters for untyped parameters.
            // fn(a, b) { ... } desugars to fn<A,B>(a A, b B) { ... }
            {
                std::set<std::string> usedNames(typeParams.begin(), typeParams.end());
                for (auto& param : params) {
                    if (!param.typeExpr && !param.resolvedType) {
                        std::string tpName;
                        for (char c = 'A'; c <= 'Z'; c++) {
                            tpName = std::string(1, c);
                            if (usedNames.find(tpName) == usedNames.end()) break;
                        }
                        usedNames.insert(tpName);
                        typeParams.push_back(tpName);
                        param.typeExpr = std::make_unique<NamedTypeNode>(loc, tpName);
                    }
                }
            }

            // Optional return type (omitted = inferred)
            TypeExprPtr returnType;
            if (current_.kind != TokenKind::LBrace && current_.kind != TokenKind::Equals) {
                returnType = parseTypeExpr();
            }

            // Body: either { block } or = expr
            ASTPtr body;
            if (match(TokenKind::Equals)) {
                ASTList stmts;
                if (check(TokenKind::Match)) {
                    stmts.push_back(parseMatchStmt());
                } else {
                    ExprPtr bodyExpr = parseExpression();
                    auto exprStmt = std::make_unique<ExprStmtNode>(bodyExpr->loc, std::move(bodyExpr));
                    exprStmt->isTrailing = true;
                    stmts.push_back(std::move(exprStmt));
                }
                body = std::make_unique<BlockStmt>(loc, std::move(stmts));
            } else {
                body = parseBlock();
            }

            auto lambda = std::make_unique<LambdaExprNode>(loc, std::move(params),
                                                            std::move(returnType), std::move(body));
            lambda->typeParams = std::move(typeParams);
            lambda->whereConstraints = std::move(whereConstraints);
            lambda->isCoroutine = isCoro;
            lambda->isAsync = isAsync;
            return lambda;
        }
        case TokenKind::LBrace: {
            // Block expression: { stmts; trailing_expr }
            SourceRange loc = currentLoc();
            ASTPtr body = parseBlock();
            return std::make_unique<BlockExprNode>(loc, std::move(body));
        }
        case TokenKind::If: {
            // if expression: if (cond) { ... } else { ... }
            SourceRange loc = currentLoc();
            advance(); // consume 'if'
            SourceRange ifParenLoc = currentLoc();
            expect(TokenKind::LParen, "Expected '(' after 'if'");
            ExprPtr cond = parseExpression();
            expectClosing(TokenKind::RParen, "(", ifParenLoc);
            ASTPtr thenBranch = parseBlock();
            ASTPtr elseBranch;
            if (match(TokenKind::Else)) {
                if (check(TokenKind::If)) {
                    // else if — wrap in a block containing the nested if expr
                    ExprPtr nestedIf = parsePrimary(); // recurse for if expr
                    nestedIf = parsePostfix(std::move(nestedIf));
                    ASTList stmts;
                    auto exprStmt = std::make_unique<ExprStmtNode>(nestedIf->loc, std::move(nestedIf));
                    exprStmt->isTrailing = true;
                    stmts.push_back(std::move(exprStmt));
                    elseBranch = std::make_unique<BlockStmt>(loc, std::move(stmts));
                } else {
                    elseBranch = parseBlock();
                }
            }
            return std::make_unique<IfExprNode>(loc, std::move(cond),
                                                 std::move(thenBranch), std::move(elseBranch));
        }
        case TokenKind::Match: {
            // match as an expression: `let x = match (v) { ... }`, `f(match ...)`.
            // parseMatchStmt builds a SwitchStmtNode; a trailing SwitchStmt in a
            // block yields a value (genBlockForValue -> genSwitchStmtForValue),
            // exactly as for an expression-body `fn f() = match ...`. Wrap it in
            // a BlockExprNode so it can appear anywhere an expression is allowed.
            SourceRange loc = currentLoc();
            ASTPtr matchStmt = parseMatchStmt();
            ASTList stmts;
            stmts.push_back(std::move(matchStmt));
            auto body = std::make_unique<BlockStmt>(loc, std::move(stmts));
            return std::make_unique<BlockExprNode>(loc, std::move(body));
        }
        case TokenKind::Yield: {
            // Prefix yield: yield value  ->  yield(value) as a CallExpr_
            SourceRange loc = currentLoc();
            advance(); // consume 'yield'
            ExprPtr callee = std::make_unique<IdentifierExpr>(loc, "yield");
            ExprList args;
            if (current_.kind != TokenKind::Semicolon &&
                current_.kind != TokenKind::RBrace &&
                current_.kind != TokenKind::Eof) {
                args.push_back(parseExpression());
            }
            return std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
        }
        case TokenKind::Await: {
            // Prefix await: await expr  ->  await(expr) as a CallExpr_.
            // Binds tightly (like unary -/!): `await foo()` = await(foo()),
            // `await a + b` = (await a) + b.
            SourceRange loc = currentLoc();
            advance(); // consume 'await'
            ExprPtr operand = parseTightPostfix(parsePrimary());
            ExprPtr callee = std::make_unique<IdentifierExpr>(loc, "await");
            ExprList args;
            args.push_back(std::move(operand));
            return std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
        }
        default: {
            error("Expected expression, got '" + current_.text + "'");
            Token tok = advance();
            return std::make_unique<IntLiteralExpr>(tok.loc, 0);  // Error recovery
        }
    }
}

// Tight postfix: only call (), index [], field ., and @ — NOT space-pipeline.
// Used as the operand of unary operators so that -f(x) parses as -(f(x)).
ExprPtr Parser::parseTightPostfix(ExprPtr left) {
    while (true) {
        if (check(TokenKind::LParen)) {
            // Function call: f(args)
            SourceRange openLoc = currentLoc();
            advance(); // consume (
            ExprList args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseCallArgument());
                } while (match(TokenKind::Comma));
            }
            expectClosing(TokenKind::RParen, "(", openLoc);
            SourceRange loc = left->loc;
            left = std::make_unique<CallExpr_>(loc, std::move(left), std::move(args));
        } else if (check(TokenKind::LBracket)) {
            // Index access: a[i]
            SourceRange openLoc = currentLoc();
            advance(); // consume [
            ExprPtr index = parseExpression();
            expectClosing(TokenKind::RBracket, "[", openLoc);
            SourceRange loc = left->loc;
            left = std::make_unique<IndexExpr_>(loc, std::move(left), std::move(index));
        } else if (check(TokenKind::Dot)) {
            // Field access: obj.field or obj.0 (tuple index)
            advance(); // consume .
            Token field;
            if (check(TokenKind::IntLiteral)) {
                field = advance();
            } else if (check(TokenKind::FloatLiteral)) {
                Token floatTok = advance();
                auto dotPos = floatTok.text.find('.');
                std::string first = floatTok.text.substr(0, dotPos);
                std::string second = floatTok.text.substr(dotPos + 1);
                SourceRange loc = left->loc;
                left = std::make_unique<FieldExpr_>(loc, std::move(left), first);
                left = std::make_unique<FieldExpr_>(loc, std::move(left), second);
                continue;
            } else {
                field = expect(TokenKind::Identifier, "Expected field name after '.'");
            }
            SourceRange loc = left->loc;
            left = std::make_unique<FieldExpr_>(loc, std::move(left), field.text);
        } else if (check(TokenKind::At)) {
            SourceRange loc = currentLoc();
            Token atTok = advance();
            int depth = 0;
            int cartesianIndex = 0;
            const std::string& text = atTok.text;
            if (text.size() >= 2 && text[1] >= '1' && text[1] <= '9') {
                depth = 1;
                cartesianIndex = text[1] - '0';
            } else {
                depth = (int)text.size();
            }
            left = std::make_unique<AutoMapExpr>(loc, std::move(left), depth, cartesianIndex);
        } else {
            break;
        }
    }
    return left;
}

ExprPtr Parser::parsePostfix(ExprPtr left) {
    while (true) {
        if (check(TokenKind::LParen)) {
            // Function call: f(args)
            SourceRange openLoc = currentLoc();
            advance(); // consume (
            ExprList args;
            if (!check(TokenKind::RParen)) {
                do {
                    args.push_back(parseCallArgument());
                } while (match(TokenKind::Comma));
            }
            expectClosing(TokenKind::RParen, "(", openLoc);
            SourceRange loc = left->loc;
            left = std::make_unique<CallExpr_>(loc, std::move(left), std::move(args));
        } else if (check(TokenKind::LBracket)) {
            // Index access: a[i]
            SourceRange openLoc = currentLoc();
            advance(); // consume [
            ExprPtr index = parseExpression();
            expectClosing(TokenKind::RBracket, "[", openLoc);
            SourceRange loc = left->loc;
            left = std::make_unique<IndexExpr_>(loc, std::move(left), std::move(index));
        } else if (check(TokenKind::Dot)) {
            // Field access: obj.field or obj.0 (tuple index)
            advance(); // consume .
            Token field;
            if (check(TokenKind::IntLiteral)) {
                field = advance();
            } else if (check(TokenKind::FloatLiteral)) {
                // Handle chained tuple access: expr.1.0 is lexed as Dot + FloatLiteral("1.0")
                // Split into two field accesses: .1 then .0
                Token floatTok = advance();
                auto dotPos = floatTok.text.find('.');
                std::string first = floatTok.text.substr(0, dotPos);
                std::string second = floatTok.text.substr(dotPos + 1);
                SourceRange loc = left->loc;
                left = std::make_unique<FieldExpr_>(loc, std::move(left), first);
                left = std::make_unique<FieldExpr_>(loc, std::move(left), second);
                continue;
            } else {
                field = expect(TokenKind::Identifier, "Expected field name after '.'");
            }
            SourceRange loc = left->loc;
            left = std::make_unique<FieldExpr_>(loc, std::move(left), field.text);
        } else if (check(TokenKind::At)) {
            // Postfix auto-map: expr @, expr @@, expr @1, expr @2, etc.
            SourceRange loc = currentLoc();
            Token atTok = advance(); // consume @, @@, @@@, @1, @2, etc.

            int depth = 0;
            int cartesianIndex = 0;
            const std::string& text = atTok.text;

            if (text.size() >= 2 && text[1] >= '1' && text[1] <= '9') {
                // @1, @2, ... @9
                depth = 1;
                cartesianIndex = text[1] - '0';
            } else {
                // @, @@, @@@, etc. - depth = number of @ chars
                depth = (int)text.size();
            }

            left = std::make_unique<AutoMapExpr>(loc, std::move(left), depth, cartesianIndex);
        } else if (check(TokenKind::Yield)) {
            // Pipeline yield: value yield  ->  yield(value)
            SourceRange loc = currentLoc();
            advance(); // consume 'yield'
            ExprPtr callee = std::make_unique<IdentifierExpr>(loc, "yield");
            ExprList args;
            args.push_back(std::move(left));
            left = std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
        } else if (check(TokenKind::Await)) {
            // Pipeline await: value await  ->  await(value)
            SourceRange loc = currentLoc();
            advance(); // consume 'await'
            ExprPtr callee = std::make_unique<IdentifierExpr>(loc, "await");
            ExprList args;
            args.push_back(std::move(left));
            left = std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
        } else if (check(TokenKind::As) && lexer_.peek().kind == TokenKind::LParen) {
            // as(TypeExpr): type cast/test on Any value
            SourceRange loc = currentLoc();
            advance(); // consume 'as'
            SourceRange asOpenLoc = currentLoc();
            advance(); // consume '('
            TypeExprPtr targetType = parseTypeExpr();
            expectClosing(TokenKind::RParen, "(", asOpenLoc);
            left = std::make_unique<AsTypeExprNode>(loc, std::move(left), std::move(targetType));
        } else if (check(TokenKind::Identifier) && !check(TokenKind::Equals)) {
            // Space-pipeline: `x abs` -> `abs(x)`, `x f(y)` -> `f(x, y)`
            // Also handles module-qualified: `x mod.func` -> `mod.func(x)`
            Token funcName = advance(); // consume identifier
            ExprPtr callee = std::make_unique<IdentifierExpr>(funcName.loc, funcName.text);

            // Handle dot notation for module-qualified names (e.g. math_utils.square)
            while (check(TokenKind::Dot)) {
                advance(); // consume .
                Token field = expect(TokenKind::Identifier, "Expected field name after '.'");
                SourceRange loc = funcName.loc;
                callee = std::make_unique<FieldExpr_>(loc, std::move(callee), field.text);
            }

            // Check if followed by ( for additional args
            if (check(TokenKind::LParen)) {
                SourceRange spOpenLoc = currentLoc();
                advance(); // consume (
                ExprList args;
                args.push_back(std::move(left)); // prepend receiver
                if (!check(TokenKind::RParen)) {
                    do {
                        args.push_back(parseCallArgument());
                    } while (match(TokenKind::Comma));
                }
                expectClosing(TokenKind::RParen, "(", spOpenLoc);
                SourceRange loc = funcName.loc;
                left = std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
            } else {
                // Simple pipeline: x abs -> abs(x)
                ExprList args;
                args.push_back(std::move(left));
                SourceRange loc = funcName.loc;
                left = std::make_unique<CallExpr_>(loc, std::move(callee), std::move(args));
            }
        } else {
            break;
        }
    }

    return left;
}

ExprPtr Parser::parseStructLiteral(const Token& nameTok) {
    SourceRange start = nameTok.loc;
    SourceRange openLoc = currentLoc();
    advance(); // consume {

    std::vector<StructFieldInit> fields;
    ExprPtr spreadExpr;

    // Check for spread-first syntax: StructName { ...source, field: val }
    if (check(TokenKind::Ellipsis)) {
        advance(); // consume '...'
        spreadExpr = parseExpression();
        if (check(TokenKind::Comma)) {
            advance();
        }
    }

    // Determine if this is positional or named field syntax.
    // Named: Point { x: 1, y: 2 }  — identifier followed by colon
    // Positional: Point { 1, 2 }    — expressions without field names
    bool positional = false;
    if (!check(TokenKind::RBrace)) {
        // Peek: if current is identifier and next is colon, it's named
        if (check(TokenKind::Identifier) && lexer_.peek().kind == TokenKind::Colon) {
            positional = false;
        } else {
            positional = true;
        }
    }

    if (positional) {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            u32 offsetBefore = current_.loc.start.offset;
            StructFieldInit field;
            field.loc = currentLoc();
            // name left empty — type checker fills it in by position
            field.value = parseExpression();
            fields.push_back(std::move(field));

            if (check(TokenKind::Comma)) {
                advance();
            }
            if (current_.loc.start.offset == offsetBefore) { synchronize(); }
        }
    } else {
        while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
            u32 offsetBefore = current_.loc.start.offset;
            StructFieldInit field;
            field.loc = currentLoc();
            Token fieldName = expect(TokenKind::Identifier, "Expected field name in struct literal");
            field.name = fieldName.text;
            expect(TokenKind::Colon, "Expected ':' after field name in struct literal");
            field.value = parseExpression();
            fields.push_back(std::move(field));

            if (check(TokenKind::Comma)) {
                advance();
            }
            if (current_.loc.start.offset == offsetBefore) { synchronize(); }
        }
    }

    expectClosing(TokenKind::RBrace, "{", openLoc);

    auto result = std::make_unique<StructLiteralExpr>(start, nameTok.text, std::move(fields));
    result->positional = positional;
    result->spreadExpr = std::move(spreadExpr);
    return result;
}

// --- Match statement ---

ASTPtr Parser::parseMatchStmt() {
    SourceRange start = currentLoc();
    advance(); // consume 'match'

    SourceRange matchParenLoc = currentLoc();
    expect(TokenKind::LParen, "Expected '(' after 'match'");
    ExprPtr subject = parseExpression();
    expectClosing(TokenKind::RParen, "(", matchParenLoc);

    SourceRange matchBraceLoc = currentLoc();
    expect(TokenKind::LBrace, "Expected '{' after match subject");

    std::vector<CaseClause> cases;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        u32 offsetBefore = current_.loc.start.offset;
        CaseClause clause;
        clause.loc = currentLoc();

        clause.pattern = parsePattern();

        // Check for guard: pattern 'if' '(' expr ')'
        if (check(TokenKind::If)) {
            SourceRange guardLoc = currentLoc();
            advance(); // consume 'if'
            SourceRange guardParenLoc = currentLoc();
            expect(TokenKind::LParen, "Expected '(' after 'if' in guard");
            ExprPtr guard = parseExpression();
            expectClosing(TokenKind::RParen, "(", guardParenLoc);
            clause.pattern = std::make_unique<GuardedPattern>(
                guardLoc, std::move(clause.pattern), std::move(guard));
        }

        expect(TokenKind::Colon, "Expected ':' after pattern");

        // Body: block or single statement
        if (check(TokenKind::LBrace)) {
            clause.body = parseBlock();
        } else {
            clause.body = parseStatement();
        }

        // An expression-statement body (`a: 1;`) already consumed its trailing ';'
        // via parseExprStmtOrAssign. A statement-form body -- a nested `match`, an
        // `if`/`while`/`for`, or a `{ ... }` block -- does NOT, so a ';' written
        // after it would dangle and make the next iteration parse ';' as a pattern.
        // Consume that optional separator here so a nested match (or any statement
        // body) may be terminated with ';' like an expression arm.
        match(TokenKind::Semicolon);

        cases.push_back(std::move(clause));
        if (current_.loc.start.offset == offsetBefore) { synchronize(); }
    }

    expectClosing(TokenKind::RBrace, "{", matchBraceLoc);

    return std::make_unique<SwitchStmtNode>(start, std::move(subject), std::move(cases));
}

// --- Pattern parsing ---

PatternPtr Parser::parsePattern() {
    PatternPtr left = parsePrimaryPattern();

    // Check for :: (cons pattern) - right-associative
    while (check(TokenKind::ColonColon)) {
        SourceRange loc = currentLoc();
        advance(); // consume ::
        PatternPtr tail = parsePattern(); // right-recursive for right-associativity
        left = std::make_unique<ConsPattern>(loc, std::move(left), std::move(tail));
        break; // only one iteration due to right-recursion
    }

    return left;
}

PatternPtr Parser::parsePrimaryPattern() {
    // nil pattern
    if (check(TokenKind::Nil)) {
        SourceRange loc = currentLoc();
        advance();
        auto lit = std::make_unique<NilLiteralExpr>(loc);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    // Array pattern: [pattern, pattern, ...name] or [pattern, ...]
    if (check(TokenKind::LBracket)) {
        SourceRange loc = currentLoc();
        advance(); // consume [
        std::vector<PatternPtr> elements;
        bool hasRest = false;
        std::string restName;

        if (!check(TokenKind::RBracket)) {
            while (true) {
                // Check for ... (rest pattern)
                if (check(TokenKind::Ellipsis)) {
                    advance(); // consume ...
                    hasRest = true;
                    // Optional rest binding name
                    if (check(TokenKind::Identifier)) {
                        restName = current_.text;
                        advance();
                    }
                    break;
                }
                elements.push_back(parsePattern());
                if (!match(TokenKind::Comma)) {
                    // Check for ... after last element (no comma before ...)
                    if (check(TokenKind::Ellipsis)) {
                        advance(); // consume ...
                        hasRest = true;
                        if (check(TokenKind::Identifier)) {
                            restName = current_.text;
                            advance();
                        }
                    }
                    break;
                }
            }
        }
        expectClosing(TokenKind::RBracket, "[", loc);
        return std::make_unique<ArrayPattern>(loc, std::move(elements), hasRest, std::move(restName));
    }

    // Tuple pattern: (pattern, pattern, ...) with optional rest: (a, b, ...rest) or (a, b, ...)
    if (check(TokenKind::LParen)) {
        SourceRange loc = currentLoc();
        advance(); // consume (
        std::vector<PatternPtr> elements;
        bool hasRest = false;
        std::string restName;
        if (!check(TokenKind::RParen)) {
            while (true) {
                if (check(TokenKind::Ellipsis)) {
                    advance();
                    hasRest = true;
                    if (check(TokenKind::Identifier) && current_.text != "_") {
                        restName = current_.text;
                        advance();
                    }
                    break;
                }
                elements.push_back(parsePattern());
                if (!match(TokenKind::Comma)) {
                    // Check for trailing ... after last element
                    if (check(TokenKind::Ellipsis)) {
                        advance();
                        hasRest = true;
                        if (check(TokenKind::Identifier) && current_.text != "_") {
                            restName = current_.text;
                            advance();
                        }
                    }
                    break;
                }
            }
        }
        expectClosing(TokenKind::RParen, "(", loc);
        return std::make_unique<TuplePattern>(loc, std::move(elements), hasRest, std::move(restName));
    }

    // Wildcard: _
    if (check(TokenKind::Identifier) && current_.text == "_") {
        SourceRange loc = currentLoc();
        advance();
        return std::make_unique<WildcardPattern>(loc);
    }

    // Bool literals
    if (check(TokenKind::True)) {
        SourceRange loc = currentLoc();
        advance();
        auto lit = std::make_unique<BoolLiteralExpr>(loc, true);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }
    if (check(TokenKind::False)) {
        SourceRange loc = currentLoc();
        advance();
        auto lit = std::make_unique<BoolLiteralExpr>(loc, false);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    // Numeric literals (possibly negative)
    bool negated = false;
    SourceRange negLoc;
    if (check(TokenKind::Minus)) {
        negLoc = currentLoc();
        negated = true;
        advance(); // consume -
    }

    if (check(TokenKind::IntLiteral)) {
        SourceRange loc = negated ? negLoc : currentLoc();
        Token tok = advance();
        i64 val = negated ? -tok.intValue : tok.intValue;
        auto lit = std::make_unique<IntLiteralExpr>(loc, val);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    if (check(TokenKind::FloatLiteral)) {
        SourceRange loc = negated ? negLoc : currentLoc();
        Token tok = advance();
        f64 val = negated ? -tok.floatValue : tok.floatValue;
        auto lit = std::make_unique<FloatLiteralExpr>(loc, val);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    if (check(TokenKind::FractionLiteral)) {
        SourceRange loc = negated ? negLoc : currentLoc();
        Token tok = advance();
        i64 num = negated ? -tok.intValue : tok.intValue;
        auto lit = std::make_unique<FractionLiteralExpr>(loc, num, tok.denominator);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    if (negated) {
        error("Expected number after '-' in pattern");
    }

    // String literal
    if (check(TokenKind::StringLiteral)) {
        SourceRange loc = currentLoc();
        Token tok = advance();
        auto lit = std::make_unique<StringLiteralExpr>(loc, tok.text);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    // Symbol literal
    if (check(TokenKind::SymbolLiteral)) {
        SourceRange loc = currentLoc();
        Token tok = advance();
        auto lit = std::make_unique<SymbolLiteralExpr>(loc, tok.text);
        return std::make_unique<LiteralPattern>(loc, std::move(lit));
    }

    // Identifier: binding, enum pattern, or struct pattern
    if (check(TokenKind::Identifier)) {
        Token nameTok = advance();

        // Type-test pattern for Any matching: name Type (lowercase name, uppercase/compound type)
        if (!nameTok.text.empty() && std::islower(nameTok.text[0]) &&
            (isTypeKeyword(current_.kind) ||
             check(TokenKind::LBracket) ||
             (check(TokenKind::Identifier) && !current_.text.empty() &&
              std::isupper(current_.text[0])))) {
            auto typeExpr = parseTypeExpr();
            return std::make_unique<TypeTestPattern>(
                nameTok.loc, nameTok.text, std::move(typeExpr));
        }

        // Enum pattern: Name.caseName or Name.caseName(pattern)
        if (check(TokenKind::Dot)) {
            advance(); // consume .
            Token caseTok = expect(TokenKind::Identifier, "Expected case name after '.'");

            PatternPtr innerPat;
            if (check(TokenKind::LParen)) {
                SourceRange epOpenLoc = currentLoc();
                advance(); // consume (
                if (!check(TokenKind::RParen)) {
                    innerPat = parsePattern();
                }
                expectClosing(TokenKind::RParen, "(", epOpenLoc);
            }

            return std::make_unique<EnumPattern>(
                nameTok.loc, nameTok.text, caseTok.text, std::move(innerPat));
        }

        // Tuple struct pattern: Name(pat, pat, ...rest)
        if (check(TokenKind::LParen)) {
            SourceRange loc = nameTok.loc;
            SourceRange tspOpenLoc = currentLoc();
            advance(); // consume (
            std::vector<PatternPtr> elements;
            bool hasRest = false;
            std::string restName;
            if (!check(TokenKind::RParen)) {
                while (true) {
                    if (check(TokenKind::Ellipsis)) {
                        advance();
                        hasRest = true;
                        if (check(TokenKind::Identifier) && current_.text != "_") {
                            restName = current_.text;
                            advance();
                        }
                        break;
                    }
                    elements.push_back(parsePattern());
                    if (!match(TokenKind::Comma)) {
                        if (check(TokenKind::Ellipsis)) {
                            advance();
                            hasRest = true;
                            if (check(TokenKind::Identifier) && current_.text != "_") {
                                restName = current_.text;
                                advance();
                            }
                        }
                        break;
                    }
                }
            }
            expectClosing(TokenKind::RParen, "(", tspOpenLoc);
            return std::make_unique<TuplePattern>(loc, std::move(elements),
                hasRest, std::move(restName), nameTok.text);
        }

        // Struct pattern: Name { field: pattern, ... }
        if (check(TokenKind::LBrace)) {
            SourceRange spOpenLoc = currentLoc();
            advance(); // consume {

            std::vector<StructPatternField> fields;
            while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
                u32 offsetBefore = current_.loc.start.offset;
                StructPatternField field;
                field.loc = currentLoc();
                Token fieldName = expect(TokenKind::Identifier, "Expected field name in struct pattern");
                field.name = fieldName.text;
                expect(TokenKind::Colon, "Expected ':' after field name in struct pattern");
                field.pattern = parsePattern();
                fields.push_back(std::move(field));

                if (check(TokenKind::Comma)) {
                    advance();
                }
                if (current_.loc.start.offset == offsetBefore) { synchronize(); }
            }

            expectClosing(TokenKind::RBrace, "{", spOpenLoc);

            return std::make_unique<StructPattern>(
                nameTok.loc, nameTok.text, std::move(fields));
        }

        // Otherwise: binding pattern
        return std::make_unique<BindingPattern>(nameTok.loc, nameTok.text);
    }

    error("Expected pattern");
    return std::make_unique<WildcardPattern>(currentLoc());
}

// --- Tentative type arg parsing ---

bool Parser::tryParseTypeArgs(std::vector<TypeExprPtr>& typeArgs) {
    // Save full parser + lexer state
    Token savedCurrent = current_;
    Token savedPrevious = previous_;
    auto lexerState = lexer_.save();
    size_t savedErrors = errors_.size();

    if (!check(TokenKind::Less)) return false;
    advance(); // consume '<'

    // Try to parse comma-separated type expressions
    do {
        // Check if current token can start a type expression
        if (current_.kind != TokenKind::KwInt && current_.kind != TokenKind::KwFloat &&
            current_.kind != TokenKind::KwString && current_.kind != TokenKind::KwBool &&
            current_.kind != TokenKind::KwSymbol && current_.kind != TokenKind::KwVoid &&
            current_.kind != TokenKind::KwFraction && current_.kind != TokenKind::KwComplex &&
            current_.kind != TokenKind::KwAny &&
            current_.kind != TokenKind::Identifier &&
            current_.kind != TokenKind::LBracket &&
            current_.kind != TokenKind::LParen) {
            // Not a type expression start — restore and fail
            current_ = savedCurrent;
            previous_ = savedPrevious;
            lexer_.restore(lexerState);
            while (errors_.size() > savedErrors) errors_.pop_back();
            return false;
        }
        typeArgs.push_back(parseTypeExpr());
    } while (match(TokenKind::Comma));

    // Must close with '>'
    if (!matchGreater()) {
        // Restore state
        typeArgs.clear();
        current_ = savedCurrent;
        previous_ = savedPrevious;
        lexer_.restore(lexerState);
        while (errors_.size() > savedErrors) errors_.pop_back();
        return false;
    }

    // Successfully parsed <Type, ...>
    // Discard any errors generated during tentative parsing
    while (errors_.size() > savedErrors) errors_.pop_back();
    return true;
}

// --- Type expressions ---

TypeExprPtr Parser::parseTypeExpr() {
    // Existential type: `some C`. `some` is a contextual keyword (it stays a
    // valid identifier elsewhere, e.g. enum case names), so we only treat it as
    // the existential introducer when it is immediately followed by a
    // constraint name. Otherwise it is an ordinary named type.
    if (current_.kind == TokenKind::Identifier && current_.text == "some") {
        Token someTok = advance();  // consume 'some'
        if (current_.kind == TokenKind::Identifier) {
            Token cname = advance();
            return std::make_unique<ExistentialTypeNode>(someTok.loc, cname.text);
        }
        // Not `some C`: treat the consumed token as a plain type named "some".
        return std::make_unique<NamedTypeNode>(someTok.loc, someTok.text);
    }

    // Persistent collection type: #[Type] or #[KeyType: ValueType]
    // Mutable collection type:    [Type]  or  [KeyType: ValueType]
    if (current_.kind == TokenKind::Hash || current_.kind == TokenKind::LBracket) {
        SourceRange loc = currentLoc();
        bool isImmutable = false;
        if (current_.kind == TokenKind::Hash) {
            isImmutable = true;
            advance(); // consume #
            expect(TokenKind::LBracket, "Expected '[' after '#' in a persistent collection type");
        } else {
            advance(); // consume [
        }
        // Empty map type: [:] is not valid as a type (need [K: V])
        auto keyOrElemType = parseTypeExpr();
        if (check(TokenKind::Colon)) {
            // Map type: [KeyType: ValueType]
            advance(); // consume :
            auto valueType = parseTypeExpr();
            expectClosing(TokenKind::RBracket, "[", loc);
            auto m = std::make_unique<MapTypeNode>(loc, std::move(keyOrElemType), std::move(valueType));
            m->isImmutable = isImmutable;
            return m;
        }
        expectClosing(TokenKind::RBracket, "[", loc);
        auto a = std::make_unique<ArrayTypeNode>(loc, std::move(keyOrElemType));
        a->isImmutable = isImmutable;
        return a;
    }

    // Tuple type: (Type, Type, ...) or parenthesized type: (Type)
    // or function type: (Type, Type) ReturnType  /  () ReturnType
    if (current_.kind == TokenKind::LParen) {
        SourceRange loc = currentLoc();
        advance(); // consume '('

        // () ReturnType — zero-argument function type
        if (check(TokenKind::RParen)) {
            advance(); // consume ')'
            // Check if a return type follows
            if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
                current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
                current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
                current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
                current_.kind == TokenKind::KwAny ||
                current_.kind == TokenKind::Identifier ||
                current_.kind == TokenKind::LBracket ||
                current_.kind == TokenKind::LParen ||
                current_.kind == TokenKind::Fn) {
                auto retType = parseTypeExpr();
                return std::make_unique<FunctionTypeNode>(loc, std::vector<TypeExprPtr>{}, std::move(retType));
            }
            // () with no return type — unit type (empty tuple)
            return std::make_unique<TupleTypeNode>(loc, std::vector<TypeExprPtr>{});
        }

        auto first = parseTypeExpr();
        if (check(TokenKind::Comma)) {
            std::vector<TypeExprPtr> elems;
            elems.push_back(std::move(first));
            while (check(TokenKind::Comma)) {
                advance(); // consume ','
                // Trailing comma: (Type,) is a 1-tuple type
                if (check(TokenKind::RParen)) break;
                elems.push_back(parseTypeExpr());
            }
            expectClosing(TokenKind::RParen, "(", loc);
            // Check if a return type follows — if so, this is a function type
            if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
                current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
                current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
                current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
                current_.kind == TokenKind::KwAny ||
                current_.kind == TokenKind::Identifier ||
                current_.kind == TokenKind::LBracket ||
                current_.kind == TokenKind::LParen ||
                current_.kind == TokenKind::Fn) {
                auto retType = parseTypeExpr();
                return std::make_unique<FunctionTypeNode>(loc, std::move(elems), std::move(retType));
            }
            return std::make_unique<TupleTypeNode>(loc, std::move(elems));
        }
        expectClosing(TokenKind::RParen, "(", loc);
        // Single-arg function type: (Type) ReturnType
        if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
            current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
            current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
            current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
            current_.kind == TokenKind::KwAny ||
            current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::LParen ||
            current_.kind == TokenKind::Fn) {
            auto retType = parseTypeExpr();
            std::vector<TypeExprPtr> params;
            params.push_back(std::move(first));
            return std::make_unique<FunctionTypeNode>(loc, std::move(params), std::move(retType));
        }
        return first; // parenthesized single type
    }

    // Function type: fn(Type, Type) ReturnType  /  fn() ReturnType
    if (current_.kind == TokenKind::Fn) {
        SourceRange loc = currentLoc();
        advance(); // consume 'fn'
        SourceRange fnTypeParenLoc = currentLoc();
        expect(TokenKind::LParen, "Expected '(' after 'fn' in function type");
        std::vector<TypeExprPtr> params;
        if (!check(TokenKind::RParen)) {
            do {
                params.push_back(parseTypeExpr());
            } while (match(TokenKind::Comma));
        }
        expectClosing(TokenKind::RParen, "(", fnTypeParenLoc);
        // Return type is optional (defaults to Void)
        TypeExprPtr retType;
        if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
            current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
            current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
            current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
            current_.kind == TokenKind::KwAny ||
            current_.kind == TokenKind::Identifier ||
            current_.kind == TokenKind::LBracket ||
            current_.kind == TokenKind::LParen ||
            current_.kind == TokenKind::Fn) {
            retType = parseTypeExpr();
        } else {
            retType = std::make_unique<NamedTypeNode>(loc, "Void");
        }
        return std::make_unique<FunctionTypeNode>(loc, std::move(params), std::move(retType));
    }

    // Named types (int, float, string, bool, void, fraction, complex, any, custom)
    if (current_.kind == TokenKind::KwInt || current_.kind == TokenKind::KwFloat ||
        current_.kind == TokenKind::KwString || current_.kind == TokenKind::KwBool ||
        current_.kind == TokenKind::KwSymbol || current_.kind == TokenKind::KwVoid ||
        current_.kind == TokenKind::KwFraction || current_.kind == TokenKind::KwComplex ||
        current_.kind == TokenKind::KwAny ||
        current_.kind == TokenKind::Identifier) {
        Token tok = advance();
        // Check for Name<T, U, ...> template type
        if (tok.kind == TokenKind::Identifier && check(TokenKind::Less)) {
            SourceRange loc = tok.loc;
            advance(); // consume <
            if (tok.text == "Array") {
                auto elemType = parseTypeExpr();
                if (!matchGreater()) {
                    error("Expected '>' after Array element type");
                }
                error(loc, "Use [T] for array types, not Array<T>");
                return std::make_unique<ArrayTypeNode>(loc, std::move(elemType));
            }
            // Special case: List<T> -> ListTypeNode for backward compatibility
            if (tok.text == "List") {
                auto elemType = parseTypeExpr();
                if (!matchGreater()) {
                    error("Expected '>' after List element type");
                }
                return std::make_unique<ListTypeNode>(loc, std::move(elemType));
            }
            // Special case: Ref<T> -> RefTypeNode
            if (tok.text == "Ref") {
                auto elemType = parseTypeExpr();
                if (!matchGreater()) {
                    error("Expected '>' after Ref element type");
                }
                return std::make_unique<RefTypeNode>(loc, std::move(elemType));
            }
            // Special case: Set<T> -> SetTypeNode
            if (tok.text == "Set") {
                auto elemType = parseTypeExpr();
                if (!matchGreater()) {
                    error("Expected '>' after Set element type");
                }
                return std::make_unique<SetTypeNode>(loc, std::move(elemType));
            }
            // General template type: Name<T, U, ...>
            std::vector<TypeExprPtr> typeArgs;
            do {
                typeArgs.push_back(parseTypeExpr());
            } while (match(TokenKind::Comma));
            if (!matchGreater()) {
                error("Expected '>' after type arguments");
            }
            return std::make_unique<TemplateTypeNode>(loc, tok.text, std::move(typeArgs));
        }
        return std::make_unique<NamedTypeNode>(tok.loc, tok.text);
    }

    error("Expected type name");
    return std::make_unique<NamedTypeNode>(currentLoc(), "Void");
}

} // namespace ts
