#pragma once

#include "lex/Token.hpp"
#include <iostream>
#include <vector>


namespace db {

// forward declare the AST visitor
template<bool C>
struct TheASTVisitor;
using ASTVisitor = TheASTVisitor<false>;
using ConstASTVisitor = TheASTVisitor<true>;

struct Type;
struct Sema;

/*======================================================================================================================
 * Expressions
 *====================================================================================================================*/

/** An expression. */
struct Expr
{
    friend struct Sema;

    private:
    const Type *type_ = nullptr; ///> the type of an expression, determined by the semantic analysis

    public:
    virtual ~Expr() { }

    const Type * type() const { return notnull(type_); }

    virtual void accept(ASTVisitor &v) = 0;
    virtual void accept(ConstASTVisitor &v) const = 0;

    void dump(std::ostream &out) const;
    void dump() const;

    friend std::ostream & operator<<(std::ostream &out, const Expr &e);
};

/** The error expression.  Used when the parser encountered a syntactical error. */
struct ErrorExpr : Expr
{
    Token tok;

    explicit ErrorExpr(Token tok) : tok(tok) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A designator.  Identifies an attribute, optionally preceeded by a table name. */
struct Designator : Expr
{
    Token table_name;
    Token attr_name;

    explicit Designator(Token attr_name) : attr_name(attr_name) { }

    Designator(Token table_name, Token attr_name) : table_name(table_name), attr_name(attr_name) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;

    bool has_table_name() const { return bool(table_name); }
};

/** A constant: a string literal or a numeric constant. */
struct Constant : Expr
{
    Token tok;

    Constant(Token tok) : tok(tok) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;

    bool is_number() const;
    bool is_integer() const;
    bool is_float() const;
    bool is_string() const;
};

/** A postfix expression. */
struct PostfixExpr : Expr
{
};

/** A function application. */
struct FnApplicationExpr : PostfixExpr
{
    Expr *fn;
    std::vector<Expr*> args;

    FnApplicationExpr(Expr *fn, std::vector<Expr*> args) : fn(fn), args(args) { }
    ~FnApplicationExpr();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A unary expression: "+e", "-e", "~e", "NOT e". */
struct UnaryExpr : Expr
{
    Token op;
    Expr *expr;

    UnaryExpr(Token op, Expr *expr) : op(op), expr(notnull(expr)) { }
    ~UnaryExpr() { delete expr; }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A binary expression.  This includes all arithmetic and logical binary operations. */
struct BinaryExpr : Expr
{
    Token op;
    Expr *lhs;
    Expr *rhs;

    BinaryExpr(Token op, Expr *lhs, Expr *rhs) : op(op), lhs(notnull(lhs)), rhs(notnull(rhs)) { }
    ~BinaryExpr() { delete lhs; delete rhs; }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/*======================================================================================================================
 * Clauses
 *====================================================================================================================*/

struct Clause
{
    Token tok;

    Clause(Token tok) : tok(tok) { }
    virtual ~Clause() { }

    virtual void accept(ASTVisitor &v) = 0;
    virtual void accept(ConstASTVisitor &v) const = 0;

    void dump(std::ostream &out) const;
    void dump() const;

    friend std::ostream & operator<<(std::ostream &out, const Clause &c);
};

struct ErrorClause : Clause
{
    ErrorClause(Token tok) : Clause(tok) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct SelectClause : Clause
{
    using select_type = std::pair<Expr*, Token>; ///> list of selected elements; expr AS name

    std::vector<select_type> select;
    bool select_all;

    SelectClause(Token tok, std::vector<select_type> select, bool select_all)
        : Clause(tok)
        , select(select)
        , select_all(select_all)
    { }
    ~SelectClause();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct FromClause : Clause
{
    using from_type = std::pair<Token, Token>; ///> first is the table name, second is the alias

    std::vector<from_type> from;

    FromClause(Token tok, std::vector<from_type> from) : Clause(tok), from(from) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct WhereClause : Clause
{
    Expr *where;

    WhereClause(Token tok, Expr *where) : Clause(tok), where(notnull(where)) { }
    ~WhereClause();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct GroupByClause : Clause
{
    std::vector<Expr*> group_by;

    GroupByClause(Token tok, std::vector<Expr*> group_by) : Clause(tok), group_by(group_by) { }
    ~GroupByClause();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct HavingClause : Clause
{
    Expr *having;

    HavingClause(Token tok, Expr *having) : Clause(tok), having(having) { }
    ~HavingClause();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct OrderByClause : Clause
{
    using order_type = std::pair<Expr*, bool>; ///> true means ascending, false means descending

    std::vector<order_type> order_by;

    OrderByClause(Token tok, std::vector<order_type> order_by) : Clause(tok), order_by(order_by) { }
    ~OrderByClause();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct LimitClause : Clause
{
    Token limit;
    Token offset;

    LimitClause(Token tok, Token limit, Token offset) : Clause(tok), limit(limit), offset(offset) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/*======================================================================================================================
 * Statements
 *====================================================================================================================*/

/** A SQL statement. */
struct Stmt
{
    virtual ~Stmt() { }

    virtual void accept(ASTVisitor &v) = 0;
    virtual void accept(ConstASTVisitor &v) const = 0;

    void dump(std::ostream &out) const;
    void dump() const;

    friend std::ostream & operator<<(std::ostream &out, const Stmt &s);
};

/** The error statement.  Used when the parser encountered a syntactical error. */
struct ErrorStmt : Stmt
{
    Token tok;

    explicit ErrorStmt(Token tok) : tok(tok) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct EmptyStmt : Stmt
{
    Token tok;

    explicit EmptyStmt(Token tok) : tok(tok) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct CreateDatabaseStmt : Stmt
{
    Token database_name;

    explicit CreateDatabaseStmt(Token database_name) : database_name(database_name) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};


struct UseDatabaseStmt : Stmt
{
    Token database_name;

    explicit UseDatabaseStmt(Token database_name) : database_name(database_name) { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

struct CreateTableStmt : Stmt
{
    using attribute_type = std::pair<Token, const Type*>;

    Token table_name;
    std::vector<attribute_type> attributes;

    CreateTableStmt(Token table_name, std::vector<attribute_type> attributes)
        : table_name(table_name)
        , attributes(attributes)
    { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A SQL select statement. */
struct SelectStmt : Stmt
{
    Clause *select;
    Clause *from;
    Clause *where;
    Clause *group_by;
    Clause *having;
    Clause *order_by;
    Clause *limit;

    SelectStmt(Clause *select,
               Clause *from,
               Clause *where,
               Clause *group_by,
               Clause *having,
               Clause *order_by,
               Clause *limit)
        : select(notnull(select))
        , from(notnull(from))
        , where(where)
        , group_by(group_by)
        , having(having)
        , order_by(order_by)
        , limit(limit)
    { }

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;

    ~SelectStmt();
};

/** A SQL insert statement. */
struct InsertStmt : Stmt
{
    enum kind_t { I_Default, I_Null, I_Expr };
    using element_type = std::pair<kind_t, Expr*>;
    using value_type = std::vector<element_type>;

    Token table_name;
    std::vector<value_type> values;

    InsertStmt(Token table_name, std::vector<value_type> values) : table_name(table_name), values(values) { }
    ~InsertStmt();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A SQL update statement. */
struct UpdateStmt : Stmt
{
    using set_type = std::pair<Token, Expr*>;

    Token table_name;
    std::vector<set_type> set;
    Clause *where = nullptr;

    UpdateStmt(Token table_name, std::vector<set_type> set, Clause *where)
        : table_name(table_name)
        , set(set)
        , where(where)
    { }

    ~UpdateStmt();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

/** A SQL delete statement. */
struct DeleteStmt : Stmt
{
    Token table_name;
    Clause *where = nullptr;

    DeleteStmt(Token table_name, Clause *where) : table_name(table_name), where(where) { }
    ~DeleteStmt();

    void accept(ASTVisitor &v);
    void accept(ConstASTVisitor &v) const;
};

}
