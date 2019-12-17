#include "catch.hpp"

#include "catalog/Schema.hpp"
#include "util/fn.hpp"
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>


using namespace db;


namespace {

std::string get_unique_id()
{
    static unsigned id = 0;
    return std::to_string(id++);
}

}

TEST_CASE("Table c'tor")
{
    Table r("mytable");

    CHECK(streq(r.name, "mytable"));
    CHECK(r.size() == 0);
}

TEST_CASE("Table empty access")
{
    Table r("mytable");

    REQUIRE_THROWS_AS(r[42].id, std::out_of_range);
    REQUIRE_THROWS_AS(r["attribute"].id, std::out_of_range);

    for (auto it = r.cbegin(), end = r.cend(); it != end; ++it)
        REQUIRE(((void) "this code must be dead or the table is not empty", false));
}

TEST_CASE("Table::push_back()")
{
    Table r("mytable");

    const PrimitiveType *i4 = Type::Get_Integer(Type::TY_Vector, 4);
    const PrimitiveType *vc = Type::Get_Varchar(Type::TY_Vector, 42);
    const PrimitiveType *b = Type::Get_Boolean(Type::TY_Vector);

    r.push_back("n", i4);
    r.push_back("comment", vc);
    r.push_back("condition", b);

    REQUIRE(r.size() == 3);

    auto &attr = r[1];
    REQUIRE(&attr == &r[attr.id]);
    REQUIRE(&attr.table == &r);
    REQUIRE(attr.type == vc);
    REQUIRE(streq(attr.name, "comment"));
}

TEST_CASE("Table iterators")
{
    Table r("mytable");
    const PrimitiveType *i4 = Type::Get_Integer(Type::TY_Vector, 4);

    r.push_back("a", i4);
    r.push_back("b", i4);
    r.push_back("c", i4);
    REQUIRE(r.size() == 3);

    auto it = r.cbegin();
    REQUIRE(streq(it->name, "a"));
    ++it;
    REQUIRE(streq(it->name, "b"));
    ++it;
    REQUIRE(streq(it->name, "c"));
    ++it;
    REQUIRE(it == r.cend());
}

TEST_CASE("Table get attribute by name")
{
    Table r("mytable");
    const PrimitiveType *i4 = Type::Get_Integer(Type::TY_Vector, 4);

    r.push_back("a", i4);
    r.push_back("b", i4);
    r.push_back("c", i4);
    REQUIRE(r.size() == 3);

    {
        auto &attr = r["a"];
        REQUIRE(streq(attr.name, "a"));
    {
        auto &attr = r["b"];
        REQUIRE(streq(attr.name, "b"));
    }
    {
        auto &attr = r["c"];
        REQUIRE(streq(attr.name, "c"));
    }
    }
}

TEST_CASE("Table::push_back() duplicate name")
{
    Table r("mytable");
    const PrimitiveType *i4 = Type::Get_Integer(Type::TY_Vector, 4);

    const char *attr_name = "a";

    r.push_back(attr_name, i4);
    r.push_back(attr_name, i4); // OK
}

TEST_CASE("Catalog singleton c'tor")
{
    Catalog &C = Catalog::Get();
    Catalog &C2 = Catalog::Get();
    REQUIRE(&C == &C2);
    Catalog::Clear();
}

TEST_CASE("Catalog Database creation")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();
    Database &D = C.add_database(db_name.c_str());
    Database &D2 = C.get_database(db_name.c_str());
    REQUIRE(&D == &D2);
    REQUIRE(streq(D.name, db_name.c_str()));
    Catalog::Clear();
}

TEST_CASE("Catalog::drop_database() by name")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();
    C.add_database(db_name.c_str());

    REQUIRE_NOTHROW(C.get_database(db_name.c_str()));
    REQUIRE_NOTHROW(C.drop_database(db_name.c_str())); // ok
    CHECK_THROWS_AS(C.get_database(db_name.c_str()), std::out_of_range); // already deleted
    REQUIRE_THROWS_AS(C.drop_database("nodb"), std::invalid_argument); // does not exist
    Catalog::Clear();
}

TEST_CASE("Catalog::drop_database() by reference")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();
    Database &D = C.add_database(db_name.c_str());

    C.set_database_in_use(D);
    REQUIRE_THROWS_AS(C.drop_database(db_name.c_str()), std::invalid_argument); // db in use

    C.unset_database_in_use();
    REQUIRE_NOTHROW(C.get_database(db_name.c_str())); // ok
    REQUIRE_NOTHROW(C.drop_database(D)); // ok
    CHECK_THROWS_AS(C.get_database(db_name.c_str()), std::out_of_range); // not found
    Catalog::Clear();
}

TEST_CASE("Catalog use database")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();

    C.unset_database_in_use();
    Database &D = C.add_database(db_name.c_str());
    REQUIRE(not C.has_database_in_use());
    C.set_database_in_use(D);
    REQUIRE(C.has_database_in_use());
    auto &in_use = C.get_database_in_use();
    REQUIRE(&D == &in_use);
    C.unset_database_in_use();
    REQUIRE(not C.has_database_in_use());
    Catalog::Clear();
}

TEST_CASE("Database c'tor")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();
    Database &D = C.add_database(db_name.c_str());
    REQUIRE(D.size() == 0);
    Catalog::Clear();
}

TEST_CASE("Database/add table error if name already taken")
{
    Catalog &C = Catalog::Get();
    std::string db_name = get_unique_id();
    Database &D = C.add_database(db_name.c_str());

    const char *tbl_name = "mytable";
    D.add_table(tbl_name);
    Table *R = new Table(tbl_name);
    REQUIRE_THROWS_AS(D.add(R), std::invalid_argument);
    delete R;
    Catalog::Clear();
}
