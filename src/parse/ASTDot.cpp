#include "parse/ASTDot.hpp"

#include "catalog/Schema.hpp"
#include <iomanip>
#include <sstream>

#define q(X) '"' << X << '"' // quote
#define id(X) q(std::hex << &X) // convert virtual address to identifier


using namespace db;


namespace {

std::string html_escape(std::string str)
{
    str = replace_all(str, "&", "&amp;");
    str = replace_all(str, "<", "&lt;");
    str = replace_all(str, ">", "&gt;");
    return str;
}

}

ASTDot::ASTDot(std::ostream &out)
    : out(out)
{
    out << GRAPH_TYPE << " ast\n{\n"
        << "forcelabels=true;\n";
}

ASTDot::~ASTDot()
{
    out << '}' << std::endl;
}

/*--- Expressions ----------------------------------------------------------------------------------------------------*/

void ASTDot::operator()(Const<ErrorExpr> &e)
{
    out << std::hex << id(e) << " [label=<<FONT COLOR=\"red\"><B>ErrorExpr</B></FONT>>];\n";
}

void ASTDot::operator()(Const<Designator> &e)
{
    out << std::hex << id(e) << " [label=<<B>";

    if (e.has_table_name()) out << e.table_name.text << ".";
    out << e.attr_name.text << "</B>";
    if (e.has_type()) {
        std::ostringstream oss;
        oss << *e.type();
        out << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>";
    }
    out << ">];\n";

    /* Dot edge to relation. */
    const auto &t = e.target();
    if (auto val = std::get_if<const Attribute*>(&t)) {
        const Attribute &A = **val;
        out << id(e) << EDGE << A.relation.name << ':' << A.name
            << " [style=\"dashed\",dir=\"forward\",color=\"#404040\"];\n";
    } else if (auto val = std::get_if<const Expr*>(&t)) {
        const Expr *expr = *val;
        out << id(e) << EDGE << id(*expr) << " [style=\"dashed\",dir=\"forward\",color=\"#404040\"];\n";
    }
}

void ASTDot::operator()(Const<Constant> &e)
{
    out << id(e) << " [label=<<B>";

    if (e.is_string()) {
        out << html_escape(e.tok.text);
    } else {
        out << e.tok.text;
    }
    out << "</B>";

    if (e.has_type()) {
        std::ostringstream oss;
        oss << *e.type();
        out << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>";
    }

    out << ">];\n";
}

void ASTDot::operator()(Const<FnApplicationExpr> &e)
{
    (*this)(*e.fn);
    out << id(e) << " [label=<()";

    if (e.has_type()) {
        std::ostringstream oss;
        oss << *e.type();
        out << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>";
    }

    out << ">];\n";
    out << id(e) << EDGE << id(*e.fn) << ";\n";

    for (auto arg : e.args) {
        (*this)(*arg);
        out << id(e) << EDGE << id(*arg) << ";\n";
    }
}

void ASTDot::operator()(Const<UnaryExpr> &e)
{
    (*this)(*e.expr);
    out << id(e) << " [label=<" << html_escape(e.op.text);

    if (e.has_type()) {
        std::ostringstream oss;
        oss << *e.type();
        out << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>";
    }

    out << ">];\n"
        << id(e) << EDGE << id(*e.expr) << ";\n";
}

void ASTDot::operator()(Const<BinaryExpr> &e)
{
    (*this)(*e.lhs);
    (*this)(*e.rhs);
    out << id(e) << " [label=<" << html_escape(e.op.text);

    if (e.has_type()) {
        std::ostringstream oss;
        oss << *e.type();
        out << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>";
    }

    out << ">];\n"
        << id(e) << EDGE << id(*e.lhs) << ";\n"
        << id(e) << EDGE << id(*e.rhs) << ";\n";
}

/*--- Clauses --------------------------------------------------------------------------------------------------------*/

void ASTDot::operator()(Const<ErrorClause> &c)
{
    out << id(c) << " [label=\"ErrorClause\"];\n";
}

void ASTDot::operator()(Const<SelectClause> &c)
{
    out << "subgraph cluster_select {\nstyle=\"rounded,filled\";\ncolor=\"#e6194B20\";penwidth=\"4\";\n"
        << id(c) << " [label=\"SELECT\"];\n";

    if (c.select_all) {
        out << q(std::hex << c << '*') << "[label=\"*\"];\n"
            << id(c) << EDGE << q(std::hex << c << '*') << ";\n";
    }
    for (auto &s : c.select) {
        (*this)(*s.first);
        if (s.second) {
            out << id(s.second) << " [label=\"AS " << s.second.text << "\"];\n"
                << id(c) << EDGE << id(s.second) << ";\n"
                << id(s.second) << EDGE << id(*s.first) << ";\n";
        } else {
            out << id(c) << EDGE << id(*s.first) << ";\n";
        }
    }

    out << "}\n";
}

void ASTDot::operator()(Const<FromClause> &c)
{
    out << "subgraph cluster_from {\nstyle=\"rounded,filled\";\ncolor=\"#bfef4550\";penwidth=\"4\";\n"
        << id(c) << " [label=\"FROM\"];\n";

    for (auto &t : c.from) {
        if (auto name = std::get_if<Token>(&t.source)) {
            if (t.alias) {
                out << id(t.alias) << " [label=\"AS " << t.alias.text << "\"];\n"
                    << id (*name) << " [label=\"" << name->text << "\"];\n"
                    << id(c) << EDGE << id(t.alias) << EDGE << id(*name) << ";\n";
            } else {
                out << id(*name) << " [label=\"" << name->text << "\"];\n"
                    << id(c) << EDGE << id(*name) << ";\n";
            }
        } else if (auto stmt = std::get_if<Stmt*>(&t.source)) {
            insist(t.alias, "nested statements must have an alias");
            out << id(t.alias) << " [label=\"AS " << t.alias.text << "\"];\n";
            (*this)(**stmt);
            out << id(c) << EDGE << id(t.alias) << EDGE << id(**stmt) << ";\n";
        } else {
            unreachable("invalid variant");
        }
        if (t.has_relation()) {
            insist(std::holds_alternative<Token>(t.source));
            auto &name = std::get<Token>(t.source);
            auto &R = t.relation();
            out << id(name) << EDGE << R.name << ":n [dir=\"forward\",color=\"#404040\"];\n";
        }
    }

    out << "}\n";
}

void ASTDot::operator()(Const<WhereClause> &c)
{
    out << "subgraph cluster_where {\nstyle=\"rounded,filled\";\ncolor=\"#42d4f430\";penwidth=\"4\";\n"
        << id(c) << " [label=\"WHERE\"];\n";

    (*this)(*c.where);
    out << id(c) << EDGE << id(*c.where) << ";\n";

    out << "}\n";
}

void ASTDot::operator()(Const<GroupByClause> &c)
{
    out << "subgraph cluster_groupby {\nstyle=\"rounded,filled\";\ncolor=\"#3cb44b30\";penwidth=\"4\";\n"
        << id(c) << " [label=\"GROUP BY\"];\n";

    for (auto &g : c.group_by) {
        (*this)(*g);
        out << id(c) << EDGE << id(*g) << ";\n";
    }

    out << "}\n";
}

void ASTDot::operator()(Const<HavingClause> &c)
{
    out << "subgraph cluster_having {\nstyle=\"rounded,filled\";\ncolor=\"#aaffc350\";penwidth=\"4\";\n"
        << id(c) << " [label=\"HAVING\"];\n";

    (*this)(*c.having);
    out << id(c) << EDGE << id(*c.having) << ";\n";

    out << "}\n";
}

void ASTDot::operator()(Const<OrderByClause> &c)
{
    out << "subgraph cluster_orderby {\nstyle=\"rounded,filled\";\ncolor=\"#ffe11950\";penwidth=\"4\";\n"
        << id(c) << " [label=\"ORDER BY\"];\n";

    for (auto &o : c.order_by) {
        if (o.second)
            out << id(o.second) << " [label=\"ASC\"];\n";
        else
            out << id(o.second) << " [label=\"DESC\"];\n";
        out << id(c) << EDGE << id(o.second) << ";\n";

        (*this)(*o.first);
        out << id(o.second) << EDGE << id(*o.first) << ";\n";
    }

    out << "}\n";
}

void ASTDot::operator()(Const<LimitClause> &c)
{
    out << "subgraph cluster_limit {\nstyle=\"rounded,filled\";\ncolor=\"#80800040\";penwidth=\"4\";\n";

    out << id(c) << " [label=\"LIMIT\"];\n";
    out << id(c.limit) << " [label=<<B>" << c.limit.text << "</B>>];\n";
    out << id(c) << EDGE << id(c.limit) << ";\n";

    if (c.offset) {
        out << id(c.offset) << " [label=<OFFSET <B>" << c.offset.text << "</B>>];\n";
        out << id(c) << EDGE << id(c.offset) << ";\n";
    }

    out << "}\n";
}

/*--- Statements -----------------------------------------------------------------------------------------------------*/

void ASTDot::operator()(Const<ErrorStmt> &s)
{
}

void ASTDot::operator()(Const<EmptyStmt> &s)
{
}

void ASTDot::operator()(Const<CreateDatabaseStmt> &s)
{
}

void ASTDot::operator()(Const<UseDatabaseStmt> &s)
{
}

void ASTDot::operator()(Const<CreateTableStmt> &s)
{
}

void ASTDot::operator()(Const<SelectStmt> &s)
{
    out << id(s) << " [label=\"SelectStmt\"];\n";

    /* Dot the accessed relations first. */
    out << "subgraph {\n";
    if (auto f = cast<FromClause>(s.from)) {
        for (auto &t : f->from) {
            if (t.has_relation()) {
                auto &R = t.relation();

                out << R.name << " [shape=none,style=filled,fillcolor=white,label=<\n<TABLE>\n<TR><TD BORDER=\"0\"><B>"
                    << R.name << "</B></TD></TR>\n";

                for (auto &A : R) {
                    std::ostringstream oss;
                    oss << *A.type;
                    out << "<TR><TD PORT=\"" << A.name << "\">" << A.name
                        << "<FONT POINT-SIZE=\"11\"><I> : " << html_escape(oss.str()) << "</I></FONT>"
                        << "</TD></TR>\n";
                }

                out << "</TABLE>\n>];\n";
#if 0
                out << id(s) << EDGE << R.name << " [style=invis];\n";
#endif
            }
        }
    }
    out << "}\n";

    (*this)(*s.select);
    out << id(s) << EDGE << id(*s.select) << ";\n";

    if (s.from) {
        (*this)(*s.from);
        out << id(s) << EDGE << id(*s.from) << ";\n";
    }

    if (s.where) {
        (*this)(*s.where);
        out << id(s) << EDGE << id(*s.where) << ";\n";
    }

    if (s.group_by) {
        (*this)(*s.group_by);
        out << id(s) << EDGE << id(*s.group_by) << ";\n";
    }

    if (s.having) {
        (*this)(*s.having);
        out << id(s) << EDGE << id(*s.having) << ";\n";
    }

    if (s.order_by) {
        (*this)(*s.order_by);
        out << id(s) << EDGE << id(*s.order_by) << ";\n";
    }

    if (s.limit) {
        (*this)(*s.limit);
        out << id(s) << EDGE << id(*s.limit) << ";\n";
    }

#if 0
    /* Order the nodes. */
    out << "{\nrank = same;\nrankdir = LR;\n"
        << id(*s.select);
    if (s.from) out << EDGE << id(*s.from);
    if (s.where) out << EDGE << id(*s.where);
    if (s.group_by) out << EDGE << id(*s.group_by);
    if (s.having) out << EDGE << id(*s.having);
    if (s.order_by) out << EDGE << id(*s.order_by);
    if (s.limit) out << EDGE << id(*s.limit);
    out << " [style=\"invis\"];\n"
        << "}\n";
#endif
}

void ASTDot::operator()(Const<InsertStmt> &s)
{
}

void ASTDot::operator()(Const<UpdateStmt> &s)
{
}

void ASTDot::operator()(Const<DeleteStmt> &s)
{
}

