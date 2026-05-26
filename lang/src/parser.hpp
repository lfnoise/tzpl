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
//  parser.hpp
//  lang
//
//  Recursive descent parser with Pratt expression parsing
//

#ifndef parser_hpp
#define parser_hpp

#include "lexer.hpp"
#include "ast.hpp"
#include <memory>

namespace ts {

class Parser {
public:
    Parser(Lexer& lexer);

    // Parse a complete program
    Program parseProgram();

    // Error access
    const std::vector<CompileError>& errors() const { return errors_; }
    bool hasErrors() const { return !errors_.empty(); }

private:
    Lexer& lexer_;
    Token current_;
    Token previous_;
    std::vector<CompileError> errors_;

    // Token handling
    Token advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    Token expect(TokenKind kind, const std::string& msg);
    Token expectClosing(TokenKind kind, const char* open, SourceRange openLoc);
    bool matchGreater();      // Match '>' with ">>" splitting (for template close)
    void expectTerminator();  // Expect ; or EOF or }

    // Declarations
    ASTPtr parseDeclaration();
    ASTPtr parseImportDecl();
    ASTPtr parseLetDecl();
    ASTPtr parseVarDecl();
    ASTPtr parseConstDecl();
    ASTPtr parseFnDecl();
    ASTPtr parseStructDecl();
    ASTPtr parseUnionDecl();
    ASTPtr parseTypeAliasDecl();
    ASTPtr parseConstraintDecl();
    void parseInlineConstraints(const std::string& typeParam, const Token& tp,
                                std::vector<WhereConstraint>& whereConstraints);
    void parseWhereClause(std::vector<WhereConstraint>& whereConstraints);

    // Statements
    ASTPtr parseStatement();
    ASTPtr parseBlock();
    ASTPtr parseIfStmt();
    ASTPtr parseWhileStmt();
    ASTPtr parseForStmt();
    ASTPtr parseMatchStmt();
    ASTPtr parseReturnStmt();
    ASTPtr parseBreakStmt();
    ASTPtr parseContinueStmt();
    ASTPtr parseExprStmtOrAssign();

    // Patterns (for switch/case)
    PatternPtr parsePattern();
    PatternPtr parsePrimaryPattern();

    // Expressions (Pratt parsing)
    ExprPtr parseExpression(int minPrec = 0);
    ExprPtr parsePrimary();
    // Parses a bracketed array/map literal body. Assumes the opening '[' has
    // already been consumed. When isImmutable is true the result is a
    // persistent vector/map literal (#[...]).
    ExprPtr parseBracketLiteral(SourceRange loc, bool isImmutable);
    ExprPtr parsePostfix(ExprPtr left);
    ExprPtr parseTightPostfix(ExprPtr left);
    ExprPtr parseStructLiteral(const Token& nameTok);
    int getPrecedence(TokenKind kind) const;
    BinaryOpExpr::Op getBinaryOp(TokenKind kind) const;

    // Type expressions
    TypeExprPtr parseTypeExpr();

    // Tentative parsing: try to parse <Type, Type, ...> returning type args.
    // Returns true (and fills typeArgs) if successful; restores state on failure.
    bool tryParseTypeArgs(std::vector<TypeExprPtr>& typeArgs);

    // Helpers
    void error(const std::string& msg);
    void error(SourceRange loc, const std::string& msg);
    void synchronize();
    SourceRange currentLoc() const;
};

} // namespace ts

#endif /* parser_hpp */
