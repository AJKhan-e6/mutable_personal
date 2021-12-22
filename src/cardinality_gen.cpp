#include "globals.hpp"
#include "util/ArgParser.hpp"
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutable/IR/PlanTable.hpp>
#include <mutable/mutable.hpp>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <x86intrin.h>


using Subproblem = m::SmallBitset;

struct args_t
{
    ///> whether to show a help message
    bool show_help;
    ///> the seed for the PRNG
    unsigned seed;
    ///> minimum cardinality of relations and intermediate results
    std::size_t min_cardinality;
    ///> maximum cardinality of relations and intermediate results
    std::size_t max_cardinality;
};

struct entry_t
{
    std::size_t max_cardinality = -1UL;
    entry_t() { }
};

using table_type = std::unordered_map<Subproblem, entry_t, m::SubproblemHash>;

table_type generate_cardinalities_for_query(const m::QueryGraph &G, const m::AdjacencyMatrix &M, const args_t &args);

void emit_cardinalities(std::ostream &out, const m::QueryGraph &G, const table_type &table);

void usage(std::ostream &out, const char *name)
{
    out << "A tool to generate fake cardinalities for queries.\n"
        << "USAGE:\n\t" << name << " <SCHEMA.sql> [<QUERY.sql>]"
        << std::endl;
}

int main(int argc, const char **argv)
{
    /*----- Parse command line arguments. ----------------------------------------------------------------------------*/
    m::ArgParser AP;
    args_t args;
#define ADD(TYPE, VAR, INIT, SHORT, LONG, DESCR, CALLBACK)\
    VAR = INIT;\
    {\
        std::function<void(TYPE)> callback = CALLBACK;\
        AP.add(SHORT, LONG, DESCR, callback);\
    }
    /*----- Help message ---------------------------------------------------------------------------------------------*/
    ADD(bool, args.show_help, false,                                        /* Type, Var, Init  */
        "-h", "--help",                                                     /* Short, Long      */
        "prints this help message",                                         /* Description      */
        [&](bool) { args.show_help = true; });                              /* Callback         */
    /*----- Seed -----------------------------------------------------------------------------------------------------*/
    ADD(unsigned, args.seed, 42,                                            /* Type, Var, Init  */
        nullptr, "--seed",                                                  /* Short, Long      */
        "the seed for the PRNG",                                            /* Description      */
        [&](unsigned s) { args.seed = s; });                                /* Callback         */
    /*----- Cardinalities --------------------------------------------------------------------------------------------*/
    ADD(std::size_t, args.min_cardinality, 1,                               /* Type, Var, Init  */
        nullptr, "--min",                                                   /* Short, Long      */
        "the minimum cardinality of base tables",                           /* Description      */
        [&](std::size_t card) { args.min_cardinality = card; });            /* Callback         */
    ADD(std::size_t, args.max_cardinality, 1e6,                             /* Type, Var, Init  */
        nullptr, "--max",                                                   /* Short, Long      */
        "the maximum cardinality of base tables",                           /* Description      */
        [&](std::size_t card) { args.max_cardinality = card; });            /* Callback         */
    /*----- Parse command line arguments. ----------------------------------------------------------------------------*/
    AP.parse_args(argc, argv);

    /*----- Help message. -----*/
    if (args.show_help) {
        usage(std::cout, argv[0]);
        std::cout << "WHERE\n";
        AP.print_args(stdout);
        std::exit(EXIT_SUCCESS);
    }

    /*----- Validate command line arguments. -------------------------------------------------------------------------*/
    if (AP.args().size() == 0 or AP.args().size() > 2) {
        usage(std::cout, argv[0]);
        std::exit(EXIT_FAILURE);
    }

    /*----- Configure mutable. ---------------------------------------------------------------------------------------*/
    m::Options::Get().quiet = true;

    /*----- Load schema. ---------------------------------------------------------------------------------------------*/
    m::Diagnostic diag(false, std::cout, std::cerr);
    std::filesystem::path path_to_schema(AP.args()[0]);
    m::execute_file(diag, path_to_schema);
    m::Catalog &C = m::Catalog::Get();
    if (not C.has_database_in_use()) {
        std::cerr << "No database selected.\n";
        std::exit(EXIT_FAILURE);
    }

    /*----- Read input from stdin or file. ---------------------------------------------------------------------------*/
    const std::string input = [&AP]() -> std::string {
        if (AP.args().size() == 1) {
            return std::string(std::istreambuf_iterator<char>(std::cin), {});
        } else {
            std::filesystem::path path(AP.args()[1]);
            errno = 0;
            std::ifstream in(path);
            if (not in) {
                std::cerr << "Could not open file '" << path << '\'';
                const auto errsv = errno;
                if (errsv)
                    std::cerr << ": " << strerror(errsv);
                std::cerr << std::endl;
                std::exit(EXIT_FAILURE);
            }
            return std::string(std::istreambuf_iterator<char>(in), {});
        }
    }();

    /*----- Parse input. ---------------------------------------------------------------------------------------------*/
    const std::unique_ptr<m::SelectStmt> select = [&diag, &input]() -> std::unique_ptr<m::SelectStmt> {
        auto stmt = m::statement_from_string(diag, input);
        if (not is<m::SelectStmt>(stmt.get())) {
            std::cerr << "Expected a SELECT statement.\n";
            std::exit(EXIT_FAILURE);
        }
        return std::unique_ptr<m::SelectStmt>(as<m::SelectStmt>(stmt.release()));
    }();

    auto G = m::QueryGraph::Build(*select);
    m::AdjacencyMatrix M(*G);

    /*----- Generate cardinalities. ----------------------------------------------------------------------------------*/
    table_type table = generate_cardinalities_for_query(*G, M, args);

    /*----- Emit the table. ------------------------------------------------------------------------------------------*/
    emit_cardinalities(std::cout, *G, table);
}

table_type generate_cardinalities_for_query(const m::QueryGraph &G, const m::AdjacencyMatrix &M, const args_t &args)
{
    std::mt19937_64 g(args.seed);
    std::gamma_distribution<double> cardinality_dist(.5, 1.);

    table_type table(G.num_sources() * G.num_sources());
    const Subproblem All((1UL << G.num_sources()) - 1UL);

    /*----- Fill table with cardinalities for base relations. --------------------------------------------------------*/
    for (unsigned i = 0; i != G.num_sources(); ++i) {
        auto &e = table[Subproblem(1UL << i)];
        e.max_cardinality =
            args.max_cardinality - (args.max_cardinality - args.min_cardinality) / (1. + cardinality_dist(g));
    }

    auto update = [&table, &g](const Subproblem S1, const Subproblem S2) -> void {
        auto &left   = table[S1];
        auto &right  = table[S2];
        auto &joined = table[S1 | S2];

        std::gamma_distribution<double> selectivity_dist(.15, 1.);

        /*----- Compute selectivity ranges. -----*/
        constexpr double MAX_SELECTIVITY = .8;
        constexpr std::size_t MAX_GROWTH_FACTOR = 10;
        double max_selectivity = MAX_SELECTIVITY;
        /* Selectivity must not exceed max growth factor. */
        max_selectivity = std::min<double>(
            max_selectivity,
            MAX_GROWTH_FACTOR * double(std::max(left.max_cardinality, right.max_cardinality)) /
                (left.max_cardinality * right.max_cardinality)
        );
        /* Selectivity must not exceed maximum representable integer value. */
        max_selectivity = std::min<double>(
            max_selectivity,
            double(std::numeric_limits<std::size_t>::max()) / left.max_cardinality / right.max_cardinality
        );
        const double selectivity_factor = 1. - 1. / (1. + selectivity_dist(g));
        const double selectivity = max_selectivity * selectivity_factor;

        /* Make sure relations are never empty. */
        joined.max_cardinality = std::max<std::size_t>(1UL, selectivity * left.max_cardinality * right.max_cardinality);
    };

    /*----- Enumerate all connected complement pairs in ascending order. ---------------------------------------------*/
    M.for_each_CSG_pair_undirected(All, update);

    return table;
}

void emit_cardinalities(std::ostream &out, const m::QueryGraph &G, const table_type &table)
{
    m::Catalog &C = m::Catalog::Get();
    m::Database &DB = C.get_database_in_use();

    out << "{\n    \"" << DB.name << "\": [\n";
    bool first = true;
    for (auto entry : table) {
        const Subproblem S = entry.first;
        const std::size_t size = entry.second.max_cardinality;

        /*----- Emit relations. -----*/
        if (first) first = false;
        else       out << ",\n";
        out << "        { \"relations\": [";
        for (auto it = S.begin(); it != S.end(); ++it) {
            if (it != S.begin()) out << ", ";
            auto DS = G.sources()[*it];
            out << '"' << DS->name() << '"';
        }
        /*----- Emit size. -----*/
        out << "], \"size\": " << size << "}";
    }
    out << "\n    ]\n}\n";
}
