#pragma once

#include "backend/WasmUtil.hpp"
#include <mutable/parse/AST.hpp>
#include <binaryen-c.h>
#include <unordered_map>
#include <vector>


namespace m {

/*======================================================================================================================
 * WasmPartition
 *====================================================================================================================*/

struct WasmPartition
{
    using order_type = std::pair<const Expr*, bool>;

    virtual ~WasmPartition() { }

    /** Emits code to perform a binary partitioning of an array of tuples of type `schema`.
     *
     * \param module    the WebAssembly module
     * \param fn        the current function
     * \param schema    the `Schema` of the tuples
     * \param order     the ordering used for comparison
     * \param b_begin   the address of the first tuple
     * \param b_end     the address one after the last tuple
     * \param b_pivot   the address of the pivot element
     */
    virtual WasmTemporary emit(FunctionBuilder &fn, BlockBuilder &block,
                               const WasmStructCGContext &context, const std::vector<order_type> &order,
                               WasmTemporary begin, WasmTemporary end, WasmTemporary pivot) const = 0;
};

/** Emits a function to perform partitioning of an array of comparable elements using conditional branches.
 *
 *      template<typename T>
 *      T * partition_branching(const T pivot, T *begin, T *end)
 *      {
 *          using std::swap;
 *          while (begin < end) {
 *              if (*begin < pivot) ++begin;
 *              else if (end[-1] >= pivot) --end;
 *              else swap(*begin, end[-1]);
 *          }
 *          return begin;
 *      }
 */
struct WasmPartitionBranching : WasmPartition
{
    WasmTemporary emit(FunctionBuilder &fn, BlockBuilder &block,
                       const WasmStructCGContext &context, const std::vector<order_type> &order,
                       WasmTemporary begin, WasmTemporary end, WasmTemporary pivot) const override;
};

/** Emits a function to perform partitioning of an array of comparable elements without conditional branches.  This is
 * an implemenation in WebAssembly of our `partition_predicated_naive` algorithm in 'util/algorithms.hpp'.
 */
struct WasmPartitionBranchless : WasmPartition
{
    WasmTemporary emit(FunctionBuilder &fn, BlockBuilder &block,
                       const WasmStructCGContext &context, const std::vector<order_type> &order,
                       WasmTemporary begin, WasmTemporary end, WasmTemporary pivot) const override;
};


/*======================================================================================================================
 * WasmQuickSort
 *====================================================================================================================*/

struct WasmQuickSort
{
    using order_type = std::pair<const Expr*, bool>;

    WasmModuleCG &module; ///< the schema of tuples to sort
    const std::vector<order_type> &order; ///< the attributes to sort by
    const WasmPartition &partitioning; ///< the partitioning function

    WasmQuickSort(WasmModuleCG &module, const std::vector<order_type> &order, const WasmPartition &partitioning);

    /** Emits a function to sort a sequence of tuples using the Quicksort algorithm.  This is an implementation in
     * WebAssembly of our `qsort` algorithm in 'util/algorithms.hpp'.
     *
     * @param module    the WebAssembly module
     * @param b_begin   the expression evaluating to the beginning of the sequence
     * @param b_end     the expression evaluating to the end of the sequence
     */
    BinaryenFunctionRef emit(WasmStructCGContext &context) const;
};


/*======================================================================================================================
 * WasmBitMix
 *====================================================================================================================*/

struct WasmBitMix
{
    virtual ~WasmBitMix() { }

    virtual WasmTemporary emit(WasmModuleCG &module, FunctionBuilder &fn, BlockBuilder &block,
                               WasmTemporary bits) const = 0;
};

struct WasmBitMixMurmur3 : WasmBitMix
{
    WasmTemporary emit(WasmModuleCG &module, FunctionBuilder &fn, BlockBuilder &block,
                       WasmTemporary bits) const override;
};


/*======================================================================================================================
 * WasmHash
 *====================================================================================================================*/

struct WasmHash
{
    using element_type = std::pair<WasmTemporary, const Type&>;

    virtual ~WasmHash() { }

    virtual WasmTemporary emit(WasmModuleCG &module, FunctionBuilder &fn, BlockBuilder &block,
                               const std::vector<element_type> &values) const = 0;
};

struct WasmHashMumur3_64A : WasmHash
{
    WasmTemporary emit(WasmModuleCG &module, FunctionBuilder &fn, BlockBuilder &block,
                       const std::vector<element_type> &values) const override;
};


/*======================================================================================================================
 * WasmHashTable
 *====================================================================================================================*/

struct WasmHashTable
{
    WasmModuleCG &module;
    FunctionBuilder &fn;
    const WasmStruct &struc; ///< the structure of elements in the hash table
    private:
    std::vector<WasmStruct::index_type> key_; ///< the indices of all key fields
    std::vector<WasmStruct::index_type> payload_; ///< the indices of all payload fields

    public:
    WasmHashTable(WasmModuleCG &module, FunctionBuilder &fn, const WasmStruct &struc,
                  std::vector<WasmStruct::index_type> key)
        : module(module)
        , fn(fn)
        , struc(struc)
        , key_(std::move(key))
    {
        /* Compute payload as complement of `key_`. */
        auto key_it = key_.begin();
        for (WasmStruct::index_type i = 0; i != struc.num_entries(); ++i) {
            while (key_it != key_.end() and *key_it < i) ++key_it;
            if (key_it != key_.end() and *key_it == i) continue;
            payload_.push_back(i);
        }
#if 0
        std::cerr << "keys:";
        for (auto k : key_) std::cerr << ' ' << k;
        std::cerr << "\npayloads:";
        for (auto p : payload_) std::cerr << ' ' << p;
        std::cerr << std::endl;
#endif
    }

    virtual ~WasmHashTable() { }

    /** Returns a `std::vector` of indices of the key fields. */
    const std::vector<WasmStruct::index_type> & key() const { return key_; }

    /** Returns a `std::vector` of indices of the payload fields. */
    const std::vector<WasmStruct::index_type> & payload() const { return payload_; }

    /** Create a fresh hash table at the address `begin` with `num_buckets` number of buckets.
     *
     * @param block         the block to emit code into
     * @param b_addr        the address where to allocate the hash table
     * @param num_buckets   the number of initial buckets to allocate
     * @return              the address immediately after the hash table
     * */
    virtual WasmTemporary create_table(BlockBuilder &block, WasmTemporary addr, std::size_t num_buckets) const = 0;

    /** Overwrite each slot in the hash table with `values`. */
    virtual void clear_table(BlockBuilder &block, WasmTemporary begin, WasmTemporary end) const = 0;

    /** Given the `hash` of an element, returns the location of its preferred bucket in the hash table. */
    virtual WasmTemporary hash_to_bucket(WasmTemporary hash) const = 0;

    /** Given a bucket address, locate a key inside the bucket.  Returns the address of the slot where the key is found
     * together with the number of probing steps performed.  If the key is not found, the address of the first slot that
     * is unoccupied is returned instead. */
    virtual std::pair<WasmTemporary, WasmTemporary>
    find_in_bucket(BlockBuilder &block, WasmTemporary b_bucket_addr, const std::vector<WasmTemporary> &key) const = 0;

    /** Evaluates to `1` iff the slot is empty (i.e. not occupied). */
    virtual WasmTemporary is_slot_empty(WasmTemporary b_slot_addr) const = 0;

    virtual WasmTemporary compare_key(BlockBuilder &block, WasmTemporary slot_addr,
                                      const std::vector<WasmTemporary> &key) const = 0;

    /** Inserts a new entry into the bucket at `b_bucket_addr` by updating the bucket's probe length to `b_steps`,
     * marking the slot at `b_slot_addr` occupied, and placing the key in this slot. */
    virtual void emplace(BlockBuilder &block, WasmTemporary bucket_addr, WasmTemporary steps, WasmTemporary slot_addr,
                         const std::vector<WasmTemporary> &key) const = 0;

    /** Creates a `WasmEnvironment` to load values from the slot at `b_slot_addr`. */
    virtual WasmEnvironment load_from_slot(WasmTemporary slot_addr) const = 0;

    /** Emits code to store the value `value` to the `idx`-th field in the slot at `b_slot_addr`. */
    virtual void store_value_to_slot(BlockBuilder &block, WasmTemporary slot_addr, std::size_t idx,
                                     WasmTemporary value) const = 0;

    /** Given the address of a slot `b_slot_addr`, compute the address of the next slot.  That is, the address of the
     * slot immediately after `b_slot_addr`. */
    virtual WasmTemporary compute_next_slot(WasmTemporary slot_addr) const = 0;

    virtual WasmTemporary insert_with_duplicates(BlockBuilder &block, WasmTemporary hash,
                                                 const std::vector<WasmTemporary> &key) const = 0;

    virtual WasmTemporary insert_without_duplicates(BlockBuilder &block, WasmTemporary hash,
                                                    const std::vector<WasmTemporary> &key) const = 0;

    virtual BinaryenFunctionRef rehash(WasmHash &hasher) const = 0;
};

struct WasmRefCountingHashTable : WasmHashTable
{
    static constexpr std::size_t REFERENCE_SIZE = 4; ///< 4 bytes for reference counting

    private:
    WasmVariable addr_; ///< the address of the hash table
    WasmVariable mask_; ///< the mask used to compute a slot address in the table, i.e. capacity - 1
    std::size_t entry_size_; ///< the size in bytes of a table entry, that is the key-value pair and meta data
    mutable BinaryenFunctionRef fn_rehash_ = nullptr; ///< the rehashing function for this hash table

    public:
    WasmRefCountingHashTable(WasmModuleCG &module, FunctionBuilder &fn, const WasmStruct &struc,
                             std::vector<WasmStruct::index_type> key)
        : WasmHashTable(module, fn, struc, std::move(key))
        , addr_(fn, BinaryenTypeInt32())
        , mask_(fn, BinaryenTypeInt32())
        , entry_size_(round_up_to_multiple<std::size_t>(REFERENCE_SIZE + struc.size_in_bytes(), 4))
    { }

    /** Create a WasmHashTable instance from an existing hash table. */
    WasmRefCountingHashTable(WasmModuleCG &module, FunctionBuilder &fn, BlockBuilder &block,
                             const WasmStruct &struc, WasmTemporary addr, WasmTemporary mask,
                             std::vector<WasmStruct::index_type> key)
        : WasmHashTable(module, fn, struc, std::move(key))
        , addr_(fn, BinaryenTypeInt32())
        , mask_(fn, BinaryenTypeInt32())
        , entry_size_(round_up_to_multiple<std::size_t>(REFERENCE_SIZE + struc.size_in_bytes(), 4))
    {
        block += addr_.set(std::move(addr));
        block += mask_.set(std::move(mask));
    }

    WasmTemporary create_table(BlockBuilder &block, WasmTemporary addr, std::size_t num_buckets) const override;

    void clear_table(BlockBuilder &block, WasmTemporary begin, WasmTemporary end) const override;

    WasmTemporary hash_to_bucket(WasmTemporary hash) const override;

    std::pair<WasmTemporary, WasmTemporary>
    find_in_bucket(BlockBuilder &block, WasmTemporary b_bucket_addr,
                   const std::vector<WasmTemporary> &key) const override;

    WasmTemporary is_slot_empty(WasmTemporary b_slot_addr) const override;

    WasmTemporary compare_key(BlockBuilder &block, WasmTemporary slot_addr,
                              const std::vector<WasmTemporary> &key) const override;

    void emplace(BlockBuilder &block, WasmTemporary bucket_addr, WasmTemporary steps, WasmTemporary slot_addr,
                 const std::vector<WasmTemporary> &key) const override;

    WasmEnvironment load_from_slot(WasmTemporary slot_addr) const override;

    void store_value_to_slot(BlockBuilder &block, WasmTemporary slot_addr, std::size_t idx,
                             WasmTemporary value) const override;

    WasmTemporary compute_next_slot(WasmTemporary slot_addr) const override;

    const WasmVariable & addr() const { return addr_; }
    const WasmVariable & mask() const { return mask_; }

    std::size_t entry_size() const { return entry_size_; }

    WasmTemporary get_bucket_ref_count(WasmTemporary b_bucket_addr) const;

    WasmTemporary insert_with_duplicates(BlockBuilder &block,
                                         WasmTemporary hash,
                                         const std::vector<WasmTemporary> &key) const override;

    WasmTemporary insert_without_duplicates(BlockBuilder &block, WasmTemporary hash,
                                            const std::vector<WasmTemporary> &key) const override;

    BinaryenFunctionRef rehash(WasmHash &hasher) const override;
};

}
