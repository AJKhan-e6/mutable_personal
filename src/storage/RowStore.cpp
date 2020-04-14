#include "storage/RowStore.hpp"

#include "backend/StackMachine.hpp"
#include "catalog/Type.hpp"
#include "util/fn.hpp"
#include <algorithm>
#include <exception>
#include <fstream>
#include <iomanip>
#include <typeinfo>


using namespace db;


#ifndef NDEBUG
static constexpr std::size_t ALLOCATION_SIZE = 1UL << 30; ///< 1 GiB
#else
static constexpr std::size_t ALLOCATION_SIZE = 1UL << 37; ///< 128 GiB
#endif


/*======================================================================================================================
 * RowStore
 *====================================================================================================================*/

RowStore::RowStore(const Table &table)
    : Store(table)
    , offsets_(new uint32_t[table.size() + 1]) // add one slot for the offset of the meta data
{
    auto &allocator = Catalog::Get().allocator();
    compute_offsets();
    capacity_ = ALLOCATION_SIZE / (row_size_ / 8);
    data_ = allocator.allocate(ALLOCATION_SIZE);
}

RowStore::~RowStore()
{
    delete[] offsets_;
}

StackMachine RowStore::loader(const Schema &schema) const
{
    StackMachine sm;
    std::size_t out_idx = 0;

    /* Add address of store to initial state. */
    auto addr_idx = sm.add(int64_t(data_.as<uintptr_t>()));

    /* Add row size to context. */
    auto row_size_idx = sm.add(int64_t(row_size_/8));

    for (auto &e : schema) {
        auto &attr = table().at(e.id.name);

        /* Load row address to stack. */
        sm.emit_Ld_Ctx(addr_idx);

        /* Load null bit offset to stack. */
        const std::size_t null_off = offset(table().size()) + attr.id;
        sm.add_and_emit_load(int64_t(null_off));

        /* Load value bit offset to stack. */
        const std::size_t value_off = offset(attr.id);
        sm.add_and_emit_load(int64_t(value_off));

        /* Emit load from store instruction. */
        auto ty = attr.type;
        if (ty->is_boolean()) {
            sm.emit_Ld_RS_b();
        } else if (auto n = cast<const Numeric>(ty)) {
            switch (n->kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal: {
                    switch (n->size()) {
                        default: unreachable("illegal type");
                        case  8: sm.emit_Ld_RS_i8();  break;
                        case 16: sm.emit_Ld_RS_i16(); break;
                        case 32: sm.emit_Ld_RS_i32(); break;
                        case 64: sm.emit_Ld_RS_i64(); break;
                    }
                    break;
                }

                case Numeric::N_Float: {
                    if (n->size() == 32)
                        sm.emit_Ld_RS_f();
                    else
                        sm.emit_Ld_RS_d();
                    break;
                }
            }
        } else if (auto cs = cast<const CharacterSequence>(ty)) {
            sm.add_and_emit_load(int64_t(cs->length));
            sm.emit_Ld_RS_s();
        } else {
            unreachable("illegal type");
        }
        sm.emit_St_Tup(0, out_idx++, attr.type);
    }

    /* Update row address. */
    sm.emit_Ld_Ctx(addr_idx);
    sm.emit_Ld_Ctx(row_size_idx);
    sm.emit_Add_i();
    sm.emit_Upd_Ctx(addr_idx);

    return sm;
}

StackMachine RowStore::writer(const std::vector<const Attribute*> &attrs) const
{
    Schema in;
    for (auto attr : attrs)
        in.add({"attr"}, attr->type);
    StackMachine sm(in);

    /* Get row id.  Allocate a slot in the context, that is to be set from the user of this StackMachine. */
    sm.add_and_emit_load(int64_t(0));

    /* Get row size in bytes. */
    sm.add_and_emit_load(int64_t(row_size_/8));

    sm.emit_Mul_i(); // multiply row size (in bytes) and row id to compute row offset
    sm.add_and_emit_load(int64_t(data_.as<uintptr_t>())); // load store base addr
    sm.emit_Add_i(); // add row offset to base address to compute row addr
    auto row_addr_idx = sm.add(int64_t(0)); // allocate slot for row addr
    sm.emit_Upd_Ctx(row_addr_idx);

    uint8_t tuple_idx = 0;
    for (auto attr : attrs) {
        if (not attr) continue; // skip nullptr

        /* Load the next value to the stack. */
        sm.emit_Ld_Tup(0, tuple_idx++);

        /* Load row address to stack. */
        sm.emit_Ld_Ctx(row_addr_idx);

        /* Load null bit offset to stack. */
        const std::size_t null_off = offset(table().size()) + attr->id;
        sm.add_and_emit_load(int64_t(null_off));

        /* Load value bit offset to stack. */
        const std::size_t value_off = offset(attr->id);
        sm.add_and_emit_load(int64_t(value_off));

        /* Emit store to store instruction. */
        auto ty = attr->type;
        if (ty->is_boolean()) {
            sm.emit_St_RS_b();
        } else if (auto n = cast<const Numeric>(ty)) {
            switch (n->kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal:
                    switch (n->size()) {
                        default: unreachable("illegal type");
                        case  8: sm.emit_St_RS_i8();  break;
                        case 16: sm.emit_St_RS_i16(); break;
                        case 32: sm.emit_St_RS_i32(); break;
                        case 64: sm.emit_St_RS_i64(); break;
                    }
                    break;

                case Numeric::N_Float: {
                    if (n->size() == 32)
                        sm.emit_St_RS_f();
                    else
                        sm.emit_St_RS_d();
                    break;
                }
            }
        } else if (auto cs = cast<const CharacterSequence>(ty)) {
            sm.add_and_emit_load(int64_t(cs->length));
            sm.emit_St_RS_s();
        } else {
            unreachable("illegal type");
        }
    }

    return sm;
}

void RowStore::compute_offsets()
{
    using std::max;

    const auto num_attrs = table().size();
    const Attribute **attrs = new const Attribute*[num_attrs];

    for (uint32_t pos = 0; pos != num_attrs; ++pos)
        attrs[pos] = &table()[pos];

    /* Sort attributes by their alignment requirement in descending order. */
    std::stable_sort(attrs, attrs + num_attrs, [](const Attribute *first, const Attribute *second) {
        return first->type->alignment() > second->type->alignment();
    });

    /* Compute offsets. */
    uint32_t off = 0;
    uint32_t alignment = 8;
    for (uint32_t pos = 0; pos != num_attrs; ++pos) {
        const Attribute &attr = *attrs[pos];
        offsets_[attr.id] = off;
        off += attr.type->size();
        alignment = max(alignment, attr.type->alignment());
    }
    /* Add space for meta data. */
    offsets_[num_attrs] = off;
    off += num_attrs; // reserve space for the NULL bitmap
    if (off % alignment)
        off += (alignment - off % alignment); // the offset is padded to fulfill the alignment requirements
    row_size_ = off;

    delete[] attrs;
}

void RowStore::dump(std::ostream &out) const
{
    out << "RowStore at " << data_.addr() << " for table \"" << table().name << "\": " << num_rows_ << '/' << capacity_
        << " rows, " << row_size_ << " bits per row, offsets [";
    for (uint32_t i = 0, end = table().size(); i != end; ++i) {
        if (i != 0) out << ", ";
        out << offsets_[i];
    }
    out << ']' << std::endl;
}

std::unique_ptr<Store> Store::CreateRowStore(const Table &table) { return std::make_unique<RowStore>(table); }
