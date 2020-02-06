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

void RowStore::save(std::filesystem::path path) const
{
    std::ofstream out(path, std::ios_base::binary);

    out << "store\n" << typeid(*this).name()
        << "\ntable\n" << table().name << '\n'
        << num_rows_ << '\n';

    std::size_t num_bytes = num_rows_ * row_size_/8;
    out.write(data_.as<char*>(), num_bytes);
}

std::size_t RowStore::load(std::filesystem::path path)
{
    std::string buf;
    std::ifstream in(path, std::ios_base::binary);

    in >> buf;
    if (buf != "store")
        throw std::invalid_argument("not a storage file");
    in >> buf;
    if (buf != typeid(*this).name())
        throw std::invalid_argument("this storeage file is of a different type");
    in >> buf;
    if (buf != "table")
        throw std::invalid_argument("missing table name");
    in >> buf;
    if (buf != table().name)
        throw std::invalid_argument("this storage file is for a different table");
    std::size_t num_fresh_rows;
    in >> num_fresh_rows;
    if (capacity_ - num_fresh_rows < num_rows_)
        throw std::runtime_error("not enough capacity to load data from storage file");
    in.get(); // skip new line

    std::size_t num_bytes = num_fresh_rows * row_size_/8;
    in.read(data_.as<char*>() + num_rows_ * row_size_/8, num_bytes);
    num_rows_ += num_fresh_rows;
    return num_fresh_rows;
}

StackMachine RowStore::loader(const OperatorSchema &schema) const
{
    StackMachine sm;

    /* Add address of store to initial state. */
    auto addr_idx = sm.add(int64_t(data_.as<uintptr_t>()));

    /* Add row size to context. */
    auto row_size_idx = sm.add(int64_t(row_size_/8));

    for (auto &attr_ident : schema) {
        auto &attr = table().at(attr_ident.first.attr_name);

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
                case Numeric::N_Int: {
                    switch (n->precision) {
                        default: unreachable("illegal integer type");
                        case 1: sm.emit_Ld_RS_i8();  break;
                        case 2: sm.emit_Ld_RS_i16(); break;
                        case 4: sm.emit_Ld_RS_i32(); break;
                        case 8: sm.emit_Ld_RS_i64(); break;
                    }
                    break;
                }

                case Numeric::N_Float: {
                    if (n->precision == 32)
                        sm.emit_Ld_RS_f();
                    else
                        sm.emit_Ld_RS_d();
                    break;
                }

                case Numeric::N_Decimal: {
                    const auto p = ceil_to_pow_2(n->size());
                    switch (p) {
                        default: unreachable("illegal precision of decimal type");
                        case 8: sm.emit_Ld_RS_i8();  break;
                        case 16: sm.emit_Ld_RS_i16(); break;
                        case 32: sm.emit_Ld_RS_i32(); break;
                        case 64: sm.emit_Ld_RS_i64(); break;
                    }
                    break;
                }
            }
        } else if (auto cs = cast<const CharacterSequence>(ty)) {
            sm.add_and_emit_load(int64_t(cs->length));
            sm.emit_Ld_RS_s();
        } else {
            unreachable("illegal type");
        }
    }

    /* Update row address. */
    sm.emit_Ld_Ctx(addr_idx);
    sm.emit_Ld_Ctx(row_size_idx);
    sm.emit_Add_i();
    sm.emit_Upd_Ctx(addr_idx);
    sm.emit_Pop();

    return sm;
}

StackMachine RowStore::writer(const std::vector<const Attribute*> &attrs, std::size_t row_id) const
{
    StackMachine sm;

    /* Add address of store to initial state. */
    auto row_addr_idx = sm.add(int64_t(data_.as<uintptr_t>() + row_id * row_size_ / 8));

    /* Add row size in bytes. */
    auto row_size_idx = sm.add_and_emit_load(int64_t(row_size_/8));

    uint8_t tuple_idx = 0;
    for (auto attr : attrs) {
        if (not attr) continue; // skip nullptr

        /* Load the next value to the stack. */
        sm.emit_Ld_Tup(tuple_idx++);

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
                case Numeric::N_Int: {
                    switch (n->precision) {
                        default: unreachable("illegal integer type");
                        case 1: sm.emit_St_RS_i8();  break;
                        case 2: sm.emit_St_RS_i16(); break;
                        case 4: sm.emit_St_RS_i32(); break;
                        case 8: sm.emit_St_RS_i64(); break;
                    }
                    break;
                }

                case Numeric::N_Float: {
                    if (n->precision == 32)
                        sm.emit_St_RS_f();
                    else
                        sm.emit_St_RS_d();
                    break;
                }

                case Numeric::N_Decimal: {
                    const auto p = ceil_to_pow_2(n->size());
                    switch (p) {
                        default: unreachable("illegal precision of decimal type");
                        case 1: sm.emit_St_RS_i8();  break;
                        case 2: sm.emit_St_RS_i16(); break;
                        case 4: sm.emit_St_RS_i32(); break;
                        case 8: sm.emit_St_RS_i64(); break;
                    }
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

    /* Advance row address to next row. */
    sm.emit_Ld_Ctx(row_addr_idx);
    sm.emit_Ld_Ctx(row_size_idx);
    sm.emit_Add_i();
    sm.emit_Upd_Ctx(row_addr_idx);

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
        row_size_ = off + (alignment - off % alignment); // the row size is padded to fulfill the alignment requirements

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


/*======================================================================================================================
 * RowStore::Row
 *====================================================================================================================*/

void RowStore::Row::dispatch(callback_t callback) const
{
    struct TypeDispatch : ConstTypeVisitor
    {
        callback_t callback;
        const Attribute &attr;
        const RowStore::Row &row;

        TypeDispatch(callback_t callback, const Attribute &attr, const RowStore::Row &row)
            : callback(callback)
            , attr(attr)
            , row(row)
        { }

        using ConstTypeVisitor::operator();
        void operator()(Const<ErrorType>&) { unreachable("error type"); }
        void operator()(Const<Boolean>&) { callback(attr, row.get_generic<bool>(attr)); }
        void operator()(Const<CharacterSequence> &ty) {
            insist(not ty.is_varying, "varying length character sequences are not supported by this store");
            callback(attr, row.get_generic<std::string_view>(attr));
        }
        void operator()(Const<Numeric> &ty) {
            switch (ty.kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal:
                    callback(attr, row.get_generic<int64_t>(attr));
                    return;

                case Numeric::N_Float:
                    if (ty.precision == 32)
                        callback(attr, row.get_generic<float>(attr));
                    else
                        callback(attr, row.get_generic<double>(attr));
                    return;
            }
        }
        void operator()(Const<FnType>&) { unreachable("fn type"); }
    };

    for (auto &attr : store.table()) {
        if (isnull(attr)) {
            callback(attr, value_type());
            continue;
        }

        TypeDispatch dispatcher(callback, attr, *this);
        dispatcher(*attr.type);
    }
}
