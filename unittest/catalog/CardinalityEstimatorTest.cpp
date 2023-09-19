#include "catch2/catch.hpp"

#include "catalog/SpnWrapper.hpp"
#include "parse/Parser.hpp"
#include "parse/Sema.hpp"
#include <cstring>
#include <mutable/catalog/CardinalityEstimator.hpp>
#include <mutable/catalog/Catalog.hpp>
#include <mutable/mutable.hpp>
#include <mutable/util/ADT.hpp>
#include <sstream>
#include "util/Spn.hpp"


#include <iostream>


using namespace m;
using namespace m::ast;


TEST_CASE("Injection estimator estimates", "[core][catalog][cardinality]")
{
    using Subproblem = SmallBitset;
    /* Get Catalog and create new database to use for unit testing. */
    Catalog::Clear();
    Catalog &Cat = Catalog::Get();
    auto &db = Cat.add_database("db");
    Cat.set_database_in_use(db);

    std::ostringstream out, err;
    Diagnostic diag(false, out, err);

    /* Create pooled strings. */
    const char *str_A = Cat.pool("A");
    const char *str_B = Cat.pool("B");
    const char *str_C = Cat.pool("C");

    const char *col_id = Cat.pool("id");
    const char *col_aid = Cat.pool("aid");

    /* Create tables. */
    Table &tbl_A = db.add_table(str_A);
    Table &tbl_B = db.add_table(str_B);
    Table &tbl_C = db.add_table(str_C);

    /* Add columns to tables. */
    tbl_A.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_B.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_B.push_back(col_aid, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_C.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_C.push_back(col_aid, Type::Get_Integer(Type::TY_Vector, 4));

    /* Add data to tables. */
    std::size_t num_rows_A = 5;
    std::size_t num_rows_B = 10;
    std::size_t num_rows_C = 8;
    tbl_A.store(Cat.create_store(tbl_A));
    tbl_B.store(Cat.create_store(tbl_B));
    tbl_C.store(Cat.create_store(tbl_C));
    tbl_A.layout(Cat.data_layout());
    tbl_B.layout(Cat.data_layout());
    tbl_C.layout(Cat.data_layout());
    for (std::size_t i = 0; i < num_rows_A; ++i) { tbl_A.store().append(); }
    for (std::size_t i = 0; i < num_rows_B; ++i) { tbl_B.store().append(); }
    for (std::size_t i = 0; i < num_rows_C; ++i) { tbl_C.store().append(); }

    /* Define query:
     *
     * A -- B -- C
     */
    const char *query = "SELECT * \
                         FROM A, B, C \
                         WHERE A.id = C.aid AND A.id = B.aid;";
    auto S = m::statement_from_string(diag, query);
    M_insist(diag.num_errors() == 0);
    auto G = QueryGraph::Build(*S);
    std::istringstream json_input;
    json_input.str("{ \"mine\": [ \
                   {\"relations\": [\"A\"], \"size\":500}, \
                   {\"relations\": [\"A\", \"B\"], \"size\":1000} \
                   ]}");
    InjectionCardinalityEstimator ICE(diag, "mine", json_input);

    //Always check if "A" == 500 because it is in the input_json
    //Always check if "B" == 10 because it is not in the input_json

    SECTION("estimate_scan")
    {
        auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
        CHECK(ICE.predict_cardinality(*existing_entry_model) == 500);
        auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
        CHECK(ICE.predict_cardinality(*non_existing_entry_model) == 10);
    }

    SECTION("estimate_filter")
    {
        cnf::CNF filter;

        auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
        auto filter_existing_entry_model = ICE.estimate_filter(*G, *existing_entry_model, filter);
        CHECK(ICE.predict_cardinality(*filter_existing_entry_model) == 500);

        auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
        auto filter_non_existing_entry_model = ICE.estimate_filter(*G, *non_existing_entry_model, filter);
        CHECK(ICE.predict_cardinality(*filter_non_existing_entry_model) == 10);
    }

    SECTION("estimate_limit")
    {
        auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
        auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
        auto limit_existing_entry_model_high = ICE.estimate_limit(*G, *existing_entry_model, 5000, 0);
        auto limit_non_existing_entry_model_high = ICE.estimate_limit(*G, *non_existing_entry_model, 5000, 0);
        CHECK(ICE.predict_cardinality(*limit_existing_entry_model_high) == 500);
        CHECK(ICE.predict_cardinality(*limit_non_existing_entry_model_high) == 10);

        auto limit_existing_entry_model_low = ICE.estimate_limit(*G, *existing_entry_model, 8, 0);
        auto limit_non_existing_entry_model_low = ICE.estimate_limit(*G, *non_existing_entry_model, 8, 0);
        CHECK(ICE.predict_cardinality(*limit_existing_entry_model_low) == 8);
        CHECK(ICE.predict_cardinality(*limit_non_existing_entry_model_low) == 8);
    }

    SECTION("estimate_grouping (empty)")
    {
        auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
        auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
        std::vector<QueryGraph::group_type> group_by;
        auto grouping_existing_entry_model = ICE.estimate_grouping(*G, *existing_entry_model, group_by);
        auto grouping_non_existing_entry_model = ICE.estimate_grouping(*G, *non_existing_entry_model, group_by);
        CHECK(ICE.predict_cardinality(*grouping_existing_entry_model) == 1);
        CHECK(ICE.predict_cardinality(*grouping_non_existing_entry_model) == 1);
    }

    SECTION("estimate_join")
    {
        auto existing_entry_model_one = ICE.estimate_scan(*G, Subproblem(1UL));
        auto non_existing_entry_model_two = ICE.estimate_scan(*G, Subproblem(1UL << 1));
        auto non_existing_entry_model_three = ICE.estimate_scan(*G, Subproblem(1UL << 2));
        cnf::CNF condition;
        auto existing_entry_model_join = ICE.estimate_join(*G, *existing_entry_model_one, *non_existing_entry_model_two,
                                                      condition);
        auto non_existing_entry_model_join = ICE.estimate_join(*G, *existing_entry_model_one,
                                                               *non_existing_entry_model_three, condition);
        CHECK(ICE.predict_cardinality(*existing_entry_model_join) == 1000);
        CHECK(ICE.predict_cardinality(*non_existing_entry_model_join) == 4000);
    }

    SECTION("wrong database, return cartesian")
    {
        std::istringstream json_input_wrong_db;
        json_input_wrong_db.str("{ \"mine\": [{\"relations\": [\"A\", \"B\"], \"size\":1000}]}");
        InjectionCardinalityEstimator ice_wrong_db(diag, "yours", json_input_wrong_db);

        auto non_existing_entry_model_one = ice_wrong_db.estimate_scan(*G, Subproblem(1UL));
        auto non_existing_entry_model_two = ice_wrong_db.estimate_scan(*G, Subproblem(1UL << 1));
        cnf::CNF condition;
        auto non_existing_entry_model_join = ice_wrong_db.estimate_join(*G, *non_existing_entry_model_one,
                                                                        *non_existing_entry_model_two, condition);
        CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_one) == 5);
        CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_two) == 10);
        CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_join) == 50);
    }

}

TEST_CASE("Cartesian estimator estimates", "[core][catalog][cardinality]")
{
    using Subproblem = SmallBitset;
    /* Get Catalog and create new database to use for unit testing. */
    Catalog::Clear();
    Catalog &Cat = Catalog::Get();
    auto &db = Cat.add_database("db");
    Cat.set_database_in_use(db);

    std::ostringstream out, err;
    Diagnostic diag(false, out, err);

    /* Create pooled strings. */
    const char *str_A = Cat.pool("A");
    const char *str_B = Cat.pool("B");
    const char *str_C = Cat.pool("C");

    const char *col_id = Cat.pool("id");
    const char *col_aid = Cat.pool("aid");

    /* Create tables. */
    Table &tbl_A = db.add_table(str_A);
    Table &tbl_B = db.add_table(str_B);
    Table &tbl_C = db.add_table(str_C);

    /* Add columns to tables. */
    tbl_A.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_B.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_B.push_back(col_aid, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_C.push_back(col_id, Type::Get_Integer(Type::TY_Vector, 4));
    tbl_C.push_back(col_aid, Type::Get_Integer(Type::TY_Vector, 4));

    /* Add data to tables. */
    std::size_t num_rows_A = 5;
    std::size_t num_rows_B = 10;
    std::size_t num_rows_C = 8;
    tbl_A.store(Cat.create_store(tbl_A));
    tbl_B.store(Cat.create_store(tbl_B));
    tbl_C.store(Cat.create_store(tbl_C));
    tbl_A.layout(Cat.data_layout());
    tbl_B.layout(Cat.data_layout());
    tbl_C.layout(Cat.data_layout());
    for (std::size_t i = 0; i < num_rows_A; ++i) { tbl_A.store().append(); }
    for (std::size_t i = 0; i < num_rows_B; ++i) { tbl_B.store().append(); }
    for (std::size_t i = 0; i < num_rows_C; ++i) { tbl_C.store().append(); }

    /* Define query:
     *
     * A -- B -- C
     */
    const char *query = "SELECT * \
                         FROM A, B, C \
                         WHERE A.id = C.aid AND A.id = B.aid;";
    auto S = m::statement_from_string(diag, query);
    M_insist(diag.num_errors() == 0);
    auto G = QueryGraph::Build(*S);
    CartesianProductEstimator CE;

    SECTION("estimate_scan")
    {
        auto scan_model_one = CE.estimate_scan(*G, Subproblem(1UL));
        auto scan_model_two = CE.estimate_scan(*G, Subproblem(1UL << 1));
        auto scan_model_three = CE.estimate_scan(*G, Subproblem(1UL << 2));
        CHECK(CE.predict_cardinality(*scan_model_one) == 5);
        CHECK(CE.predict_cardinality(*scan_model_two) == 10);
        CHECK(CE.predict_cardinality(*scan_model_three) == 8);
    }

    SECTION("estimate_filter")
    {
        auto scan_model = CE.estimate_scan(*G, Subproblem(1UL));
        cnf::CNF filter;

        std::cout << "Filter for cartesian estimator is: " << filter << std::endl;
        auto filter_model = CE.estimate_filter(*G, *scan_model, filter);
        CHECK(CE.predict_cardinality(*filter_model) == 5);
    }

    SECTION("estimate_limit")
    {
        auto scan_model_one = CE.estimate_scan(*G, Subproblem(1UL));
        auto scan_model_two = CE.estimate_scan(*G, Subproblem(1UL << 1));
        auto limit_model_one_high = CE.estimate_limit(*G, *scan_model_one, 5000, 0);
        auto limit_model_two_high = CE.estimate_limit(*G, *scan_model_two, 5000, 0);
        CHECK(CE.predict_cardinality(*limit_model_one_high) == 5);
        CHECK(CE.predict_cardinality(*limit_model_two_high) == 10);

        auto limit_model_one_low = CE.estimate_limit(*G, *scan_model_one, 3, 0);
        auto limit_model_two_low = CE.estimate_limit(*G, *scan_model_two, 3, 0);
        CHECK(CE.predict_cardinality(*limit_model_one_low) == 3);
        CHECK(CE.predict_cardinality(*limit_model_two_low) == 3);
    }

    SECTION("estimate_grouping")
    {
        auto scan_model = CE.estimate_scan(*G, Subproblem(1UL));
        std::vector<QueryGraph::group_type> group_by;
        auto grouping_model = CE.estimate_grouping(*G, *scan_model, group_by);
        CHECK(CE.predict_cardinality(*grouping_model) == 5);
    }

    SECTION("estimate_join")
    {
        auto scan_model_one = CE.estimate_scan(*G, Subproblem(1UL));
        auto scan_model_two = CE.estimate_scan(*G, Subproblem(1UL << 1));
        cnf::CNF condition;
        auto join_model = CE.estimate_join(*G, *scan_model_one, *scan_model_two, condition);
        CHECK(CE.predict_cardinality(*join_model) == 50);
    }
}


TEST_CASE("SPN estimator estimates", "[core][catalog][cardinality]")
{
    using Subproblem = SmallBitset;
    /* Get Catalog and create new database to use for unit testing. */
    Catalog::Clear();
    Catalog &C = Catalog::Get();
    auto &db = C.add_database("test");
    C.set_database_in_use(db);

    std::cout << "Database size is: " << db.size() <<std::endl;


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
    // creating second table 
    std::ostringstream s_oss;
    s_oss << "CREATE TABLE Supplier ("
        << "s_suppkey INT(4) NOT NULL,"
        << "s_name CHAR(25) NOT NULL,"
        << "s_address VARCHAR(40) NOT NULL,"
        << "s_nationkey INT(4) NOT NULL,"
        << "s_phone CHAR(15) NOT NULL,"
        << "s_acctbal DECIMAL(10,2) NOT NULL,"
        << "s_comment VARCHAR(101) NOT NULL"
        << ");";
    auto s_stmt = statement_from_string(diag, s_oss.str());
    execute_statement(diag, *s_stmt);

    // Creating third table 
    std::ostringstream r_oss;
    r_oss << "CREATE TABLE Region ("
        << "r_regionkey INT(4) NOT NULL,"
        << "r_name CHAR(25) NOT NULL,"
        << "r_comment VARCHAR(152) NOT NULL"
        << ");";
    auto r_stmt = statement_from_string(diag, r_oss.str());
    execute_statement(diag, *r_stmt);

    // creating fourth table 
    std::ostringstream ps_oss;
    ps_oss << "CREATE TABLE Partsupp ("
        << "ps_partkey INT(4) NOT NULL,"
        << "ps_suppkey INT(4) NOT NULL,"
        << "ps_availqty INT(4) NOT NULL,"
        << "ps_supplycost DECIMAL(10,2) NOT NULL,"
        << "ps_comment VARCHAR(199) NOT NULL"
        << ");";
    auto ps_stmt = statement_from_string(diag, ps_oss.str());
    execute_statement(diag, *ps_stmt);
    



    std::cout << "Database size now is: " << db.size() <<std::endl;


    std::ostringstream oss_insert;
    oss_insert << "IMPORT INTO Nation DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/unclean_data/nation.tbl\" DELIMITER \"|\";";
    auto insert_stmt = statement_from_string(diag, oss_insert.str());
    execute_statement(diag, *insert_stmt);

    std::ostringstream r_oss_insert;    
    r_oss_insert << "IMPORT INTO Region DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/unclean_data/region.tbl\" DELIMITER \"|\";";
    auto r_insert_stmt = statement_from_string(diag, r_oss_insert.str());
    execute_statement(diag, *r_insert_stmt);

    std::ostringstream s_oss_insert;    
    s_oss_insert << "IMPORT INTO Supplier DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/supplier.tbl\" DELIMITER \"|\";";
    auto s_insert_stmt = statement_from_string(diag, s_oss_insert.str());
    execute_statement(diag, *s_insert_stmt);

    std::ostringstream ps_oss_insert;    
    ps_oss_insert << "IMPORT INTO Partsupp DSV \"/home/abdul/Downloads/mutable/mutable/benchmark/tpc-h/data/partsupp.tbl\" DELIMITER \"|\";";
    auto ps_insert_stmt = statement_from_string(diag, ps_oss_insert.str());
    execute_statement(diag, *ps_insert_stmt);

    /* Define query:
     *
     * A -- B -- C
     */
    const char *query = "SELECT * \
                         FROM Nation, Supplier, Region \
                         WHERE Region.r_regionkey = Nation.n_nationkey AND Supplier.s_nationkey = Nation.n_regionkey;";
    auto S = m::statement_from_string(diag, query);
    M_insist(diag.num_errors() == 0);
    auto G = QueryGraph::Build(*S);

    std::ostream& outputStream = std::cout; 
    G->dump(outputStream);

    // // Learning spn on the database 
    // std::unordered_map<const char*, std::vector<Spn::LeafType>> leafTypesMap_disc = {
    //     {"Nation", {Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE, Spn::DISCRETE}}
    //     };

    // std::cout << leafTypesMap_disc <<std::endl;

    SpnEstimator SPNE("test");
    std::cout << "SpnEstimator object created" <<std::endl;
    // SPNE.learn_new_spn("Nation");
    // std::cout << "Learnt spn on single table" <<std::endl;
    SPNE.learn_spns();

    std::cout << "Learnt SPNs" << std::endl;



    // SECTION("estimate_scan")
    // {
    //     auto existing_entry_model = SPNE.estimate_scan(*G, Subproblem(1UL));
    //     CHECK(SPNE.predict_cardinality(*existing_entry_model) == 2);
    //     // auto non_existing_entry_model = SPNE.estimate_scan(*G, Subproblem(1UL << 1));
    //     // CHECK(SPNE.predict_cardinality(*non_existing_entry_model) == 10);
    // }

    // SECTION("estimate_filter")
    // {
    //     // Create a CNF filter to store the collected filters
    //     m::cnf::CNF collectedFilters;

    //     // Iterate through the sources in the QueryGraph
    //     for (auto &src : G->sources()) {
    //         // Check if the source has a filter
    //         if (!src->filter().empty()) {
    //             // Extract the filters from the source
    //             m::cnf::CNF sourceFilters = src->filter();

    //             // Append the filters to the collectedFilters CNF
    //             collectedFilters.insert(collectedFilters.end(), sourceFilters.begin(), sourceFilters.end());
    //         }
    //     }


    //     std::cout << "filter is: " << collectedFilters << std::endl;

    //     auto existing_entry_model = SPNE.estimate_scan(*G, Subproblem(1UL));
    //     CHECK(SPNE.predict_cardinality(*existing_entry_model) == 25);
    //     auto filter_existing_entry_model = SPNE.estimate_filter(*G, *existing_entry_model, collectedFilters);
    //     CHECK(SPNE.predict_cardinality(*filter_existing_entry_model) == 5);

    //     // auto non_existing_entry_model = SPNE.estimate_scan(*G, Subproblem(1UL << 1));
    //     // auto filter_non_existing_entry_model = SPNE.estimate_filter(*G, *non_existing_entry_model, collectedFilters);
    //     // CHECK(SPNE.predict_cardinality(*filter_non_existing_entry_model) == 10);
    // }

    // SECTION("estimate_limit")
    // {
    //     auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
    //     auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
    //     auto limit_existing_entry_model_high = ICE.estimate_limit(*G, *existing_entry_model, 5000, 0);
    //     auto limit_non_existing_entry_model_high = ICE.estimate_limit(*G, *non_existing_entry_model, 5000, 0);
    //     CHECK(ICE.predict_cardinality(*limit_existing_entry_model_high) == 500);
    //     CHECK(ICE.predict_cardinality(*limit_non_existing_entry_model_high) == 10);

    //     auto limit_existing_entry_model_low = ICE.estimate_limit(*G, *existing_entry_model, 8, 0);
    //     auto limit_non_existing_entry_model_low = ICE.estimate_limit(*G, *non_existing_entry_model, 8, 0);
    //     CHECK(ICE.predict_cardinality(*limit_existing_entry_model_low) == 8);
    //     CHECK(ICE.predict_cardinality(*limit_non_existing_entry_model_low) == 8);
    // }

    // SECTION("estimate_grouping (empty)")
    // {
    //     auto existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL));
    //     auto non_existing_entry_model = ICE.estimate_scan(*G, Subproblem(1UL << 1));
    //     std::vector<QueryGraph::group_type> group_by;
    //     auto grouping_existing_entry_model = ICE.estimate_grouping(*G, *existing_entry_model, group_by);
    //     auto grouping_non_existing_entry_model = ICE.estimate_grouping(*G, *non_existing_entry_model, group_by);
    //     CHECK(ICE.predict_cardinality(*grouping_existing_entry_model) == 1);
    //     CHECK(ICE.predict_cardinality(*grouping_non_existing_entry_model) == 1);
    // }

    SECTION("estimate_join")
    {
        auto existing_entry_model_one = SPNE.estimate_scan(*G, Subproblem(1UL));

        std::cout << "Subproblem(1UL) is: " << Subproblem(1UL) << std::endl;
        auto non_existing_entry_model_two = SPNE.estimate_scan(*G, Subproblem(1UL << 1));

        std::cout << "Subproble(1UL << 1) is: " << Subproblem(1UL << 1) << std::endl;
        // auto non_existing_entry_model_three = ICE.estimate_scan(*G, Subproblem(1UL << 2));
        cnf::CNF condition;

        for(auto &j : G->joins()){
            const cnf::CNF& newcondition = j->condition();
            condition.insert(condition.end(), newcondition.begin(), newcondition.end());
        }

        auto existing_entry_model_join = SPNE.estimate_join(*G, *existing_entry_model_one, *non_existing_entry_model_two,
                                                      condition);
        // auto non_existing_entry_model_join = ICE.estimate_join(*G, *existing_entry_model_one,
        //                                                        *non_existing_entry_model_three, condition);
        CHECK(SPNE.predict_cardinality(*existing_entry_model_join) == 5);
        // CHECK(ICE.predict_cardinality(*non_existing_entry_model_join) == 4000);
    }

    // SECTION("wrong database, return cartesian")
    // {
    //     std::istringstream json_input_wrong_db;
    //     json_input_wrong_db.str("{ \"mine\": [{\"relations\": [\"A\", \"B\"], \"size\":1000}]}");
    //     InjectionCardinalityEstimator ice_wrong_db(diag, "yours", json_input_wrong_db);

    //     auto non_existing_entry_model_one = ice_wrong_db.estimate_scan(*G, Subproblem(1UL));
    //     auto non_existing_entry_model_two = ice_wrong_db.estimate_scan(*G, Subproblem(1UL << 1));
    //     cnf::CNF condition;
    //     auto non_existing_entry_model_join = ice_wrong_db.estimate_join(*G, *non_existing_entry_model_one,
    //                                                                     *non_existing_entry_model_two, condition);
    //     CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_one) == 5);
    //     CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_two) == 10);
    //     CHECK(ice_wrong_db.predict_cardinality(*non_existing_entry_model_join) == 50);
    // }

}