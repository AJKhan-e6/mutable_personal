#pragma once

#include <mutable/util/ADT.hpp>
#include <x86intrin.h>


namespace m {

/** A sorted list of elements.  Allows duplicates. */
template<typename T, typename Compare = std::less<T>>
struct sorted_vector
{
    using vector_type = std::vector<T>; ///< the type of the internal container of elements
    using value_type = T;
    using size_type = typename vector_type::size_type;

    private:
    Compare comp_;
    vector_type v_; ///< the internal container of elements

    public:
    sorted_vector(Compare comp = Compare()) : comp_(comp) { }

    auto begin() { return v_.begin(); }
    auto end()   { return v_.end(); }
    auto begin() const { return v_.begin(); }
    auto end()   const { return v_.end(); }
    auto cbegin() const { return v_.cbegin(); }
    auto cend()   const { return v_.cend(); }

    /** Returns `true` iff the `sorted_vector` has no elements. */
    auto empty() const { return v_.empty(); }
    /** Returns the number of elements in this `sorted_vector`. */
    auto size() const { return v_.size(); }
    /** Reserves space for `new_cap` elements in this `sorted_vector`. */
    auto reserve(size_type new_cap) { return v_.reserve(new_cap); }

    /** Returns `true` iff this `sorted_vector` contains an element that is equal to `value`. */
    bool contains(const T &value) const {
        auto pos = std::lower_bound(begin(), end(), value, comp_);
        return pos != end() and *pos == value;
    }

    /** Inserts `value` into this `sorted_vector`.  Returns an `iterator` pointing to the inserted element. */
    auto insert(T value) { return v_.insert(std::lower_bound(begin(), end(), value), value, comp_); }

    /** Inserts elements in the range from `first` (including) to `last` (excluding) into this `sorted_vector. */
    template<typename InsertIt>
    void insert(InsertIt first, InsertIt last) {
        while (first != last)
            insert(*first++);
    }
};

/** Enumerate all subsets of size `k` based on superset of size `n`.
 *  See http://programmingforinsomniacs.blogspot.com/2018/03/gospers-hack-explained.html. */
struct GospersHack
{
    private:
    SmallBitset set_;
    uint64_t limit_;

    GospersHack() { }

    public:
    /** Create an instance of `GospersHack` that enumerates all subsets of size `k` of a set of `n` elements. */
    static GospersHack enumerate_all(uint64_t k, uint64_t n) {
        insist(k <= n, "invalid enumeration");
        insist(n < 64, "n exceeds range");
        GospersHack GH;
        GH.set_ = SmallBitset((1UL << k) - 1);
        GH.limit_ = 1UL << n;
        return GH;
    }
    /** Create an instance of `GospersHack` that enumerates all remaining subsets of a set of `n` elements, starting at
     * subset `set`. */
    static GospersHack enumerate_from(SmallBitset set, uint64_t n) {
        insist(n < 64, "n exceeds range");
        GospersHack GH;
        GH.set_ = set;
        GH.limit_ = 1UL << n;
        insist(uint64_t(set) <= GH.limit_, "set exceeds the limit");
        return GH;
    }

    /** Advance to the next subset. */
    GospersHack & operator++() {
        uint64_t s(set_);
        uint64_t c = s & -s;
        uint64_t r = s + c;
        set_ = SmallBitset((((r ^ s) >> 2) / c) | r);
        return *this;
    }

    /** Returns `false` iff all subsets have been enumerated. */
    operator bool() const { return uint64_t(set_) < limit_; }

    /** Returns the current subset. */
    SmallBitset operator*() const { return SmallBitset(set_); }
};

/** This class efficiently enumerates all subsets of a given size. */
struct SubsetEnumerator
{
    private:
    ///> the set to compute the power set of
    SmallBitset set_;
    ///> used to enumerate the power set of numbers 0 to n-1
    GospersHack GH_;

    public:
    SubsetEnumerator(SmallBitset set, uint64_t size)
        : set_(set)
        , GH_(GospersHack::enumerate_all(size, set.size()))
    {
        insist(set.size() >= size);
    }

    SubsetEnumerator & operator++() { ++GH_; return *this; }
    operator bool() const { return bool(GH_); }
    SmallBitset operator*() const {
        auto gh_set = *GH_;
        return SmallBitset(_pdep_u64(uint64_t(gh_set), uint64_t(set_)));
    }
};

}
