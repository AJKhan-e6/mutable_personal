#include "catalog/Schema.hpp"

#include "util/fn.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>


using namespace db;


/*======================================================================================================================
 * Attribute
 *====================================================================================================================*/

void Attribute::dump(std::ostream &out) const
{
    out << "Attribute `" << table.name << "`.`" << name << "`, "
        << "id " << id << ", "
        << "type " << *type
        << std::endl;
}

void Attribute::dump() const { dump(std::cerr); }

/*======================================================================================================================
 * Table
 *====================================================================================================================*/

Table::~Table() { }

void Table::push_back(const PrimitiveType *type, const char *name)
{
    if (name_to_attr_.count(name)) throw std::invalid_argument("attribute with that name already exists");
    name_to_attr_.emplace(name, attrs_.size());
    attrs_.emplace_back(Attribute(attrs_.size(), *this, type, name));
}

void Table::dump(std::ostream &out) const
{
    out << "Table `" << name << '`';
    for (const auto &attr : attrs_)
        out << "\n` " << attr.id << ": `" << attr.name << "` " << *attr.type;
    out << std::endl;
}

void Table::dump() const { dump(std::cerr); }

/*======================================================================================================================
 * Function
 *====================================================================================================================*/

constexpr const char * Function::FNID_TO_STR_[];
constexpr const char * Function::KIND_TO_STR_[];

void Function::dump(std::ostream &out) const
{
    out << "Function{ name = \"" << name << "\", fnid = " << FNID_TO_STR_[fnid] << ", kind = " << KIND_TO_STR_[kind]
        << "}" << std::endl;
}

/*======================================================================================================================
 * Database
 *====================================================================================================================*/

Database::Database(const char *name)
    : name(name)
{
}

Database::~Database()
{
    for (auto &r : tables_)
        delete r.second;
}

/*======================================================================================================================
 * Catalog
 *====================================================================================================================*/

Catalog::Catalog()
{
    /* Initialize standard functions. */
#define DB_FUNCTION(NAME, KIND) { \
    auto name = pool(#NAME); \
    auto res = standard_functions_.emplace(name, new Function(name, Function::FN_ ## NAME, Function::KIND)); \
    insist(res.second, "function already defined"); \
}
#include "tables/Functions.tbl"
#undef DB_FUNCTION
}

Catalog::~Catalog()
{
    for (auto &s : databases_)
        delete s.second;
    for (auto fn : standard_functions_)
        delete fn.second;
}
