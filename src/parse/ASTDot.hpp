#pragma once

#include "parse/ASTVisitor.hpp"
#include <iostream>


namespace db {

/** Implements printing the AST in dot language. */
struct ASTDot : ConstASTVisitor
{
    using ConstASTVisitor::operator();

    static constexpr const char * const GRAPH_TYPE = "graph";
    static constexpr const char * const EDGE = " -- ";

    std::ostream &out; ///< the output stream to print to

    private:
    int indent_; ///< the current indentation for pretty printing

    public:
    /**
     * @param out the output stream to print to
     * @param indent the initial indentation
     */
    ASTDot(std::ostream &out, int indent = 0);
    ~ASTDot();

    /* Expressions */
    void operator()(Const<ErrorExpr> &e);
    void operator()(Const<Designator> &e);
    void operator()(Const<Constant> &e);
    void operator()(Const<FnApplicationExpr> &e);
    void operator()(Const<UnaryExpr> &e);
    void operator()(Const<BinaryExpr> &e);

    /* Clauses */
    void operator()(Const<ErrorClause> &c);
    void operator()(Const<SelectClause> &c);
    void operator()(Const<FromClause> &c);
    void operator()(Const<WhereClause> &c);
    void operator()(Const<GroupByClause> &c);
    void operator()(Const<HavingClause> &c);
    void operator()(Const<OrderByClause> &c);
    void operator()(Const<LimitClause> &c);

    /* Statements */
    void operator()(Const<ErrorStmt> &s);
    void operator()(Const<EmptyStmt> &s);
    void operator()(Const<CreateDatabaseStmt> &s);
    void operator()(Const<UseDatabaseStmt> &s);
    void operator()(Const<CreateTableStmt> &s);
    void operator()(Const<SelectStmt> &s);
    void operator()(Const<InsertStmt> &s);
    void operator()(Const<UpdateStmt> &s);
    void operator()(Const<DeleteStmt> &s);
    void operator()(Const<DSVImportStmt> &s);

    private:
    /** Start a new line with proper indentation. */
    std::ostream & indent() const {
        insist(indent_ >= 0, "Indent must not be negative!  Missing increment or superfluous decrement?");
        if (indent_)
            out << '\n' << std::string(2 * indent_, ' ');
        return out;
    }

    /** Emit the given clause `c` in a dot cluster.
     *
     * @param c the clause to emit
     * @param name the internal name of the clause (must contain no white spaces)
     * @param label the human-readable name of this clause (may contain white spaces)
     * @param color the color to use for highlighting this cluster
     */
    void cluster(Const<Clause> &c, const char *name, const char *label, const char *color);
};

}
