#include "parse/ASTDumper.hpp"

#include "catalog/Schema.hpp"


using namespace db;


/*===== Expr =========================================================================================================*/

void ASTDumper::operator()(Const<ErrorExpr> &e)
{
    indent() << "ErrorExpr '" << e.tok.text << "' (" << e.tok.pos << ')';
}

void ASTDumper::operator()(Const<Designator> &e)
{
    if (e.has_table_name()) {
        indent() << "Designator\n";
        ++indent_;
        indent() << "table name: '" << e.table_name.text << "' (" << e.table_name.pos << ')';
        indent() << "attribute name: '" << e.attr_name.text << "' (" << e.attr_name.pos << ')';
        --indent_;
    } else {
        indent() << "identifier: '" << e.attr_name.text << "' (" << e.attr_name.pos << ')';
    }
}

void ASTDumper::operator()(Const<Constant> &e)
{
    indent() << "Constant: " << e.tok.text << " (" << e.tok.pos << ')';
}

void ASTDumper::operator()(Const<FnApplicationExpr> &e)
{
    indent() << "FnApplicationExpr";
    ++indent_;
    (*this)(*e.fn);
    indent() << "args";
    ++indent_;
    for (auto expr : e.args)
        (*this)(*expr);
    --indent_;
    --indent_;
}

void ASTDumper::operator()(Const<UnaryExpr> &e)
{
    indent() << "UnaryExpr: '" << e.op.text << "' (" << e.op.pos << ')';
    ++indent_;
    (*this)(*e.expr);
    --indent_;
}

void ASTDumper::operator()(Const<BinaryExpr> &e)
{
    indent() << "BinaryExpr: '" << e.op.text << "' (" << e.op.pos << ')';
    ++indent_;
    (*this)(*e.lhs);
    (*this)(*e.rhs);
    --indent_;
}

/*===== Clause =======================================================================================================*/

void ASTDumper::operator()(Const<ErrorClause> &c)
{
    indent() << "ErrorClause: '" << c.tok.text << "' (" << c.tok.pos << ')';
}

void ASTDumper::operator()(Const<SelectClause> &c)
{
    indent() << "SelectClause (" << c.tok.pos << ')';
    ++indent_;
    if (c.select_all)
        indent() << "select_all = TRUE";
    for (auto s : c.select) {
        if (s.second) {
            indent() << "AS '" << s.second.text << "' (" << s.second.pos << ')';
            ++indent_;
            (*this)(*s.first);
            --indent_;
        } else {
            (*this)(*s.first);
        }
    }
    --indent_;
}

void ASTDumper::operator()(Const<FromClause> &c)
{
    indent() << "FromClause (" << c.tok.pos << ')';
    ++indent_;
    for (auto f : c.from) {
        if (f.second) {
            indent() << "AS '" << f.second.text << "' (" << f.second.pos << ')';
            ++indent_;
            indent() << f.first.text << " (" << f.first.pos << ')';
            --indent_;
        } else {
            indent() << f.first.text << " (" << f.first.pos << ')';
        }
    }
    --indent_;
}

void ASTDumper::operator()(Const<WhereClause> &c)
{
    indent() << "WhereClause (" << c.tok.pos << ')';
    ++indent_;
    (*this)(*c.where);
    --indent_;
}


void ASTDumper::operator()(Const<GroupByClause> &c)
{
    indent() << "GroupByClause (" << c.tok.pos << ')';
    ++indent_;
    for (auto g : c.group_by)
        (*this)(*g);
    --indent_;
}

void ASTDumper::operator()(Const<HavingClause> &c)
{
    indent() << "HavingClause (" << c.tok.pos << ')';
    ++indent_;
    (*this)(*c.having);
    --indent_;
}

void ASTDumper::operator()(Const<OrderByClause> &c)
{
    indent() << "OrderByClause (" << c.tok.pos << ')';
    ++indent_;
    for (auto o : c.order_by) {
        indent() << (o.second ? "ASC" : "DESC");
        ++indent_;
        (*this)(*o.first);
        --indent_;
    }
    --indent_;
}

void ASTDumper::operator()(Const<LimitClause> &c)
{
    indent() << "LimitClause (" << c.tok.pos << ')';
    ++indent_;

    indent() << "LIMIT " << c.limit.text << " (" << c.limit.pos << ')';

    if (c.offset)
        indent() << "OFFSET " << c.offset.text << " (" << c.offset.pos << ')';

    --indent_;
}

/*===== Stmt =========================================================================================================*/

void ASTDumper::operator()(Const<ErrorStmt> &s)
{
    indent() << "ErrorStmt: '" << s.tok.text << "' (" << s.tok.pos << ')';
}

void ASTDumper::operator()(Const<EmptyStmt> &s)
{
    indent() << "EmptyStmt: '" << s.tok.text << "' (" << s.tok.pos << ')';
}

void ASTDumper::operator()(Const<CreateDatabaseStmt> &s)
{
    indent() << "CreateDatabaseStmt: '" << s.database_name.text << "' (" << s.database_name.pos << ')';
}

void ASTDumper::operator()(Const<UseDatabaseStmt> &s)
{
    indent() << "UseDatabaseStmt: '" << s.database_name.text << "' (" << s.database_name.pos << ')';
}

void ASTDumper::operator()(Const<CreateTableStmt> &s)
{
    indent() << "CreateTableStmt: table " << s.table_name.text << " (" << s.table_name.pos << ')';
    ++indent_;
    indent() << "attributes";
    ++indent_;
    for (auto &attr : s.attributes)
        indent() << attr.first.text << " : " << *attr.second << " (" << attr.first.pos << ")";
    --indent_;
    --indent_;
}

void ASTDumper::operator()(Const<SelectStmt> &s)
{
    indent() << "SelectStmt";
    ++indent_;

    (*this)(*s.select);
    (*this)(*s.from);

    if (s.where) (*this)(*s.where);
    if (s.group_by) (*this)(*s.group_by);
    if (s.having) (*this)(*s.having);
    if (s.order_by) (*this)(*s.order_by);
    if (s.limit) (*this)(*s.limit);

    --indent_;
}

void ASTDumper::operator()(Const<InsertStmt> &s)
{
    indent() << "InsertStmt: table " << s.table_name.text << " (" << s.table_name.pos << ')';
    ++indent_;
    indent() << "values";
    ++indent_;
    for (std::size_t idx = 0, end = s.values.size(); idx != end; ++idx) {
        indent() << '[' << idx << ']';
        const InsertStmt::value_type &v = s.values[idx];
        ++indent_;
        for (auto &e : v) {
            switch (e.first) {
                case InsertStmt::I_Default:
                    indent() << "DEFAULT";
                    break;

                case InsertStmt::I_Null:
                    indent() << "NULL";
                    break;

                case InsertStmt::I_Expr:
                    (*this)(*e.second);
                    break;
            }
        }
        --indent_;
    }
    --indent_;
    --indent_;
}

void ASTDumper::operator()(Const<UpdateStmt> &s)
{
    indent() << "UpdateStmt: table " << s.table_name.text << " (" << s.table_name.pos << ')';
    ++indent_;
    indent() << "set";
    ++indent_;
    for (auto s : s.set) {
        indent() << s.first.text << " (" << s.first.pos << ')';
        ++indent_;
        (*this)(*s.second);
        --indent_;
    }
    --indent_;

    if (s.where) (*this)(*s.where);

    --indent_;
}

void ASTDumper::operator()(Const<DeleteStmt> &s)
{
    indent() << "DeleteStmt: table " << s.table_name.text << " (" << s.table_name.pos << ')';

    if (s.where) {
        ++indent_;
        (*this)(*s.where);
        --indent_;
    }
}
