#include "catch.hpp"

#include "util/fn.hpp"
#include <cstdlib>
#include <cstring>


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

    REQUIRE(1 == ceil_to_pow_2(1U));
    REQUIRE(2 == ceil_to_pow_2(2U));
    REQUIRE(4 == ceil_to_pow_2(3U));
    REQUIRE(4 == ceil_to_pow_2(4U));
    REQUIRE(8 == ceil_to_pow_2(5U));
    REQUIRE(u31 == ceil_to_pow_2(u31 - 1U));
    REQUIRE(u31 == ceil_to_pow_2(u31));
    REQUIRE(u63 == ceil_to_pow_2(u63 - 1UL));
    REQUIRE(u63 == ceil_to_pow_2(u63));
}

TEST_CASE("pow", "[core][util][fn]")
{
    REQUIRE(pow(4, 0) == 1);
    REQUIRE(pow(4, 1) == 4);
    REQUIRE(pow(4, 2) == 16);
    REQUIRE(pow(4, 3) == 64);
    REQUIRE(pow(4, 4) == 256);
    REQUIRE(pow(4, 5) == 1024);
}
