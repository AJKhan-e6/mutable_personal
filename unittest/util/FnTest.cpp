#include "catch2/catch.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <mutable/util/concepts.hpp>
#include <mutable/util/fn.hpp>
#include <sstream>
#include <string>

using namespace m;


TEST_CASE("streq", "[core][util][fn]")
{
    const char *s0 = "Hello, World";
    const char *s1 = strdup(s0);
    const char *s2 = "The quick brown fox";
    const char *s3 = "The quick brown";

    REQUIRE(streq(s0, s0));
    REQUIRE(streq(s0, s1));
    REQUIRE(streq(s1, s0));
    REQUIRE(not streq(s0, s2));
    REQUIRE(not streq(s2, s0));

    REQUIRE(not streq(s2, s3));
    REQUIRE(strneq(s2, s3, strlen(s3)));
    REQUIRE(not strneq(s2, s3, strlen(s2)));
    REQUIRE(not strneq(s2, s3, strlen(s3) + 42));

    free((void*) s1);
}

TEST_CASE("ceil_to_pow_2", "[core][util][fn]")
{
    uint32_t u31 = 1U << 31;
    uint64_t u63 = 1UL << 63;

    CHECK(1 == ceil_to_pow_2(1U));
    CHECK(2 == ceil_to_pow_2(2U));
    CHECK(4 == ceil_to_pow_2(3U));
    CHECK(4 == ceil_to_pow_2(4U));
    CHECK(8 == ceil_to_pow_2(5U));
    CHECK(u31 == ceil_to_pow_2(u31 - 1U));
    CHECK(u31 == ceil_to_pow_2(u31));
    CHECK(u63 == ceil_to_pow_2(u63 - 1UL));
    CHECK(u63 == ceil_to_pow_2(u63));
}

TEST_CASE("round_up_to_multiple", "[core][util][fn]")
{
    CHECK(0 == round_up_to_multiple(0U, 0U));
    CHECK(0 == round_up_to_multiple(0U, 1U));
    CHECK(1 == round_up_to_multiple(1U, 1U));
    CHECK(2 == round_up_to_multiple(2U, 1U));
    CHECK(0 == round_up_to_multiple(0U, 2U));
    CHECK(2 == round_up_to_multiple(1U, 2U));
    CHECK(2 == round_up_to_multiple(2U, 2U));
    CHECK(4 == round_up_to_multiple(3U, 2U));

    // 0 factor (invalid_argument)
    CHECK_THROWS_AS(round_up_to_multiple(1U, 0U), m::invalid_argument);
    CHECK_THROWS_AS(round_up_to_multiple(3U, 0U), m::invalid_argument);
    CHECK_THROWS_AS(round_up_to_multiple(100U, 0U), m::invalid_argument);
}

TEST_CASE("log2", "[core][util][fn]")
{
    SECTION("floor")
    {
        CHECK( 0 == log2_floor(1U));
        CHECK( 1 == log2_floor(2U));
        CHECK( 1 == log2_floor(3U));
        CHECK( 2 == log2_floor(4U));
        CHECK( 9 == log2_floor(1023U));
        CHECK(10 == log2_floor(1024U));
        CHECK(10 == log2_floor(1025U));
    }

    SECTION("ceil")
    {
        CHECK( 0 == log2_ceil(1U));
        CHECK( 1 == log2_ceil(2U));
        CHECK( 2 == log2_ceil(3U));
        CHECK( 2 == log2_ceil(4U));
        CHECK(10 == log2_ceil(1023U));
        CHECK(10 == log2_ceil(1024U));
        CHECK(11 == log2_ceil(1025U));
    }
}

TEST_CASE("powi", "[core][util][fn]")
{
    REQUIRE(powi(4, 0) == 1);
    REQUIRE(powi(4, 1) == 4);
    REQUIRE(powi(4, 2) == 16);
    REQUIRE(powi(4, 3) == 64);
    REQUIRE(powi(4, 4) == 256);
    REQUIRE(powi(4, 5) == 1024);
}

TEST_CASE("sum_wo_overflow", "[core][util][fn]")
{
    uint64_t UL_MAX = std::numeric_limits<uint64_t>::max();
    uint64_t U_MAX = std::numeric_limits<uint32_t>::max();

    REQUIRE(sum_wo_overflow(5U, 10U) == 15U);
    REQUIRE(sum_wo_overflow(UL_MAX, 10U) == UL_MAX);
    REQUIRE(sum_wo_overflow((1UL << 63), (1UL << 63)) == UL_MAX);
    REQUIRE(sum_wo_overflow((1UL << 62), (1UL << 62)) == (1UL << 63));
    REQUIRE(sum_wo_overflow((1UL << 63), (1UL << 63), 5U) == UL_MAX);
    REQUIRE(sum_wo_overflow((1UL << 63), 5U, (1UL << 63), 1U) == UL_MAX);
    REQUIRE(sum_wo_overflow(UL_MAX, U_MAX) == UL_MAX);
    REQUIRE(sum_wo_overflow(UL_MAX - 1, 1U) == UL_MAX);
}

TEST_CASE("prod_wo_overflow", "[core][util][fn]")
{
    uint64_t UL_MAX = std::numeric_limits<uint64_t>::max();
    uint64_t U_MAX = std::numeric_limits<uint32_t>::max();

    REQUIRE(prod_wo_overflow(5U, 10U) == 50U);
    REQUIRE(prod_wo_overflow(UL_MAX, 42U) == UL_MAX);
    REQUIRE(prod_wo_overflow(UL_MAX, UL_MAX) == UL_MAX);
    REQUIRE(prod_wo_overflow(U_MAX, U_MAX) == 18446744065119617025UL);
    REQUIRE(prod_wo_overflow(1UL << 32, U_MAX) == 18446744069414584320UL);
    REQUIRE(prod_wo_overflow(1UL << 32, 1UL << 32) == UL_MAX);
}

TEST_CASE("pattern_to_regex", "[core][util][fn]")
{
    std::string s1 = "abcd";
    std::string s2 = "defg";
    std::string s3 = "\"+";
    std::string s4 = "[]";
    std::string s5 = "()";
    std::string s6 = "{}";
    std::string s7 = ".*+^?|$";
    std::string s8 = "\\";
    std::string s9 = "_";

    SECTION("abcd")
    {
        auto r1 = std::regex("abcd");
        auto r1_ = pattern_to_regex("abcd");

        REQUIRE(std::regex_match(s1, r1) == std::regex_match(s1, r1_));
        REQUIRE(std::regex_match(s2, r1) == std::regex_match(s2, r1_));
    }

    SECTION("...d")
    {
        auto r2 = std::regex("...d");
        auto r2_ = pattern_to_regex("___d");

        REQUIRE(std::regex_match(s1, r2) == std::regex_match(s1, r2_));
        REQUIRE(std::regex_match(s2, r2) == std::regex_match(s2, r2_));
    }

    SECTION("%")
    {
        auto r3 = std::regex("(.*)d(.*)");
        auto r3_ = pattern_to_regex("%d%");

        REQUIRE(std::regex_match(s1, r3) == std::regex_match(s1, r3_));
        REQUIRE(std::regex_match(s2, r3) == std::regex_match(s2, r3_));
    }

    SECTION("_ to .")
    {
        auto r4 = std::regex("\".");
        auto r4_ = pattern_to_regex("\"_");

        REQUIRE(std::regex_match(s2, r4) == std::regex_match(s2, r4_));
        REQUIRE(std::regex_match(s3, r4) == std::regex_match(s3, r4_));
    }

    SECTION("Add backslashes on []")
    {
        auto r5 = std::regex("\\[\\]");
        auto r5_ = pattern_to_regex("[]");

        REQUIRE(std::regex_match(s1, r5) == std::regex_match(s1, r5_));
        REQUIRE(std::regex_match(s4, r5) == std::regex_match(s4, r5_));
    }

    SECTION("Add backslashes on ()")
    {
        auto r6 = std::regex("\\(\\)");
        auto r6_ = pattern_to_regex("()");

        REQUIRE(std::regex_match(s1, r6) == std::regex_match(s1, r6_));
        REQUIRE(std::regex_match(s5, r6) == std::regex_match(s5, r6_));
    }

    SECTION("Add backslashes on {}")
    {
        auto r7 = std::regex("\\{\\}");
        auto r7_ = pattern_to_regex("{}");

        REQUIRE(std::regex_match(s1, r7) == std::regex_match(s1, r7_));
        REQUIRE(std::regex_match(s6, r7) == std::regex_match(s6, r7_));
    }

    SECTION("Add backslashes on . * + ^ ? | $")
    {
        auto r8 = std::regex("\\.\\*\\+\\^\\?\\|\\$");
        auto r8_ = pattern_to_regex(".*+^?|$");

        REQUIRE(std::regex_match(s1, r8) == std::regex_match(s1, r8_));
        REQUIRE(std::regex_match(s7, r8) == std::regex_match(s7, r8_));
    }

    SECTION("Use specified escape char")
    {
        auto r9 = std::regex("_");
        auto r9_ = pattern_to_regex("a_", false, 'a');

        REQUIRE(std::regex_match(s1, r9) == std::regex_match(s1, r9_));
        REQUIRE(std::regex_match(s9, r9) == std::regex_match(s9, r9_));
    }

    SECTION("Escaped \\")
    {
        auto r10 = std::regex("\\\\");
        auto r10_ = pattern_to_regex("\\\\");

        REQUIRE(std::regex_match(s1, r10) == std::regex_match(s1, r10_));
        REQUIRE(std::regex_match(s8, r10) == std::regex_match(s8, r10_));
    }

    SECTION("Escaped _")
    {
        auto r11 = std::regex("_");
        auto r11_ = pattern_to_regex("\\_");

        REQUIRE(std::regex_match(s1, r11) == std::regex_match(s1, r11_));
        REQUIRE(std::regex_match(s9, r11) == std::regex_match(s9, r11_));
    }

    SECTION("Invalid escape character/sequence")
    {
        for (char esc : { '_', '%' }) {
            CHECK_THROWS_AS(pattern_to_regex("", false, esc), m::invalid_argument);
            CHECK_THROWS_AS(pattern_to_regex("abcd", false, esc), m::invalid_argument);
            CHECK_THROWS_AS(pattern_to_regex("abcd", true, esc), m::invalid_argument);
            CHECK_THROWS_AS(pattern_to_regex("_", false, esc), m::invalid_argument);
            CHECK_THROWS_AS(pattern_to_regex("%", false, esc), m::invalid_argument);
        }

        CHECK_THROWS_AS(pattern_to_regex("abc\\x"), m::runtime_error);
        CHECK_THROWS_AS(pattern_to_regex("\\x\\y\\z"), m::runtime_error);
        CHECK_THROWS_AS(pattern_to_regex("\\\\\\"), m::runtime_error);
    }
}

TEST_CASE("like", "[core][util][fn]")
{
    std::tuple<std::string, std::string, bool> triples[] = {
        /* { string, pattern, result } */

        /* empty pattern */
        { "", "", true },
        { "a", "", false },
        { " ", "", false },

        /* no wildcards */
        { "", "a", false },
        { "a", "a", true },
        { "A", "a", false },
        { "a", "A", false },
        { "b", "a", false },
        { "abc", "abc", true },
        { "ab", "abc", false },
        { "abcd", "abc", false },
        { "cba", "abc", false },
        { "\\", "\\\\", true },
        { "\\a", "\\\\_", true },
        { "\\ab", "\\\\%", true },
        { "_", "\\_", true },
        { "\\a", "\\_", false },
        { "%", "\\%", true },
        { "\\ab", "\\%", false },

        /* `_`-wildcard */
        { "", "_", false },
        { "a", "_", true },
        { " ", "_", true },
        { "aa", "_", false },
        { "ab", "_", false },
        { "a", "a_", false },
        { "ab", "a_", true },
        { "abc", "a_", false },
        { "axbyzc", "a_b__c", true },
        { "axbyc", "a_b__c", false },
        { "axbyz", "a_b__c", false },
        { "axbyzcd", "a_b__c", false },
        { "axcyzc", "a_b__c", false },
        { "xbyzc", "a_b__c", false },
        { "axybyzc", "a_b__c", false },
        { "axbyzqc", "a_b__c", false },

        /* `%`-wildcard */
        { "", "%", true },
        { "a", "%", true },
        { " ", "%", true },
        { "abc", "%", true },
        { "", "a%", false },
        { "a", "a%", true },
        { "abc", "a%", true },
        { "b", "a%", false },
        { "bac", "a%", false },
        { "abc", "a%b%%c", true },
        { "axyzbc", "a%b%%c", true },
        { "abxyzc", "a%b%%c", true },
        { "axyzbrstc", "a%b%%c", true },
        { "axyzbrst", "a%b%%c", false },
        { "axyzbrstcd", "a%b%%c", false },
        { "axyzcrstc", "a%b%%c", false },
        { "xyzbrstc", "a%b%%c", false },

        /* complex patterns */
        { "xabcyzdqe", "%_ab%c__d%e", true },
        { "rstabuvwcxydqlmke", "%_ab%c__d%e", true },
        { "abcyzdqe", "%_ab%c__d%e", false },
        { "xabcydqe", "%_ab%c__d%e", false },
        { "xabcyzdq", "%_ab%c__d%e", false },
        { "xyz_u%vw", "%\\__\\%%", true },
        { "_u%", "%\\__\\%%", true },
        { "xyz\\uv%abc", "%\\__\\%%", false },
        { "xyz_u\\vw", "%\\__\\%%", false },
    };

    for (const auto &[str, pattern, exp] : triples) {
        auto res = like(str, pattern);
        CHECK(exp == res);
        if (exp != res) {
            std::cerr << "Expected " << (exp ? "" : "no ") << "match for string \"" << str << "\" and pattern \""
                      << pattern << "\", but got " << (res ? "one." : "none.") << std::endl;
        }
    }
}

TEST_CASE("get_home_path", "[core][util][fn]")
{
#if __linux || __APPLE__
    char *homepath = getenv("HOME");            // Get original HOME path
    const std::string str("Hello, World");
    setenv("HOME", str.c_str(), 1);             // Replace value of HOME
    const std::string gotten = get_home_path();
    REQUIRE(str == gotten);                     // Check wether get_home_path() returns the correct string
    setenv("HOME", homepath, 1);                // Restore original HOME path
#elif _WIN32
    // TODO implement test case
#else
    /* no test available */
#endif
}

TEST_CASE("isspace", "[core][util][fn]")
{
    SECTION("5 spaces with length 5")
    {
        auto string = "     ";
        REQUIRE(isspace(string, 5));
    }

    SECTION("10 spaces with length 5")
    {
        auto string = "          ";
        REQUIRE(isspace(string, 5));
    }

    SECTION("4 spaces, 1 nonspace, length 5")
    {
        auto string = "  x  ";
        REQUIRE(not isspace(string, 5));
    }

    SECTION("String containing nonspace, but length is shorter")
    {
        auto string = "  x";
        REQUIRE(isspace(string, 2));
    }

    SECTION("Empty string")
    {
        auto string = "";
        REQUIRE(isspace(string, 0));
    }

    SECTION("Trailing spaces")
    {
        auto string = "test     ";
        REQUIRE(not isspace(string, 9));
    }

    SECTION("Spaces in the middle")
    {
        auto string = "a    b";
        REQUIRE(not isspace(string, 5));
    }

    SECTION("Given length is longer than string length")
    {
        auto string = "     ";
        REQUIRE(not isspace(string, 10));
    }
}

TEST_CASE("replace_all", "[core][util][fn]")
{
    SECTION("Replace all b with t")
    {
        auto s1 = "abcbbxyzba";
        auto s2 = "b";
        auto s3 = "t";
        REQUIRE(replace_all(s1, s2, s3) == "atcttxyzta");
    }

    SECTION("Replace all b with sql")
    {
        auto s1 = "abcbbxyzba";
        auto s2 = "b";
        auto s3 = "sql";
        REQUIRE(replace_all(s1, s2, s3) == "asqlcsqlsqlxyzsqla");
    }

    SECTION("Replace all abc with space")
    {
        auto s1 = "xyzabcabcueabcuqabc6ab!";
        auto s2 = "abc";
        auto s3 = " ";
        REQUIRE(replace_all(s1, s2, s3) == "xyz  ue uq 6ab!");
    }

    SECTION("Replace all 5 with 33")
    {
        auto s1 = "5 + 5 = 66";
        auto s2 = "5";
        auto s3 = "33";
        REQUIRE(replace_all(s1, s2, s3) == "33 + 33 = 66");
    }

    SECTION("Replace all == with .")
    {
        auto s1 = "c=f====e2==dE===2=====x";
        auto s2 = "==";
        auto s3 = ".";
        REQUIRE(replace_all(s1, s2, s3) == "c=f..e2.dE.=2..=x");
    }
}

TEST_CASE("TimePoint to human readable", "[core][util][fn]")
{
    auto check_human_readable = [](const std::string &str) -> void {
        auto it = str.begin();
        REQUIRE(it != str.end());

        /* Sign */
        if (*it == '-') ++it;

#define CHECK_DECIMAL \
        REQUIRE(it != str.end()); \
        CHECK(is_dec(*it++))
#define CHECK_CHAR(chr)  \
        REQUIRE(it != str.end()); \
        CHECK(chr == *it++)

        CHECK_DECIMAL; CHECK_DECIMAL; CHECK_DECIMAL; CHECK_DECIMAL;     // year
        CHECK_CHAR('-');
        CHECK_DECIMAL; CHECK_DECIMAL;                                   // month
        CHECK_CHAR('-');
        CHECK_DECIMAL; CHECK_DECIMAL;                                   // day of month
        CHECK_CHAR(' ');
        CHECK_DECIMAL; CHECK_DECIMAL;                                   // hour
        CHECK_CHAR(':');
        CHECK_DECIMAL; CHECK_DECIMAL;                                   // minute
        CHECK_CHAR(':');
        CHECK_DECIMAL; CHECK_DECIMAL;                                   // second

#undef CHECK_CHAR
#undef CHECK_DECIMAL

        CHECK(it == str.end());
    };

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;

    TimePoint tp;
    SECTION("1970-01-01 01:00:00")
    {
        tp = TimePoint();
    }
    SECTION("1970-01-01 01:00:04")
    {
        tp = TimePoint(std::chrono::seconds(4));
    }

    std::ostringstream oss;
    oss << put_timepoint(tp);
    check_human_readable(oss.str());
}

TEST_CASE("sequence_number/double", "[core][util]")
{
    SECTION("negative infinity")
    {
        constexpr double N_INF = -std::numeric_limits<double>::infinity();
        const double next = std::nextafter(N_INF, 0.);
        CHECK(sequence_number(next) - sequence_number(N_INF) == 1);
    }

    SECTION("positive infinity")
    {
        constexpr double P_INF = std::numeric_limits<double>::infinity();
        const double previous = std::nextafter(P_INF, 0.);
        CHECK(sequence_number(P_INF) - sequence_number(previous) == 1);
    }

    SECTION("negative zero")
    {
        constexpr double N_ZERO = -0.;
        const double next = std::nextafter(N_ZERO, +1.);
        CHECK(sequence_number(next) - sequence_number(N_ZERO) == 1);
    }

    SECTION("positive zero")
    {
        constexpr double P_ZERO = +0.;
        const double previous = std::nextafter(P_ZERO, -1.);
        CHECK(sequence_number(P_ZERO) - sequence_number(previous) == 1);
    }

    SECTION("negative one")
    {
        constexpr double N_ONE = -1.;
        const double next = std::nextafter(N_ONE, 0.);
        CHECK(sequence_number(next) - sequence_number(N_ONE) == 1);
    }

    SECTION("positive one")
    {
        constexpr double P_ONE = +1.;
        const double previous = std::nextafter(P_ONE, 0.);
        CHECK(sequence_number(P_ONE) - sequence_number(previous) == 1);
    }

    SECTION("three steps from 42.0")
    {
        constexpr double VAL = 42.;
        const double val_1 = std::nextafter(VAL, 100);
        const double val_2 = std::nextafter(val_1, 100);
        const double val_3 = std::nextafter(val_2, 100);
        CHECK(sequence_number(val_3) - sequence_number(VAL) == 3);
    }
}

TEST_CASE("sequence_number/float", "[core][util]")
{
    SECTION("negative infinity")
    {
        constexpr float N_INF = -std::numeric_limits<float>::infinity();
        const float next = std::nextafter(N_INF, 0.f);
        CHECK(sequence_number(next) - sequence_number(N_INF) == 1);
    }

    SECTION("positive infinity")
    {
        constexpr float P_INF = std::numeric_limits<float>::infinity();
        const float previous = std::nextafter(P_INF, 0.f);
        CHECK(sequence_number(P_INF) - sequence_number(previous) == 1);
    }

    SECTION("negative zero")
    {
        constexpr float N_ZERO = -0.f;
        const float next = std::nextafter(N_ZERO, +1.f);
        CHECK(sequence_number(next) - sequence_number(N_ZERO) == 1);
    }

    SECTION("positive zero")
    {
        constexpr float P_ZERO = +0.f;
        const float previous = std::nextafter(P_ZERO, -1.f);
        CHECK(sequence_number(P_ZERO) - sequence_number(previous) == 1);
    }

    SECTION("negative one")
    {
        constexpr float N_ONE = -1.f;
        const float next = std::nextafter(N_ONE, 0.f);
        CHECK(sequence_number(next) - sequence_number(N_ONE) == 1);
    }

    SECTION("positive one")
    {
        constexpr float P_ONE = +1.f;
        const float previous = std::nextafter(P_ONE, 0.f);
        CHECK(sequence_number(P_ONE) - sequence_number(previous) == 1);
    }

    SECTION("three steps from 42.0f")
    {
        constexpr float VAL = 42.f;
        const float val_1 = std::nextafter(VAL, 100.f);
        const float val_2 = std::nextafter(val_1, 100.f);
        const float val_3 = std::nextafter(val_2, 100.f);
        CHECK(sequence_number(val_3) - sequence_number(VAL) == 3);
    }
}

TEST_CASE("fast_reciprocal_sqrt", "[core][util][fn]")
{
    SECTION("float")
    {
        CHECK(fast_reciprocal_sqrt(0.1f)      == Approx(3.1622).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(1.234f)    == Approx(0.9091).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(10.987f)   == Approx(0.2994).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(100.123f)  == Approx(0.0999).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(1000.987f) == Approx(0.0316).epsilon(.01));
    }

    SECTION("double")
    {
        CHECK(fast_reciprocal_sqrt(0.1)      == Approx(3.1622).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(1.234)    == Approx(0.9091).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(10.987)   == Approx(0.2994).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(100.123)  == Approx(0.0999).epsilon(.01));
        CHECK(fast_reciprocal_sqrt(1000.987) == Approx(0.0316).epsilon(.01));
    }
}

TEST_CASE("fast_sqrt", "[core][util][fn]")
{
    SECTION("float")
    {
        CHECK(fast_sqrtf(0)    == Approx(0).epsilon(.01));
        CHECK(fast_sqrtf(1)    == Approx(1).epsilon(.01));
        CHECK(fast_sqrtf(2)    == Approx(1.414).epsilon(.01));
        CHECK(fast_sqrtf(3)    == Approx(1.732).epsilon(.01));
        CHECK(fast_sqrtf(4)    == Approx(2).epsilon(.01));
        CHECK(fast_sqrtf(5)    == Approx(2.236).epsilon(.01));
        CHECK(fast_sqrtf(10)   == Approx(3.162).epsilon(.01));
        CHECK(fast_sqrtf(100)  == Approx(10).epsilon(.01));
        CHECK(fast_sqrtf(1000) == Approx(31.62).epsilon(.01));

        CHECK(fast_sqrt(0.f)        == Approx(0).epsilon(.01));
        CHECK(fast_sqrt(2.5f)       == Approx(1.58).epsilon(.01));
        CHECK(fast_sqrt(5.55f)      == Approx(2.35).epsilon(.01));
        CHECK(fast_sqrt(7654.32f)   == Approx(87.49).epsilon(.01));
        CHECK(fast_sqrt(543210.98f) == Approx(737.52).epsilon(.01));
        CHECK(fast_sqrt(1234567.8f) == Approx(1111.11).epsilon(.01));
    }

    SECTION("double")
    {
        CHECK(fast_sqrtd(0)    == Approx(0).epsilon(.01));
        CHECK(fast_sqrtd(1)    == Approx(1).epsilon(.01));
        CHECK(fast_sqrtd(2)    == Approx(1.414).epsilon(.01));
        CHECK(fast_sqrtd(3)    == Approx(1.732).epsilon(.01));
        CHECK(fast_sqrtd(4)    == Approx(2).epsilon(.01));
        CHECK(fast_sqrtd(5)    == Approx(2.236).epsilon(.01));
        CHECK(fast_sqrtd(10)   == Approx(3.162).epsilon(.01));
        CHECK(fast_sqrtd(100)  == Approx(10).epsilon(.01));
        CHECK(fast_sqrtd(1000) == Approx(31.62).epsilon(.01));

        CHECK(fast_sqrt(0.)            == Approx(0).epsilon(.01));
        CHECK(fast_sqrt(2.5)           == Approx(1.58).epsilon(.01));
        CHECK(fast_sqrt(5.55)          == Approx(2.35).epsilon(.01));
        CHECK(fast_sqrt(654987321.543) == Approx(25589.26).epsilon(.01));
        CHECK(fast_sqrt(999999999.999) == Approx(31622.78).epsilon(.01));
        CHECK(fast_sqrt(111111111.111) == Approx(10526.31).epsilon(.01));
    }
}

TEST_CASE("escape/unescape", "[core][util][fn]")
{
    SECTION("Empty string")
    {
        CHECK(escape("") == "");
        CHECK(unescape("") == "");
    }

    SECTION("String with no escape sequence")
    {
        auto str = "Nothing to (e/un)scape! ";
        CHECK(escape(str) == str);
        CHECK(unescape(str) == str);
    }

    SECTION("String with valid quote escapes")
    {
        auto str = "String with, \"quote escapes!\"";
        auto escaped_str = "String with, \\\"quote escapes!\\\"";
        CHECK(escape(str) == escaped_str);
        CHECK(unescape(escaped_str) == str);
    }

    SECTION("String with valid backslashe escapes")
    {
        auto str = "\\ Back\\slaches \\";
        auto escaped_str = "\\\\ Back\\\\slaches \\\\";
        CHECK(escape(str) == escaped_str);
        CHECK(unescape(escaped_str) == str);
    }

    SECTION("String with valid newline escapes")
    {
        auto str = "\n Newline\nEscapes \n";
        auto escaped_str = "\\n Newline\\nEscapes \\n";
        CHECK(escape(str) == escaped_str);
        CHECK(unescape(escaped_str) == str);
    }

    SECTION("String with all escape characters")
    {
        auto str = "\n\"\\\"\n";
        auto escaped_str = "\\n\\\"\\\\\\\"\\n";
        CHECK(escape(str) == escaped_str);
        CHECK(unescape(escaped_str) == str);
    }

    SECTION("String with same quote and escape character")
    {
        auto str = "quote\"escape";
        auto escaped_str = "quote\"\"escape";
        CHECK(escape(str, '\"', '\"') == escaped_str);
        CHECK(unescape(escaped_str, '\"', '\"') == str);
    }

    SECTION("String with invalid escape sequence")
    {
        auto str = "Invalid\\Escape\\tSequence";
        CHECK(unescape(str, '\\', '\"') == str);
    }
}

TEST_CASE("html_escape", "[core][util][fn]")
{
    SECTION("Empty String")
    {
        CHECK(html_escape("") == "");
    }

    SECTION("String without special characters")
    {
        CHECK(html_escape("No special characters") == "No special characters");
    }

    SECTION("String with all special characters")
    {
        CHECK(html_escape("<&&>") == "&lt;&amp;&amp;&gt;");
    }

    SECTION("HTML with mixed characters")
    {
        std::string input = "<p>Escape &amp; me!</p>";
        std::string expected = "&lt;p&gt;Escape &amp;amp; me!&lt;/p&gt;";
        CHECK(html_escape(input) == expected);
    }
}

TEST_CASE("exec", "[core][util][fn]")
{
    SECTION("Valid executable with no arguments")
    {
        CHECK_NOTHROW(exec("/usr/bin/true", {}));
    }

    SECTION("Valid executable with arguments")
    {
        CHECK_NOTHROW(exec("/bin/sh", { "-c", "ls > ls_out.txt" }));
        CHECK_NOTHROW(exec("/bin/sh", { "-c", "rm ls_out.txt" }));
    }
}

TEST_CASE("unquote", "[core][util][fn]")
{
    SECTION("valid input")
    {
        CHECK(unquote("\"unquote\"") == "unquote");
        CHECK(unquote("\"\"") == "");
        CHECK(unquote("\"\\\"\"") == "\\\"");
        CHECK(unquote("\"\\\"unquote\\\"\"") == "\\\"unquote\\\"");
        CHECK(unquote("\"nes\"t\"ed\"") == "nes\"t\"ed");
    }

    SECTION("invalid input")
    {
        CHECK_THROWS_AS(unquote(""), m::invalid_argument);
        CHECK_THROWS_AS(unquote("a"), m::invalid_argument);
        CHECK_THROWS_AS(unquote("\""), m::invalid_argument);
        CHECK_THROWS_AS(unquote("\"a"), m::invalid_argument);

        CHECK(unquote("\\\"") == "\\\"");
        CHECK(unquote("\\a bunch_of_chars\"") == "\\a bunch_of_chars\"");

        CHECK(unquote("a\"") == "a\"");
        CHECK(unquote("a bunch_of_chars\"") == "a bunch_of_chars\"");
    }
}

TEMPLATE_TEST_CASE("is_range_wide_enough", "[core][util][fn]",
    int16_t, int32_t, int64_t, uint16_t, uint32_t, uint64_t, float, double)
{
    std::vector<std::pair<TestType, TestType>> ranges;
    CHECK(is_range_wide_enough(TestType(0), TestType(0), 0)); // n = 0
    CHECK(is_range_wide_enough(TestType(0), TestType(0), 1)); // n = 1

#define CHECK_RANGE                                                     \
    {                                                                   \
        CAPTURE(r.first, r.second, diff);                               \
        CHECK(is_range_wide_enough(r.first, r.second, diff - 1));       \
        CHECK(is_range_wide_enough(r.first, r.second, diff));           \
        CHECK(is_range_wide_enough(r.first, r.second, diff + 1));       \
        CHECK_FALSE(is_range_wide_enough(r.first, r.second, diff + 2)); \
    }

    if constexpr (std::floating_point<TestType>) { // Floating point Range
        ranges = { { 10., 100. }, { 0., 1. }, { -1., 10. }, { -1., 0. }, { -10., 100. } };

        for (auto r : ranges) {
            /** Floating point range [a, b] is wide enough if the representable values of b and a has distance
             *  of at least n. `sequence_number()` is used to get representable value of those endpoints */
            auto a = sequence_number(r.first);
            auto b = sequence_number(r.second);
            auto diff = b - a;

            CHECK_RANGE;
            std::swap(r.first, r.second);
            CHECK_RANGE;
        }
    } else { // Integral Range
        ranges = { { 10, 100 }, { 0, 1 }, }; // Unsigned Ranges
        if constexpr (signed_integral<TestType>) // Signed Integral Range
            ranges = { { 10, 100 }, { 0, 1 }, { -1, 10 }, { -1, 0 }, { -10, 100 } };

        for (auto r : ranges) {
            auto diff = r.second - r.first;

            CHECK_RANGE;
            std::swap(r.first, r.second);
            CHECK_RANGE;
        }
    }
#undef CHECK_RANGE
}

TEST_CASE("FNV1a", "[core][util][fn]")
{
    uint64_t hash = 0xcbf29ce484222325UL;

    using pairType = std::pair<const char*, uint64_t>;
    auto testcase = GENERATE(
        pairType("",       0xcbf29ce484222325UL),
        pairType("\"",     0xaf639f4c860184e5UL),
        pairType("\r",     0xaf63c04c8601bcf8UL),
        pairType("'",      0xaf639a4c86017c66UL),
        pairType("\"\"",   0x07cc7607b4949e25UL),
        pairType("\"\r",   0x07cc9707b494d638UL),
        pairType("\" \"",  0xd503c617d882b8c7UL),
        pairType("\"\"\"", 0xd50a9617d88885e5UL),
        pairType("a",      0xaf63dc4c8601ec8cUL),
        pairType("ab",     0x089c4407b545986aUL),
        pairType("a b",    0xe63f991904833892UL),
        pairType("The quick brown \"fox\"", 0x79fcb92f1a12b238UL));

    const char* c_str = testcase.first;
    uint64_t c_str_hash = testcase.second;

    CAPTURE(c_str); // Captures c_str in output if the test fails

    CHECK(FNV1a(c_str) == c_str_hash);

    CHECK(FNV1a(c_str, 0) == hash);
    CHECK(FNV1a(c_str, strlen(c_str)) == c_str_hash);
    CHECK(FNV1a(c_str, strlen(c_str) + 1) == c_str_hash);
}

TEST_CASE("PairHash", "[core][util][fn]")
{
    using str = std::string;
    using std::make_pair;

    SECTION("Symmetric Pair Types")
    {
        PairHash<int, int> ph_ii;
        PairHash<double, double> ph_dd;
        PairHash<str, str> ph_ss;

        // symmetric pairs should have same hash value
        CHECK(ph_ii(make_pair(0, 0))       == ph_ii(make_pair(0, 0)));
        CHECK(ph_dd(make_pair(0.0, 0.0))   == ph_dd(make_pair(0.0, 0.0)));
        CHECK(ph_ss(make_pair("\"", "\"")) == ph_ss(make_pair("\"", "\"")));

        // checking each pair against its reverse
        CHECK(ph_ii(make_pair(1, 2))      != ph_ii(make_pair(2, 1)));
        CHECK(ph_dd(make_pair(-1.0, 2.0)) != ph_dd(make_pair(2.0, -1.0)));
        CHECK(ph_ss(make_pair("a", "b"))  != ph_ss(make_pair("b", "a")));

        // checking each pair against a different pair of same type
        CHECK(ph_ii(make_pair(1, 2))      != ph_ii(make_pair(1, 3)));
        CHECK(ph_dd(make_pair(-1.0, 2.0)) != ph_dd(make_pair(-1.0, 3.0)));
        CHECK(ph_ss(make_pair("a", "b"))  != ph_ss(make_pair("a", "c")));

        // checking each pair against a different pair of different type
        CHECK(ph_ii(make_pair(1, 2))      != ph_dd(make_pair(1.0, 2.0)));
        CHECK(ph_dd(make_pair(-1.0, 2.0)) != ph_ss(make_pair("a", "b")));
        CHECK(ph_ss(make_pair("a", "b"))  != ph_ii(make_pair(1, 2)));
    }

    SECTION("Non-Symmetric Pair Types and Validity as Custom Hash Function")
    {
        std::hash<str> str_hash;
        std::hash<int> int_hash;

        auto p1 = PairHash<int, str>()(make_pair(1, "ab"));
        auto p2 = PairHash<double, str>()(make_pair(1.0, "ab"));
        CHECK(p2 != p1);

        std::unordered_map<std::pair<int, str>, int, PairHash<int, str>> umap;
        umap[make_pair(1e6, "ab")] = 1;
        umap[make_pair(-1e6, "ba")] = 2;

        CHECK(umap[make_pair(1e6, "ab")] == 1);
        CHECK(umap[make_pair(-1e6, "ba")] == 2);
    }
}
