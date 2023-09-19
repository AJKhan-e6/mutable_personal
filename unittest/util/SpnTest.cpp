#include "catch2/catch.hpp"

#include "catalog/SpnWrapper.hpp"
#include <mutable/mutable.hpp>
#include <mutable/util/Diagnostic.hpp>
#include "util/Spn.hpp"


using namespace m;


TEST_CASE("spn/learning","[core][util][spn]")
{
    Catalog::Clear();
    Catalog &C = Catalog::Get();
    auto &db = C.add_database(C.pool("db"));
    C.set_database_in_use(db);

    std::ostringstream out, err;
    Diagnostic diag(false, out, err);

    SECTION("empty table")
    {
        std::ostringstream oss;
        oss << "CREATE TABLE table ("
            << "id INT(4) PRIMARY KEY,"
            << "column_1 INT(4)"
            << ");";
        auto stmt = statement_from_string(diag, oss.str());
        execute_statement(diag, *stmt);

        auto spn = SpnWrapper::learn_spn_table("db", "table");

        /* Expect a leaf as the root */
        CHECK(spn.height() == 0);
        CHECK(spn.degree() == 0);
        CHECK(spn.breadth() == 1);
    }

    SECTION("product split 1 attribute")
    {
        std::ostringstream oss;
        oss << "CREATE TABLE table ("
            << "id INT(4) PRIMARY KEY,"
            << "column_1 INT(4)"
            << ");";
        auto stmt = statement_from_string(diag, oss.str());
        execute_statement(diag, *stmt);
        auto &table = db.get_table(C.pool("table"));

        std::ostringstream oss_insert;
        oss_insert << "INSERT INTO table VALUES (0, 1);";
        auto insert_stmt = statement_from_string(diag, oss_insert.str());
        execute_statement(diag, *insert_stmt);

        auto spn = SpnWrapper::learn_spn_table("db", "table");

        /* Expect a leaf as the root */
        CHECK(spn.height() == 0);
        CHECK(spn.degree() == 0);
        CHECK(spn.breadth() == 1);
    }

    SECTION("product split 2 attributes")
    {
        std::ostringstream oss;
        oss << "CREATE TABLE table ("
            << "id INT(4) PRIMARY KEY,"
            << "column_1 INT(4),"
            << "column_2 INT(4)"
            << ");";
        auto stmt = statement_from_string(diag, oss.str());
        execute_statement(diag, *stmt);
        auto &table = db.get_table(C.pool("table"));

        std::ostringstream oss_insert;
        oss_insert << "INSERT INTO table VALUES (0, 1, 2);";
        auto insert_stmt = statement_from_string(diag, oss_insert.str());
        execute_statement(diag, *insert_stmt);

        auto spn = SpnWrapper::learn_spn_table("db", "table");

        /* Expect a product node as the root to split the attributes */
        CHECK(spn.height() == 1);
        CHECK(spn.degree() == 2);
        CHECK(spn.breadth() == 2);
    }

    SECTION("product split 3 attributes")
    {
        std::ostringstream oss;
        oss << "CREATE TABLE table ("
            << "id INT(4) PRIMARY KEY,"
            << "column_1 INT(4),"
            << "column_2 INT(4),"
            << "column_3 INT(4)"
            << ");";
        auto stmt = statement_from_string(diag, oss.str());
        execute_statement(diag, *stmt);
        auto &table = db.get_table(C.pool("table"));

        std::ostringstream oss_insert;
        oss_insert << "INSERT INTO table VALUES (0, 1, 2, 3);";
        auto insert_stmt = statement_from_string(diag, oss_insert.str());
        execute_statement(diag, *insert_stmt);

        auto spn = SpnWrapper::learn_spn_table("db", "table");

        /* Expect a product node as the root to split the attributes */
        CHECK(spn.height() == 1);
        CHECK(spn.degree() == 3);
        CHECK(spn.breadth() == 3);
    }

    SECTION("sum cluster 10 rows")
    {
        std::ostringstream oss;
        oss << "CREATE TABLE table ("
            << "id INT(4) PRIMARY KEY,"
            << "column_1 INT(4),"
            << "column_2 INT(4)"
            << ");";
        auto stmt = statement_from_string(diag, oss.str());
        execute_statement(diag, *stmt);
        auto &table = db.get_table(C.pool("table"));

        for (int i = 0; i < 10; i++) {
            std::ostringstream oss_insert;
            if (i < 5) {
                oss_insert << "INSERT INTO table VALUES (" << i << ", " << i+1 << "00, 0);";
            }
            else {
                oss_insert << "INSERT INTO table VALUES (" << i << ", " << i+1 << "000, 1);";
            }
            auto insert_stmt = statement_from_string(diag, oss_insert.str());
            execute_statement(diag, *insert_stmt);
        }

        auto spn = SpnWrapper::learn_spn_table("db", "table");

        /* Expect a sum node as the root to cluster into 2 clusters */
        CHECK(spn.height() == 2);
        CHECK(spn.degree() == 2);
        CHECK(spn.breadth() == 4);
    }
}

TEST_CASE("spn/inference","[core][util][spn]")
{
    Catalog::Clear();
    Catalog &C = Catalog::Get();
    auto &db = C.add_database(C.pool("db"));
    C.set_database_in_use(db);

    std::ostringstream out, err;
    Diagnostic diag(false, out, err);

    std::ostringstream oss;
    oss << "CREATE TABLE table ("
        << "id INT(4) PRIMARY KEY,"
        << "column_1 INT(4),"
        << "column_2 INT(4),"
        << "column_3 INT(4)"
        << ");";
    auto stmt = statement_from_string(diag, oss.str());
    execute_statement(diag, *stmt);
    auto &table = db.get_table(C.pool("table"));

    for (int i = 0; i < 100; i++) {
        std::ostringstream oss_insert;
        oss_insert << "INSERT INTO table VALUES (" << i << ", 1, " << i * 10 << ", " << i << ");";
        auto insert_stmt = statement_from_string(diag, oss_insert.str());
        execute_statement(diag, *insert_stmt);
    }

    std::vector<Spn::LeafType> leaf_types_discrete = {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE};
    auto spn_discrete = SpnWrapper::learn_spn_table("db", "table", leaf_types_discrete);

    std::vector<Spn::LeafType> leaf_types_continuous = {Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS};
    auto spn_continuous = SpnWrapper::learn_spn_table("db", "table", leaf_types_continuous);

    SECTION("EQUAL")
    {
        /* P(column_1 = 1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_1"), std::make_pair(Spn::EQUAL, 1));
        CHECK(spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("LESS")
    {
        /* P(column_2 < 2000) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_2"), std::make_pair(Spn::LESS, 2000));
        CHECK(spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("LESS_EQUAL")
    {
        /* P(column_3 <= 101) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_3"), std::make_pair(Spn::LESS_EQUAL, 101));
        CHECK(spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("GREATER")
    {
        /* P(column_3 > -1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_3"), std::make_pair(Spn::GREATER, -1));
        CHECK(spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("GREATER_EQUAL")
    {
        /* P(column_2 >= 0) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_2"), std::make_pair(Spn::GREATER_EQUAL, 0));
        CHECK(spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("IS_NULL")
    {
        /* P(column_3 being NULL) should be 0 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("column_3"), std::make_pair(Spn::IS_NULL, 0));
        CHECK(spn_discrete.likelihood(filter) <= 0.001f);
        CHECK(spn_continuous.likelihood(filter) <= 0.001f);
    }

    SECTION("EXPECTATION")
    {
        /* E(column_1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        CHECK(spn_discrete.expectation(C.pool("column_1"), filter) == 1.f);
    }
}

// adding comments to see where the error lies idk  
// idk what to do anymore 

TEST_CASE("spn/testing","[core][util][spn]")
{
    Catalog::Clear();
    Catalog &C = Catalog::Get();
    auto &db = C.add_database(C.pool("test"));
    C.set_database_in_use(db);

    std::ostringstream out, err;
    Diagnostic diag(false, out, err);

    std::ostringstream oss;
    oss << "CREATE TABLE Nation ("
        << "n_nationkey INT(4) NOT NULL,"
        << "n_name CHAR(25) NOT NULL,"
        << "n_regionkey INT(4) NOT NULL,"
        << "n_comment VARCHAR(152) NOT NULL"
        << ");";
    auto stmt = statement_from_string(diag, oss.str());
    execute_statement(diag, *stmt);
    auto &table = db.get_table(C.pool("Nation"));

    // Creating second table 
    std::ostringstream r_oss;
    r_oss << "CREATE TABLE Region ("
        << "r_regionkey INT(4) NOT NULL,"
        << "r_name CHAR(25) NOT NULL,"
        << "r_comment VARCHAR(152) NOT NULL"
        << ");";
    auto r_stmt = statement_from_string(diag, r_oss.str());
    execute_statement(diag, *r_stmt);

    std::ostringstream oss_insert;
    oss_insert << "IMPORT INTO Nation DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/unclean_data/nation.tbl\" DELIMITER \"|\";";
    auto insert_stmt = statement_from_string(diag, oss_insert.str());
    execute_statement(diag, *insert_stmt);

    std::ostringstream r_oss_insert;    
    r_oss_insert << "IMPORT INTO Region DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/unclean_data/region.tbl\" DELIMITER \"|\";";
    auto r_insert_stmt = statement_from_string(diag, r_oss_insert.str());
    execute_statement(diag, *r_insert_stmt);
    

    std::vector<Spn::LeafType> leaf_types_discrete = {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE};
    auto spn_discrete = SpnWrapper::learn_spn_table("test", "Nation", leaf_types_discrete);

    std::vector<Spn::LeafType> leaf_types_continuous = {Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS};
    auto spn_continuous = SpnWrapper::learn_spn_table("test", "Nation", leaf_types_continuous);

    // std::vector<Spn::LeafType> r_leaf_types_discrete = {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE};
    auto r_spn_discrete = SpnWrapper::learn_spn_table("test", "Region", leaf_types_discrete);

    // std::vector<Spn::LeafType> r_leaf_types_continuous = {Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS};
    auto r_spn_continuous = SpnWrapper::learn_spn_table("test", "Region", leaf_types_continuous);

    // Learning spn on the database 
    std::unordered_map<const char*, std::vector<Spn::LeafType>> leafTypesMap_disc = {
        {"Nation", {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE}},
        {"Region", {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE}}
        };
    auto spn_db_discrete = SpnWrapper::learn_spn_database("test", leafTypesMap_disc);


    std::unordered_map<const char*, std::vector<Spn::LeafType>> leafTypesMap_cont = {
        {"Nation", {Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS}},
        {"Region", {Spn::CONTINUOUS, Spn::CONTINUOUS, Spn::CONTINUOUS}}
        };
    auto spn_db_continuous = SpnWrapper::learn_spn_database("test", leafTypesMap_cont); 

    SECTION("EQUAL")
    {
        /* P(column_1 = 1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::EQUAL, 1));
        CHECK(spn_discrete.likelihood(filter) == 0.04f);
        CHECK(spn_continuous.likelihood(filter) == 0.04f);
    }

    SECTION("EQUAL_R")
    {
        /* P(column_1 = 1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::EQUAL, 1));
        CHECK(r_spn_discrete.likelihood(filter) == 0.20f);
        CHECK(r_spn_continuous.likelihood(filter) == 0.20f);
    }

    SECTION("EQUAL_DB")
{
    /* P(column_1 = 1) should be 1 */
    std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
    filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::EQUAL, 1));
    const char* tableName = "Region";

    // Use the table name as the key to look up the SpnWrapper
    auto it_discrete = spn_db_discrete.find(tableName);
    auto it_continuous = spn_db_continuous.find(tableName);

    // Check if the table name exists in the map for discrete Spns
    if (it_discrete != spn_db_discrete.end()) {
        // Access the SpnWrapper using the iterator
        auto spn = it_discrete->second;
        CHECK(spn->likelihood(filter) == 0.20f);
        delete spn; // Delete the SpnWrapper
        spn_db_discrete.erase(it_discrete); // Remove from the map
    } else {
        std::cout << "Table " << tableName << " not found in discrete spns." << std::endl;
    }

    // Check if the table name exists in the map for continuous Spns
    if (it_continuous != spn_db_continuous.end()) {
        // Access the SpnWrapper using the iterator
        auto spn = it_continuous->second;
        CHECK(spn->likelihood(filter) == 0.20f);
        delete spn; // Delete the SpnWrapper
        spn_db_continuous.erase(it_continuous); // Remove from the map
    } else {
        std::cout << "Table " << tableName << " not found in continuous spns." << std::endl;
    }

    spn_db_continuous.clear();
    spn_db_discrete.clear();
}


    SECTION("LESS")
    {
        /* P(column_2 < 2000) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::LESS, 5));
        CHECK(spn_discrete.likelihood(filter) >= 0.15f);
        CHECK(spn_continuous.likelihood(filter) >= 0.15f);
    }

    SECTION("LESS_R")
    {
        /* P(column_2 < 2000) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::LESS, 4));
        CHECK(r_spn_discrete.likelihood(filter) >= 0.60f);
        CHECK(r_spn_continuous.likelihood(filter) >= 0.60f);
    }

    SECTION("LESS_EQUAL")
    {
        /* P(column_3 <= 101) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::LESS_EQUAL, 5));
        CHECK(spn_discrete.likelihood(filter) >= 0.24f);
        CHECK(spn_continuous.likelihood(filter) >= 0.24f);
    }

    SECTION("LESS_EQUAL_R")
    {
        /* P(column_3 <= 101) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::LESS_EQUAL, 5));
        CHECK(r_spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(r_spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("GREATER")
    {
        /* P(column_3 > -1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::GREATER, 1));
        CHECK(spn_discrete.likelihood(filter) >= 0.90f);
        CHECK(spn_continuous.likelihood(filter) >= 0.90f);
    }

    SECTION("GREATER_R")
    {
        /* P(column_3 > -1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::GREATER, 1));
        CHECK(r_spn_discrete.likelihood(filter) >= 0.60f);
        CHECK(r_spn_continuous.likelihood(filter) >= 0.60f);
    }

    SECTION("GREATER_EQUAL")
    {
        /* P(column_2 >= 0) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::GREATER_EQUAL, 1));
        CHECK(spn_discrete.likelihood(filter) >= 0.95f);
        CHECK(spn_continuous.likelihood(filter) >= 0.95f);
    }

    SECTION("GREATER_EQUAL_R")
    {
        /* P(column_2 >= 0) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::GREATER_EQUAL, 0));
        CHECK(r_spn_discrete.likelihood(filter) >= 0.999f);
        CHECK(r_spn_continuous.likelihood(filter) >= 0.999f);
    }

    SECTION("IS_NULL")
    {
        /* P(column_3 being NULL) should be 0 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("n_nationkey"), std::make_pair(Spn::IS_NULL, 0));
        CHECK(spn_discrete.likelihood(filter) <= 0.001f);
        CHECK(spn_continuous.likelihood(filter) <= 0.001f);
    }

    SECTION("IS_NULL_R")
    {
        /* P(column_3 being NULL) should be 0 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        filter.emplace(C.pool("r_regionkey"), std::make_pair(Spn::IS_NULL, 0));
        CHECK(r_spn_discrete.likelihood(filter) <= 0.001f);
        CHECK(r_spn_continuous.likelihood(filter) <= 0.001f);
    }

    SECTION("EXPECTATION")
    {
        /* E(column_1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        CHECK(spn_discrete.expectation(C.pool("n_nationkey"), filter) == 1.f);
    }

    SECTION("EXPECTATION_R")
    {
        /* E(column_1) should be 1 */
        std::unordered_map<const char*, std::pair<Spn::SpnOperator, float>> filter;
        CHECK(r_spn_discrete.expectation(C.pool("r_regionkey"), filter) == 1.f);
    }
}
