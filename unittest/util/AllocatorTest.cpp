#include "catch2/catch.hpp"

#include "mutable/util/malloc_allocator.hpp"
#include <cstring>


using namespace m;

namespace {

struct S0 { int i; char c; };
struct S1 { double d; int i; };
struct alignas(8) S2 { char c; short s; };

template<typename Allocator>
void check_typed_allocation(Allocator &A)
{
#define CHECK_ALLOC(TYPE) { \
    auto p = A.template allocate<TYPE>(); \
    std::uintptr_t u = reinterpret_cast<std::uintptr_t>(p); \
    CHECK(u % sizeof(TYPE) == 0); \
    A.deallocate(p); \
}
    CHECK_ALLOC(int);
    CHECK_ALLOC(S0);
    CHECK_ALLOC(S1);
    CHECK_ALLOC(S2);
#undef CHECK_ALLOC
}

template<typename Allocator>
void check_array_allocation(Allocator &A)
{
#define CHECK_ALLOC(TYPE, COUNT) { \
    auto p = A.template allocate<TYPE>(COUNT); \
    std::uintptr_t u = reinterpret_cast<std::uintptr_t>(p); \
    CHECK(u % alignof(TYPE) == 0); \
    A.deallocate(p, COUNT); \
}
    CHECK_ALLOC(int, 42);
    CHECK_ALLOC(S0,  13);
    CHECK_ALLOC(S1,  73);
    CHECK_ALLOC(S2,   5);
#undef CHECK_ALLOC
}

}


TEST_CASE("malloc_allocator", "[core][util][allocator]")
{
    malloc_allocator A;

    SECTION("unaligned bytes")
    {
        char *p0 = (char*) A.allocate(3);
        strncpy(p0, "Test", 3);
        A.deallocate(p0, 3);

        char *p1 = (char*) A.allocate(5);
        char *p2 = (char*) A.allocate(2);

        strncpy(p1, "Hello", 5);
        A.deallocate(p1, 5);

        strncpy(p2, "World", 2);
        A.deallocate(p2, 2);
    }

    SECTION("aligned bytes")
    {
        char *p0 = (char*) A.allocate(1024, 64);
        std::uintptr_t u0 = reinterpret_cast<std::uintptr_t>(p0);
        CHECK(u0 % 64 == 0);
        strncpy(p0, "Test", 3);
        A.deallocate(p0, 3);

        char *p1 = (char*) A.allocate(1024, 128);
        std::uintptr_t u1 = reinterpret_cast<std::uintptr_t>(p1);
        CHECK(u1 % 128 == 0);

        char *p2 = (char*) A.allocate(256, 256);
        std::uintptr_t u2 = reinterpret_cast<std::uintptr_t>(p2);
        CHECK(u2 % 256 == 0);

        strncpy(p1, "Hello", 5);
        A.deallocate(p1, 5);

        strncpy(p2, "World", 2);
        A.deallocate(p2, 2);
    }

    SECTION("typed") { check_typed_allocation(A); }

    SECTION("array") { check_array_allocation(A); }
}
