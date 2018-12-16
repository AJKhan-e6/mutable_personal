#include "parse/Parser.hpp"

#include "util/macro.hpp"
#include <utility>

#undef DEBUG
#define DEBUG(X)


using namespace db;


/** Returns the precedence of an operator.  A higher value means the operator has higher precedence. */
int get_precedence(const TokenType tt)
{
    int p = 0;
    /* List all binary operators.  The higher up an operator is in the switch statement, the higher its precedence. */
    switch (tt) {
        default:                    return -1;
        /* bitwise NOT */
        case TK_TILDE:              ++p;
        /* multiplicative */
        case TK_ASTERISK:
        case TK_SLASH:
        case TK_PERCENT:            ++p;
        /* additive */
        case TK_PLUS:
        case TK_MINUS:              ++p;
        /* comparison */
        case TK_LESS:
        case TK_GREATER:
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
        case TK_EQUAL:
        case TK_BANG_EQUAL:         ++p;
        /* logical NOT */
        case TK_Not:                ++p;
        /* logical AND */
        case TK_And:                ++p;
        /* logical OR */
        case TK_Or:                 ++p;
    }
    return p;
}

Stmt * Parser::parse()
{
    Stmt *stmt = nullptr;
    switch (token().type) {
        default:
            diag.e(token().pos) << "expected a statement, got " << token().text << '\n';
            consume();
            return nullptr;

        case TK_Select: stmt = parse_SelectStmt(); break;
        case TK_Update: stmt = parse_UpdateStmt(); break;
        case TK_Delete: stmt = parse_DeleteStmt(); break;
    }
    expect(TK_SEMICOL);
    return stmt;
}

/*======================================================================================================================
 * Statements
 *====================================================================================================================*/

SelectStmt * Parser::parse_SelectStmt()
{
    SelectStmt *stmt = new SelectStmt();

    /* 'SELECT' */
    expect(TK_Select);

    /* ( '*' | expression [ 'AS' identifier ] ) */
    if (token() == TK_ASTERISK) {
        consume();
        stmt->select_all = true;
    } else {
        auto e = parse_Expr();
        Token tok;
        if (accept(TK_As)) {
            tok = token();
            expect(TK_IDENTIFIER);
        }
        stmt->select.push_back(std::make_pair(e, tok));
    }

    /* { ',' expression [ 'AS' identifier ] } */
    while (accept(TK_COMMA)) {
        auto e = parse_Expr();
        Token tok;
        if (accept(TK_As)) {
            tok = token();
            expect(TK_IDENTIFIER);
        }
        stmt->select.push_back(std::make_pair(e, tok));
    }

    /* 'FROM' identifier [ 'AS' identifier ] { ',' identifier [ 'AS' identifier ] } */
    expect(TK_From);
    do {
        Token table = token();
        Token as;
        expect(TK_IDENTIFIER);
        if (accept(TK_As)) {
            as = token();
            expect(TK_IDENTIFIER);
        }
        stmt->from.push_back(std::make_pair(table, as));
    } while (accept(TK_COMMA));

    if (accept(TK_Where)) stmt->where = parse_Expr();
    if (token() == TK_Group) stmt->group_by = parse_group_by_clause();
    if (token() == TK_Order) stmt->order_by = parse_order_by_clause();
    if (token() == TK_Limit) stmt->limit = parse_limit_clause();

    return stmt;
}

Stmt * Parser::parse_InsertStmt()
{
    InsertStmt *stmt = new InsertStmt();

    /* 'INSERT' 'INTO' identifier 'VALUES' */
    expect(TK_Insert);
    expect(TK_Into);
    stmt->table_name = token();
    expect(TK_IDENTIFIER);
    expect(TK_Values);

    /* ( 'DEFAULT' | 'NULL' | expression ) { ',' ( 'DEFAULT' | 'NULL' | expression ) } */
    do {
        switch (token().type) {
            case TK_Default:
                consume();
                stmt->values.push_back({InsertStmt::I_Default});
                break;

            case TK_Null:
                consume();
                stmt->values.push_back({InsertStmt::I_Null});
                break;

            default: {
                auto e = parse_Expr();
                stmt->values.push_back({InsertStmt::I_Expr, e});
                break;
            }
        }
    } while (accept(TK_COMMA));

    return stmt;
}

Stmt * Parser::parse_UpdateStmt()
{
    UpdateStmt *stmt = new UpdateStmt();

    /* update-clause ::= 'UPDATE' identifier 'SET' identifier '=' expression { ',' identifier '=' expression } ; */
    expect(TK_Update);
    stmt->table_name = token();
    expect(TK_IDENTIFIER);
    expect(TK_Set);

    do {
        auto id = token();
        expect(TK_IDENTIFIER);
        expect(TK_EQUAL);
        auto e = parse_Expr();
        stmt->set.push_back(std::make_pair(id, e));
    } while (accept(TK_COMMA));

    if (accept(TK_Where))
        parse_Expr();

    return stmt;
}

Stmt * Parser::parse_DeleteStmt()
{
    DeleteStmt *stmt = new DeleteStmt();

    /* delete-statement ::= 'DELETE' 'FROM' identifier [ where-clause ] ; */
    expect(TK_Delete);
    expect(TK_From);
    stmt->table_name = token();
    expect(TK_IDENTIFIER);

    if (accept(TK_Where))
        stmt->where = parse_Expr();

    return stmt;
}

/*======================================================================================================================
 * Clauses
 *====================================================================================================================*/

std::vector<Expr*> Parser::parse_group_by_clause()
{
    /* 'GROUP' 'BY' designator { ',' designator } */
    std::vector<Expr*> group_by;
    expect(TK_Group);
    expect(TK_By);
    do
        group_by.push_back(parse_designator());
    while (accept(TK_COMMA));
    return group_by;
}

std::vector<std::pair<Expr*, bool>> Parser::parse_order_by_clause()
{
    /* 'ORDER' 'BY' designator [ 'ASC' | 'DESC' ] { ',' designator [ 'ASC' | 'DESC' ] } */
    std::vector<std::pair<Expr*, bool>> order_by;
    expect(TK_Order);
    expect(TK_By);

    do {
        auto d = parse_designator();
        if (accept(TK_Descending)) {
            order_by.push_back(std::make_pair(d, false));
        } else {
            accept(TK_Ascending);
            order_by.push_back(std::make_pair(d, true));
        }
    } while (accept(TK_COMMA));

    return order_by;
}

std::pair<Expr*, Expr*> Parser::parse_limit_clause()
{
    /* 'LIMIT' integer-constant [ 'OFFSET' integer-constant ] */
    expect(TK_Limit);
    Expr *limit = expect_integer();
    Expr *offset = nullptr;
    if (accept(TK_Offset))
        offset = expect_integer();
    return {limit, offset};
}

/*======================================================================================================================
 * Expressions
 *====================================================================================================================*/

Expr * Parser::parse_Expr(const int precedence_lhs, Expr *lhs)
{
    DEBUG('(' << precedence_lhs << ", " << (lhs ? "expr" : "NULL") << ')');

    /*
     * primary-expression::= designator | constant | '(' expression ')' ;
     * unary-expression ::= [ '+' | '-' | '~' ] postfix-expression ;
     * logical-not-expression ::= 'NOT' logical-not-expression | comparative-expression ;
     */
    switch (token().type) {
        /* primary-expression */
        case TK_IDENTIFIER:
            lhs = parse_designator(); // XXX For SUM(x), 'SUM' is parsed as designator; should be identifier.
            break;
        case TK_STRING_LITERAL:
        case TK_OCT_INT:
        case TK_DEC_INT:
        case TK_HEX_INT:
        case TK_DEC_FLOAT:
        case TK_HEX_FLOAT:
            lhs = new Constant(consume());
            break;
        case TK_LPAR:
            consume();
            lhs = parse_Expr();
            expect(TK_RPAR);
            break;

        /* unary-expression */
        case TK_PLUS:
        case TK_MINUS:
        case TK_TILDE: {
            auto tok = consume();
            int p = get_precedence(TK_TILDE); // the precedence of TK_TILDE equals that of unary plus and minus
            lhs = new UnaryExpr(tok, parse_Expr(p));
            break;
        }

        /* logical-NOT-expression */
        case TK_Not: {
            auto tok = consume();
            int p = get_precedence(tok.type);
            lhs = new UnaryExpr(tok, parse_Expr(p));
            break;
        }

        default:
            diag.e(token().pos) << "expected expression, got " << token().text << '\n';
            lhs = new ErrorExpr(token());
    }

    /* postfix-expression ::= postfix-expression '(' [ expression { ',' expression } ] ')' | primary-expression */
    while (accept(TK_LPAR)) {
        std::vector<Expr*> args;
        if (token().type != TK_RPAR) {
            do
                args.push_back(parse_Expr());
            while (accept(TK_COMMA));
        }
        expect(TK_RPAR);
        lhs = new FnApplicationExpr(lhs, args);
    }

    for (;;) {
        Token op = token();
        int p = get_precedence(op);
        DEBUG("potential binary operator " << token().text << " with precedence " << p);
        if (precedence_lhs > p) return lhs; // left operator has higher precedence_lhs
        DEBUG("binary operator with higher precedence " << token());
        consume();

        DEBUG("recursive call to parse_Expr(" << p+1 << ')');
        Expr *rhs = parse_Expr(p + 1);
        lhs = new BinaryExpr(op, lhs, rhs);
    }
}

#if 0
Expr * Parser::parse_Expr(void *lhs, const int precedence_lhs)
{
    for (;;) {
        Token op = token();
        int p = get_precedence(op);
        if (p < precedence_lhs) return;
        consume();

        /* TODO what to do about NOT, which has lower precedence than some binary operators?
         * Maybe we should do LR parsing for all expressions, and add a respective entry for each operator (binary and
         * unique) to `get_precedence()`. */

        void *rhs = nullptr;
        parse_Expr();
        parse_Expr(rhs, p + 1);
        /* TODO merge lhs/rhs */
    }
}
#endif

Expr * Parser::parse_designator()
{
    Token lhs = token();
    if (not expect(TK_IDENTIFIER))
        return new ErrorExpr(lhs);
    if (accept(TK_DOT)) {
        Token rhs = token();
        if (not expect(TK_IDENTIFIER))
            return new ErrorExpr(rhs);
        return new Designator(lhs, rhs); // tbl.attr
    }
    return new Designator(lhs); // attr
}

Expr * Parser::expect_integer()
{
    switch (token().type) {
        case TK_OCT_INT:
        case TK_DEC_INT:
        case TK_HEX_INT:
            return new Constant(consume());

        default:
            diag.e(token().pos) << "expected integer constant, got " << token().text << '\n';
            return new ErrorExpr(token());
    }
}
