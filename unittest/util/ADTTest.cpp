#include "catch.hpp"

#include "util/ADT.hpp"


using namespace m;


TEST_CASE("SmallBitset", "[core][util]")
{
    SmallBitset S;
    REQUIRE(S.empty());
    REQUIRE(S.size() == 0);
    REQUIRE(S.capacity() == 64);

    SECTION("setting and checking bits")
    {
        S(0) = true;
        REQUIRE(S == SmallBitset(1UL));
        REQUIRE(S.size() == 1);
        S[2] = true;
        REQUIRE(S == SmallBitset(5UL));
        S[2] = true;
        REQUIRE(S == SmallBitset(5UL));
        REQUIRE(S.size() == 2);
        REQUIRE(not S.empty());
        REQUIRE(S(0));
        REQUIRE(S(2));
        REQUIRE(not S(1));
    }

    SECTION("bitwise operations")
    {
        SmallBitset S1(14);
        SmallBitset S2(10);

        REQUIRE((S1 | S2) == S1);
        REQUIRE((S1 & S2) == S2);
        REQUIRE((S1 - S2) == SmallBitset(4UL));
        REQUIRE((S - S2) == S);
    }

    SECTION("is_subset")
    {
        SmallBitset S1(14);
        SmallBitset S2(10);

        REQUIRE(S2.is_subset(S1));
        REQUIRE(not S1.is_subset(S2));
    }

    SECTION("out of range")
    {
        SmallBitset S;
        REQUIRE_THROWS_AS(S.at(64), m::out_of_range);
    }
}

TEST_CASE("GospersHack", "[core][util]")
{
    SECTION("factory methods")
    {
        GospersHack S1 = GospersHack::enumerate_all(3UL, 5UL); // 3 of 5
        REQUIRE(*S1 == SmallBitset(7UL));
        REQUIRE(S1);

        GospersHack S2 = GospersHack::enumerate_from(SmallBitset(14UL), 5UL); // 14 = 0b01110
        REQUIRE(*S2 == SmallBitset(14UL));
        REQUIRE(S2);
    }

    SECTION("enumerating subsets")
    {
        GospersHack S = GospersHack::enumerate_all(3UL, 4UL); // 3 of 4
        REQUIRE(*S == SmallBitset(7UL));        // 0b0111
        REQUIRE(*(++S) == SmallBitset(11UL));   // 0b1011
        REQUIRE(*(++S) == SmallBitset(13UL));   // 0b1101
        REQUIRE(*(++S) == SmallBitset(14UL));   // 0b1110
        REQUIRE(not ++S);
    }
}

TEST_CASE("SmallBitset/least_subset", "[core][util][fn]")
{
    SmallBitset set(10UL); // 0b1010 <=> { 2, 8 }
    REQUIRE(least_subset(set) == SmallBitset(2UL));
}


TEST_CASE("SmallBitset/next_subset", "[core][util][fn]")
{
    SmallBitset set(10UL); // 0b1010 <=> { 2, 8 }

    REQUIRE(next_subset(SmallBitset(0UL), set) == SmallBitset(2UL));
    REQUIRE(next_subset(SmallBitset(2UL), set) == SmallBitset(8UL));
    REQUIRE(next_subset(SmallBitset(8UL), set) == set);
    REQUIRE(next_subset(SmallBitset(10UL), set) == SmallBitset(0UL));
}

TEST_CASE("doubly_linked_list", "[core][util]")
{
    auto CHECK_LIST = []<typename T>(const doubly_linked_list<T> &L, std::initializer_list<T> values) -> void {
        using std::begin, std::end, std::rbegin, std::rend;
        REQUIRE(L.size() == values.size());

        /* Forward iteration. */
        {
            auto list_it = L.cbegin();
            for (auto value_it = begin(values); value_it != end(values); ++value_it, ++list_it) {
                REQUIRE(list_it != L.cend());
                CHECK(*list_it == *value_it);
            }
            CHECK(list_it == L.cend());
        }

        /* Reverse iteration. */
        {
            auto list_rit = L.crbegin();
            for (auto value_it = rbegin(values); value_it != rend(values); ++value_it, ++list_rit) {
                REQUIRE(list_rit != L.crend());
                CHECK(*list_rit == *value_it);
            }
            CHECK(list_rit == L.crend());
        }
    };

    doubly_linked_list<int> L;

    REQUIRE(L.size() == 0);
    REQUIRE(L.begin() == L.end());
    REQUIRE(L.empty());
    CHECK_LIST(L, { });

    SECTION("emplace")
    {
        SECTION("empty")
        {
            REQUIRE(L.begin() == L.end());
            auto pos = L.emplace(L.begin(), 42);
            REQUIRE(L.size() == 1);
            CHECK(*pos == 42);
            CHECK(pos == L.begin());
            CHECK_LIST(L, { 42 });
        }

        L.push_back(42);
        L.push_back(13);

        SECTION("front")
        {
            auto pos = L.emplace(L.begin(), 73);
            REQUIRE(L.size() == 3);
            CHECK(*pos == 73);
            CHECK(pos == L.begin());
            CHECK_LIST(L, { 73, 42, 13 });
            ++pos;
            REQUIRE(pos != L.end());
            CHECK(*pos == 42);
            ++pos;
            REQUIRE(pos != L.end());
            CHECK(*pos == 13);
            ++pos;
            CHECK(pos == L.end());
        }

        SECTION("mid")
        {
            auto pos = L.emplace(std::next(L.begin()), 73);
            REQUIRE(L.size() == 3);
            CHECK(*pos == 73);
            CHECK(pos == std::next(L.begin()));
            CHECK_LIST(L, { 42, 73, 13 });
            ++pos;
            REQUIRE(pos != L.end());
            CHECK(*pos == 13);
            ++pos;
            CHECK(pos == L.end());
        }

        SECTION("back")
        {
            auto pos = L.emplace(L.end(), 73);
            REQUIRE(L.size() == 3);
            CHECK(*pos == 73);
            CHECK(pos == std::next(std::next(L.begin())));
            CHECK_LIST(L, { 42, 13, 73 });
            ++pos;
            CHECK(pos == L.end());
        }
    }

    SECTION("emplace_front")
    {
        {
            auto &ref = L.emplace_front(42);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 1);
            CHECK(ref == 42);
            CHECK(L.front() == 42);
            CHECK(L.back() == 42);
            CHECK_LIST(L, { 42 });
        }

        {
            auto &ref = L.emplace_front(13);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 2);
            CHECK(ref == 13);
            CHECK(L.front() == 13);
            CHECK(L.back() == 42);
            CHECK_LIST(L, { 13, 42 });
        }

        {
            auto &ref = L.emplace_front(73);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 3);
            CHECK(ref == 73);
            CHECK(L.front() == 73);
            CHECK(L.back() == 42);
            CHECK_LIST(L, { 73, 13, 42 });
        }
    }

    SECTION("emplace_back")
    {
        {
            auto &ref = L.emplace_back(42);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 1);
            CHECK(ref == 42);
            CHECK(L.front() == 42);
            CHECK(L.back() == 42);
            CHECK_LIST(L, { 42 });
        }

        {
            auto &ref = L.emplace_back(13);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 2);
            CHECK(ref == 13);
            CHECK(L.front() == 42);
            CHECK(L.back() == 13);
            CHECK_LIST(L, { 42, 13 });
        }

        {
            auto &ref = L.emplace_back(73);
            REQUIRE_FALSE(L.empty());
            REQUIRE(L.size() == 3);
            CHECK(ref == 73);
            CHECK(L.front() == 42);
            CHECK(L.back() == 73);
            CHECK_LIST(L, { 42, 13, 73 });
        }
    }

    SECTION("insert")
    {
        SECTION("multiple")
        {
            auto it = L.insert(L.begin(), 3, 42);
            CHECK_LIST(L, { 42, 42, 42 });
            REQUIRE(it != L.end());
            CHECK(*it == 42);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 42);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 42);
            ++it;
            CHECK(it == L.end());
        }

        SECTION("range")
        {
            std::vector<int> vec{{ 42, 13, 73 }};
            auto it = L.insert(L.begin(), vec.cbegin(), vec.cend());
            CHECK_LIST(L, { 42, 13, 73 });
            REQUIRE(it != L.end());
            CHECK(*it == 42);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 13);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 73);
            ++it;
            CHECK(it == L.end());
        }

        SECTION("initializer list")
        {
            auto it = L.insert(L.begin(), { 42, 13, 73 });
            CHECK_LIST(L, { 42, 13, 73 });
            REQUIRE(it != L.end());
            CHECK(*it == 42);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 13);
            ++it;
            REQUIRE(it != L.end());
            CHECK(*it == 73);
            ++it;
            CHECK(it == L.end());
        }
    }

    SECTION("iterators")
    {
        L.push_back(42);
        L.push_back(13);
        L.push_back(73);
        CHECK_LIST(L, { 42, 13, 73 });

        decltype(L)::iterator it = L.begin();
        decltype(L)::const_iterator cit = it; // implicit conversion
        (void) cit;
        // decltype(L)::iterator ncit = cit; // illegal conversion
    }

    SECTION("range c'tor")
    {
        const std::vector<int> vec{{ 42, 13, 73 }};
        doubly_linked_list<int> L(vec.begin(), vec.end());
        CHECK_LIST(L, { 42, 13, 73 });
    }

    SECTION("clear")
    {
        L.push_back(42);
        L.push_back(13);
        L.push_back(73);
        REQUIRE(L.size() == 3);

        L.clear();
        CHECK(L.size() == 0);
        CHECK(L.begin() == L.end());
        CHECK(L.rbegin() == L.rend());
    }

    SECTION("erase")
    {
        L.push_back(42);

        SECTION("last")
        {
            auto to_erase = L.begin();
            auto ref = L.erase(to_erase);
            CHECK(L.size() == 0);
            REQUIRE(ref == L.end());
            CHECK_LIST(L, { });
       }

        L.push_back(13);
        L.push_back(73);

        SECTION("front")
        {
            auto to_erase = L.begin();
            auto ref = L.erase(to_erase); // erase 42
            CHECK(L.size() == 2);
            REQUIRE(ref != L.end());
            CHECK(*ref == 13);
            CHECK_LIST(L, { 13, 73 });
        }

        SECTION("mid")
        {
            auto to_erase = L.begin();
            ++to_erase;
            auto ref = L.erase(to_erase); // erase 13
            CHECK(L.size() == 2);
            REQUIRE(ref != L.end());
            CHECK(*ref == 73);
            CHECK_LIST(L, { 42, 73 });
        }

        SECTION("back")
        {
            auto to_erase = L.begin();
            ++to_erase;
            ++to_erase;
            auto ref = L.erase(to_erase); // erase 73
            CHECK(L.size() == 2);
            REQUIRE(ref == L.end());
            CHECK_LIST(L, { 42, 13 });
        }
    }

    SECTION("pop_front")
    {
        L.push_back(42);

        SECTION("last")
        {
            auto val = L.pop_front();
            CHECK(val == 42);
            CHECK_LIST(L, { });
        }

        L.push_back(13);
        L.push_back(73);

        SECTION("multiple")
        {
            auto val = L.pop_front();
            CHECK(val == 42);
            CHECK_LIST(L, { 13, 73 });
        }
    }

    SECTION("pop_back")
    {
        L.push_back(42);

        SECTION("last")
        {
            auto val = L.pop_back();
            CHECK(val == 42);
            CHECK_LIST(L, { });
        }

        L.push_back(13);
        L.push_back(73);

        SECTION("multiple")
        {
            auto val = L.pop_back();
            CHECK(val == 73);
            CHECK_LIST(L, { 42, 13 });
        }
    }

    SECTION("reverse")
    {
        L.push_back(42);
        L.push_back(13);
        L.push_back(73);
        L.reverse();
        CHECK_LIST(L, { 73, 13, 42 });
    }
}
