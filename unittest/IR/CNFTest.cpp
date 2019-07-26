#include "catch.hpp"

#include "IR/CNF.hpp"
#include "util/fn.hpp"
#include <algorithm>

using namespace db;
using namespace db::cnf;


/*======================================================================================================================
 * Test CNF operations.
 *====================================================================================================================*/

TEST_CASE("CNF/Clause operators", "[unit]")
{
    Position pos("test");
    Designator A(Token(pos, "A", TK_IDENTIFIER));
    Designator B(Token(pos, "B", TK_IDENTIFIER));
    Designator C(Token(pos, "C", TK_IDENTIFIER));
    Designator D(Token(pos, "D", TK_IDENTIFIER));

    Predicate PA = cnf::Predicate::Positive(&A);
    Predicate PB = cnf::Predicate::Positive(&B);
    Predicate PC = cnf::Predicate::Positive(&C);
    Predicate PD = cnf::Predicate::Positive(&D);

    cnf::Clause AB({PA, PB});
    cnf::Clause CD({PC, PD});

    SECTION("Logical or")
    {
        auto result = AB or CD;
        REQUIRE(result.size() == 4);
        REQUIRE(contains(result, PA));
        REQUIRE(contains(result, PB));
        REQUIRE(contains(result, PC));
        REQUIRE(contains(result, PD));
    }

    SECTION("Logical and")
    {
        auto result = AB and CD;
        REQUIRE(result.size() == 2);
        REQUIRE(contains(result, AB));
        REQUIRE(contains(result, CD));
    }
}

TEST_CASE("CNF/CNF operators", "[unit]")
{
    Position pos("test");
    Designator A(Token(pos, "A", TK_IDENTIFIER));
    Designator B(Token(pos, "B", TK_IDENTIFIER));
    Designator C(Token(pos, "C", TK_IDENTIFIER));
    Designator D(Token(pos, "D", TK_IDENTIFIER));

    Predicate PA = cnf::Predicate::Positive(&A);
    Predicate PB = cnf::Predicate::Positive(&B);
    Predicate PC = cnf::Predicate::Positive(&C);
    Predicate PD = cnf::Predicate::Positive(&D);

    cnf::Clause AB({PA, PB});
    cnf::Clause CD({PC, PD});
    cnf::Clause AC({PA, PC});
    cnf::Clause BD({PB, PD});

    CNF ABCD({AB, CD});
    CNF ACBD({AC, BD});

    SECTION("Logical or")
    {
        auto result = ABCD or ACBD;
        result.dump();
        REQUIRE(result.size() == 4);

        cnf::Clause ABAC({PA, PB, PA, PC});
        cnf::Clause ABBD({PA, PB, PB, PD});
        cnf::Clause CDAC({PC, PD, PA, PC});
        cnf::Clause CDBD({PC, PD, PB, PD});

        REQUIRE(contains(result, ABAC));
        REQUIRE(contains(result, ABBD));
        REQUIRE(contains(result, CDAC));
        REQUIRE(contains(result, CDBD));
    }

    SECTION("Logical and")
    {
        auto result = ABCD and ACBD;
        result.dump();
        REQUIRE(result.size() == 4);

        REQUIRE(contains(result, AB));
        REQUIRE(contains(result, CD));
        REQUIRE(contains(result, AC));
        REQUIRE(contains(result, BD));
    }
}
