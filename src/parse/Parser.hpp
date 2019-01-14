#pragma once


#include "lex/Lexer.hpp"
#include "lex/Token.hpp"
#include "lex/TokenType.hpp"
#include "parse/AST.hpp"
#include "util/Diagnostic.hpp"


namespace db {

struct Parser
{
    public:
    Lexer      &lexer;
    Diagnostic &diag;

    private:
    Token tok_;

    public:
    explicit Parser(Lexer &lexer)
        : lexer(lexer)
        , diag(lexer.diag)
        , tok_(Position(lexer.filename), "ERROR", TK_ERROR)
    {
        consume();
    }

    const Token & token() const { return tok_; }

    bool no(const TokenType tt) { return token() != tt; }

    Token consume() {
        auto old = tok_;
        tok_ = lexer.next();
        return old;
    }

    bool accept(const TokenType tt) {
        if (token() == tt or token() == TK_ERROR) {
            consume();
            return true;
        }
        return false;
    }

    bool expect(const TokenType tt) {
        if (accept(tt)) return true;
        diag.e(token().pos) << "expected " << tt << ", got " << token().text << '\n';
        return false;
    }

    Stmt * parse();

    /* Statements */
    Stmt * parse_CreateDatabaseStmt();
    Stmt * parse_UseDatabaseStmt();
    Stmt * parse_CreateTableStmt();
    Stmt * parse_SelectStmt();
    Stmt * parse_InsertStmt();
    Stmt * parse_UpdateStmt();
    Stmt * parse_DeleteStmt();

    /* Clauses */
    Clause * parse_SelectClause();
    Clause * parse_FromClause();
    Clause * parse_WhereClause();
    Clause * parse_GroupByClause();
    Clause * parse_HavingClause();
    Clause * parse_OrderByClause();
    Clause * parse_LimitClause();

    /* Expressions */
    Expr * parse_Expr(int precedence_lhs = 0, Expr *lhs = nullptr);
    Expr * parse_designator();
    Expr * expect_integer();

    /* Types */
    const Type * parse_data_type();
};

}
