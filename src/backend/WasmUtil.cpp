#include "backend/WasmUtil.hpp"

#include "backend/Interpreter.hpp"
#include "backend/WasmMacro.hpp"
#include <optional>


using namespace m;
using namespace m::storage;
using namespace m::wasm;


/*======================================================================================================================
 * Helper function
 *====================================================================================================================*/

template<typename T>
void convert_to(SQL_t &operand)
{
    std::visit(overloaded {
        [&operand](auto &&actual) -> void requires requires { actual.template to<T>(); } {
            auto v = actual.template to<T>();
            operand.~SQL_t();
            new (&operand) SQL_t(v);
        },

        [](auto &actual) -> void requires (not requires { actual.template to<T>(); }) {
            M_unreachable("illegal conversion");
        }
    }, operand);
}

void convert_to(SQL_t &operand, const Type *to_type)
{
    visit(overloaded {
        [&operand](const Boolean&) -> void { convert_to<bool>(operand); },
        [&operand](const Numeric &n) -> void {
            switch (n.kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal:
                    switch (n.size()) {
                        default:
                            M_unreachable("invalid integer size");
                        case 8:
                            convert_to<int8_t>(operand);
                            return;
                        case 16:
                            convert_to<int16_t>(operand);
                            return;
                        case 32:
                            convert_to<int32_t>(operand);
                            return;
                        case 64:
                            convert_to<int64_t>(operand);
                            return;
                    }
                    break;
                case Numeric::N_Float:
                    if (n.size() <= 32)
                        convert_to<float>(operand);
                    else
                        convert_to<double>(operand);
                    break;
            }
        },
        [&operand](const CharacterSequence&) -> void { convert_to<char*>(operand); },
        [&operand](const Date&) -> void { convert_to<int32_t>(operand); },
        [&operand](const DateTime&) -> void { convert_to<int64_t>(operand); },
        [](auto&&) -> void { M_unreachable("illegal conversion"); },
    }, *to_type);
}


/*======================================================================================================================
 * ExprCompiler
 *====================================================================================================================*/

void ExprCompiler::operator()(const ErrorExpr&) { M_unreachable("no errors at this stage"); }

void ExprCompiler::operator()(const Designator &e)
{
    if (e.type()->is_none()) {
        set(_I32::Null()); // create NULL
        return;
    }

    /* Search with fully qualified name. */
    Schema::Identifier id(e.table_name.text, e.attr_name.text);
    set(env_.get(id));
}

void ExprCompiler::operator()(const Constant &e)
{
    if (e.type()->is_none()) {
        set(_I32::Null()); // create NULL
        return;
    }

    /* Interpret constant. */
    auto value = Interpreter::eval(e);

    visit(overloaded {
        [this, &value](const Boolean&) { set(_Bool(value.as_b())); },
        [this, &value](const Numeric &n) {
            switch (n.kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal:
                    switch (n.size()) {
                        default:
                            M_unreachable("invalid integer size");
                        case 8:
                            set(_I8(value.as_i()));
                            break;
                        case 16:
                            set(_I16(value.as_i()));
                            break;
                        case 32:
                            set(_I32(value.as_i()));
                            break;
                        case 64:
                            set(_I64(value.as_i()));
                            break;
                    }
                    break;
                case Numeric::N_Float:
                    if (n.size() <= 32)
                        set(_Float(value.as_f()));
                    else
                        set(_Double(value.as_d()));
            }
        },
        [this, &value](const CharacterSequence&) {
            set(CodeGenContext::Get().get_literal_address(value.as<const char*>()));
        },
        [this, &value](const Date&) { set(_I32(value.as_i())); },
        [this, &value](const DateTime&) { set(_I64(value.as_i())); },
        [](const NoneType&) { M_unreachable("should've been handled earlier"); },
        [](auto&&) { M_unreachable("invalid type"); },
    }, *e.type());
}

void ExprCompiler::operator()(const UnaryExpr &e)
{
    /* This is a helper to apply unary operations to `Expr<T>`s.  It uses SFINAE within `overloaded` to only apply the
     * operation if it is well typed, e.g. `+42` is ok whereas `+true` is not. */
    auto apply_unop = [this, &e](auto unop) {
        (*this)(*e.expr);
        std::visit(overloaded {
            [](std::monostate&&) -> void { M_unreachable("illegal value"); },
            [this, &unop](auto &&expr) -> void requires requires { unop(expr); } { set(unop(expr)); },
            [](auto &&expr) -> void requires (not requires { unop(expr); }) { M_unreachable("illegal operation"); },
        }, get());
    };

#define UNOP(OP) apply_unop(overloaded { \
        [](auto &&expr) -> decltype(expr.operator OP()) { return expr.operator OP(); }, \
    }); \
    break

    switch (e.op().type) {
        default:
            M_unreachable("invalid operator");

        case TK_PLUS:   UNOP(+);
        case TK_MINUS:  UNOP(-);
        case TK_TILDE:  UNOP(~);
        case TK_Not:    UNOP(not);
    }
#undef UNOP
}

void ExprCompiler::operator()(const BinaryExpr &e)
{
    /* This is a helper to apply binary operations to `Expr<T>`s.  It uses SFINAE within `overloaded` to only apply the
     * operation if it is well typed, e.g. `42 + 13` is ok whereas `true + 42` is not. */
    auto apply_binop = [this, &e](auto binop) {
        (*this)(*e.lhs);
        SQL_t lhs = get();

        (*this)(*e.rhs);
        SQL_t rhs = get();

        if (e.common_operand_type) {
            convert_to(lhs, e.common_operand_type); // convert in-place
            convert_to(rhs, e.common_operand_type); // convert in-place
        }

        std::visit(overloaded {
            [](std::monostate&&) -> void { M_unreachable("illegal value"); },
            [this, &binop, &rhs](auto &&expr_lhs) -> void {
                std::visit(overloaded {
                    [](std::monostate&&) -> void { M_unreachable("illegal value"); },
                    [this, expr_lhs, &binop](auto &&expr_rhs) mutable -> void
                    requires requires { binop(expr_lhs, expr_rhs); }
                    {
                        set(binop(expr_lhs, expr_rhs));
                    },
                    [](auto &&expr_rhs) -> void
                    requires (not requires { binop(expr_lhs, expr_rhs); })
                    {
                        M_unreachable("illegal operation");
                    },
                }, rhs);
            },
        }, lhs);
    };

#define BINOP(OP) apply_binop( \
        [](auto lhs, auto rhs) -> decltype(lhs.operator OP(rhs)) { return lhs.operator OP(rhs); } \
    ); break
#define CMPOP(OP, STRCMP_OP) { \
        if (auto ty_lhs = cast<const CharacterSequence>(e.lhs->type())) { \
            auto ty_rhs = as<const CharacterSequence>(e.rhs->type()); \
            apply_binop( \
                [&ty_lhs, &ty_rhs](Ptr<Char> lhs, Ptr<Char> rhs) -> _Bool { \
                    return strcmp(*ty_lhs, *ty_rhs, lhs, rhs, STRCMP_OP); \
                } \
            ); break; \
        } else { \
            BINOP(OP); \
        } \
    }

    switch (e.op().type) {
        default:
            M_unreachable("illegal token type");

        /*----- Arithmetic operations --------------------------------------------------------------------------------*/
        case TK_PLUS:           BINOP(+);
        case TK_MINUS:          BINOP(-);
        case TK_ASTERISK:       BINOP(*);
        case TK_SLASH:          BINOP(/);
        case TK_PERCENT:        BINOP(%);

        /*----- Comparison operations --------------------------------------------------------------------------------*/
        case TK_EQUAL:          CMPOP(==, EQ);
        case TK_BANG_EQUAL:     CMPOP(!=, NE);
        case TK_LESS:           CMPOP(<,  LT);
        case TK_LESS_EQUAL:     CMPOP(<=, LE);
        case TK_GREATER:        CMPOP(>,  GT);
        case TK_GREATER_EQUAL:  CMPOP(>=, GE);

        /*----- CharacterSequence operations -------------------------------------------------------------------------*/
        case TK_Like: {
            auto &cs_str = as<const CharacterSequence>(*e.lhs->type());
            auto &cs_pattern = as<const CharacterSequence>(*e.rhs->type());
            (*this)(*e.lhs);
            Ptr<Char> str = get<Ptr<Char>>();
            (*this)(*e.rhs);
            Ptr<Char> pattern = get<Ptr<Char>>();
            set(like(cs_str, cs_pattern, str, pattern));
            break;
        }

        case TK_DOTDOT: {
            auto &cs_lhs = as<const CharacterSequence>(*e.lhs->type());
            auto &cs_rhs = as<const CharacterSequence>(*e.rhs->type());
            (*this)(*e.lhs);
            Ptr<Char> lhs = get<Ptr<Char>>();
            (*this)(*e.rhs);
            Ptr<Char> rhs = get<Ptr<Char>>();
            auto size_lhs = cs_lhs.size() / 8;
            auto size_rhs = cs_rhs.size() / 8;

            Var<Ptr<Char>> res(Ptr<Char>::Nullptr()); // return value if at least one operand is NULL

            auto [_ptr_lhs, is_nullptr_lhs] = lhs.split();
            auto [_ptr_rhs, is_nullptr_rhs] = rhs.split();
            Ptr<Char> ptr_lhs(_ptr_lhs), ptr_rhs(_ptr_rhs); // since structured bindings cannot be used in lambda capture

            IF (not is_nullptr_lhs and not is_nullptr_rhs) {
                res = Module::Allocator().pre_malloc<char>(size_lhs + size_rhs); // create pre-allocation for result
                Var<Ptr<Char>> ptr(res.val()); // since res must not be changed
                ptr = strncpy(ptr, ptr_lhs, U32(size_lhs));
                strncpy(ptr, ptr_rhs, U32(size_rhs)).discard();
            };
            set(SQL_t(res));
            break;
        }

        /*----- Logical operations -----------------------------------------------------------------------------------*/
        case TK_And:
        case TK_Or: {
            M_insist(e.lhs->type()->is_boolean());
            M_insist(e.rhs->type()->is_boolean());

            (*this)(*e.lhs);
            _Bool lhs = get<_Bool>();
            (*this)(*e.rhs);
            _Bool rhs = get<_Bool>();

            if (e.op().type == TK_And)
                set(lhs and rhs);
            else
                set(lhs or rhs);

            break;
        }
    }
#undef CMPOP
#undef BINOP
}

void ExprCompiler::operator()(const FnApplicationExpr &e)
{
    switch (e.get_function().fnid) {
        default:
            M_unreachable("function kind not implemented");

        case m::Function::FN_UDF:
            M_unreachable("UDFs not yet supported");

        /*----- NULL check -------------------------------------------------------------------------------------------*/
        case m::Function::FN_ISNULL: {
            (*this)(*e.args[0]);
            std::visit(overloaded {
                [](std::monostate&&) -> void { M_unreachable("invalid expression"); },
                [this](PrimitiveExpr<char*> expr) -> void { set(_Bool(expr.is_nullptr())); },
                [this](auto &&expr) -> void { set(_Bool(expr.is_null())); },
            }, get());
            break;
        }

        /*----- Type cast --------------------------------------------------------------------------------------------*/
        case m::Function::FN_INT: {
            (*this)(*e.args[0]);
            std::visit(overloaded {
                [](std::monostate&&) -> void { M_unreachable("invalid expression"); },
                [this](auto &&expr) -> void requires requires { expr.template to<int32_t>(); } {
                    set(expr.template to<int32_t>());
                },
                [](auto&&) -> void { M_unreachable("illegal operation"); },
            }, get());
            break;
        }

        /*----- Aggregate functions ----------------------------------------------------------------------------------*/
        case m::Function::FN_COUNT:
        case m::Function::FN_MIN:
        case m::Function::FN_MAX:
        case m::Function::FN_SUM:
        case m::Function::FN_AVG: {
            /* Evaluate the argument for the function call. */
            (*this)(*e.args[0]);
            break;
        }
    }
}

void ExprCompiler::operator()(const QueryExpr &e)
{
    /* Search with fully qualified name. */
    Schema::Identifier id(e.alias(), Catalog::Get().pool("$res"));
    set(env_.get(id));
}

_Bool ExprCompiler::compile(const cnf::CNF &cnf)
{
    Var<_Bool> wasm_cnf; // to make it nullable
    Var<_Bool> wasm_clause; // to make it nullable

    bool wasm_cnf_empty = true;
    for (auto &clause : cnf) {
        bool wasm_clause_empty = true;
        for (auto &pred : clause) {
            /* Generate code for the literal of the predicate. */
            M_insist(pred.expr()->type()->is_boolean());
            _Bool compiled = compile<_Bool>(*pred.expr());
            _Bool wasm_pred = pred.negative() ? not compiled : compiled;
            /* Add the predicate to the clause with an `or`. */
            if (wasm_clause_empty) {
                wasm_clause = wasm_pred;
                wasm_clause_empty = false;
            } else {
                wasm_clause = wasm_clause or wasm_pred;
            }
        }
        /* Add the clause to the CNF with an `and`. */
        if (wasm_cnf_empty) {
            wasm_cnf = wasm_clause;
            wasm_cnf_empty = false;
        } else {
            wasm_cnf = wasm_cnf and wasm_clause;
        }
    }
    M_insist(not wasm_cnf_empty, "empty CNF?");

    return wasm_cnf;
}


/*======================================================================================================================
 * Environment
 *====================================================================================================================*/

M_LCOV_EXCL_START
void Environment::dump(std::ostream &out) const
{
    out << "WasmEnvironment\n` entries: { ";
    for (auto it = exprs_.begin(), end = exprs_.end(); it != end; ++it) {
        if (it != exprs_.begin()) out << ", ";
        out << it->first;
    }
    out << " }" << std::endl;
}

void Environment::dump() const { dump(std::cerr); }
M_LCOV_EXCL_STOP


/*======================================================================================================================
 * compile data layout
 *====================================================================================================================*/

namespace m {

namespace wasm {

/** Compiles the data layout \p layout containing tuples of schema \p layout_schema such that it sequentially
 * stores/loads tuples of schema \p tuple_schema starting at memory address \p base_address and tuple ID \p
 * initial_tuple_id.  The caller has to provide a variable \p tuple_id which must be initialized to \p
 * initial_tuple_id and will be incremented automatically after storing/loading each tuple (i.e. code for this will
 * be emitted at the end of the block returned as second element).
 *
 * Does not emit any code but returns three `wasm::Block`s containing code: the first one initializes all needed
 * variables, the second one stores/loads one tuple, and the third one advances to the next tuple. */
template<bool IsStore, VariableKind Kind>
std::tuple<Block, Block, Block>
compile_data_layout_sequential(const Schema &tuple_schema, Ptr<void> base_address, const storage::DataLayout &layout,
                               const Schema &layout_schema, Variable<uint32_t, Kind, false> &tuple_id,
                               uint32_t initial_tuple_id = 0)
{
    /** code blocks for pointer/mask initialization, stores/loads of values, and stride jumps for pointers / updates
     * of masks */
    Block inits("inits", false), stores("stores", false), loads("loads", false), jumps("jumps", false);
    ///> the values loaded for the entries in `tuple_schema`
    SQL_t values[tuple_schema.num_entries()];
    ///> the NULL information loaded for the entries in `tuple_schema`
    Bool *null_bits;
    if constexpr (not IsStore)
        null_bits = static_cast<Bool*>(alloca(sizeof(Bool) * tuple_schema.num_entries()));
    /** a map from bit offset (mod 8) and stride in bits to runtime pointer and mask; reset for each leaf; used to
     * share pointers between attributes of the same leaf that have equal stride and to share masks between attributes
     * of the same leaf that have equal offset (mod 8) */
    using key_t = std::pair<uint8_t, uint64_t>;
    struct value_t
    {
        Var<Ptr<void>> ptr;
        std::optional<Var<U32>> mask;
    };
    std::unordered_map<key_t, value_t> loading_context;

    auto &env = CodeGenContext::Get().env(); // the current codegen environment

    BLOCK_OPEN(inits) {
        Wasm_insist(tuple_id == initial_tuple_id, "initial value of tuple ID must be equal `initial_tuple_id`");
    }

    /*----- Check whether any of the entries in `tuple_schema` can be NULL, so that we need the NULL bitmap. -----*/
    const bool needs_null_bitmap = [&]() {
        for (auto &tuple_entry : tuple_schema) {
            M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
            if (tuple_entry.nullable()) return true; // found an entry in `tuple_schema` that can be NULL
        }
        return false; // no attribute in `schema` can be NULL
    }();
    bool has_null_bitmap = false; // indicates whether the data layout specifies a NULL bitmap

    /*----- Visit the data layout. -----*/
    layout.for_sibling_leaves([&](const std::vector<DataLayout::leaf_info_t> &leaves,
                                  const DataLayout::level_info_stack_t &levels, uint64_t inode_offset_in_bits)
    {
        M_insist(inode_offset_in_bits % 8 == 0, "inode offset must be byte aligned");

        /*----- Clear the per-leaf data structure. -----*/
        loading_context.clear();

        /*----- Remember whether and where we found the NULL bitmap. -----*/
        std::optional<Var<Ptr<void>>> null_bitmap_ptr;
        std::optional<Var<U32>> null_bitmap_mask;
        uint8_t null_bitmap_bit_offset;
        uint64_t null_bitmap_stride_in_bits;

        /*----- Compute additional initial INode offset in bits depending on the given initial tuple ID. -----*/
        auto current_tuple_id = initial_tuple_id;
        uint64_t additional_inode_offset_in_bits = 0;
        for (auto &level : levels) {
            const auto child_iter = current_tuple_id / level.num_tuples;
            current_tuple_id = current_tuple_id % level.num_tuples;
            additional_inode_offset_in_bits += child_iter * level.stride_in_bits;
        }

        /*----- Iterate over sibling leaves, i.e. leaf children of a common parent INode, to emit code. -----*/
        for (auto &leaf_info : leaves) {
            const uint8_t bit_offset  = (additional_inode_offset_in_bits + leaf_info.offset_in_bits) % 8;
            const int32_t byte_offset = (additional_inode_offset_in_bits + leaf_info.offset_in_bits) / 8;

            const uint8_t bit_stride  = leaf_info.stride_in_bits % 8; // need byte stride later for the stride jumps

            if (leaf_info.leaf.index() == layout_schema.num_entries()) { // NULL bitmap
                if (not needs_null_bitmap)
                    continue;

                M_insist(not has_null_bitmap, "at most one bitmap may be specified");
                has_null_bitmap = true;
                if (bit_stride) { // NULL bitmap with bit stride requires dynamic masking
                    null_bitmap_bit_offset = bit_offset;
                    null_bitmap_stride_in_bits = leaf_info.stride_in_bits;
                    BLOCK_OPEN(inits) {
                        /*----- Initialize pointer and mask. -----*/
                        null_bitmap_ptr = base_address.clone() + inode_offset_in_bits / 8 + byte_offset;
                        null_bitmap_mask = 1U << bit_offset;
                    }

                    /*----- Iterate over layout entries in *ascending* order. -----*/
                    std::size_t prev_layout_idx = 0; ///< remember the bit offset of the previously accessed NULL bit
                    for (std::size_t layout_idx = 0; layout_idx < layout_schema.num_entries(); ++layout_idx) {
                        auto &layout_entry = layout_schema[layout_idx];
                        if (layout_entry.nullable()) { // layout entry may be NULL
                            auto tuple_it = tuple_schema.find(layout_entry.id);
                            if (tuple_it == tuple_schema.end())
                                continue; // entry not contained in tuple schema
                            M_insist(prev_layout_idx == 0 or layout_idx > prev_layout_idx,
                                     "layout entries not processed in ascending order");
                            M_insist(*tuple_it->type == *layout_entry.type);
                            M_insist(tuple_it->nullable() == layout_entry.nullable());
                            const auto delta = layout_idx - prev_layout_idx;
                            const uint8_t bit_delta  = delta % 8;
                            const int32_t byte_delta = delta / 8;

                            auto advance_to_next_bit = [&]() {
                                if (bit_delta) {
                                    *null_bitmap_mask <<= bit_delta; // advance mask by `bit_delta`
                                    /* If the mask surpasses the first byte, advance pointer to the next byte... */
                                    *null_bitmap_ptr += (*null_bitmap_mask bitand 0xffU).eqz().to<int32_t>();
                                    /* ... and remove lowest byte from the mask. */
                                    *null_bitmap_mask = Select((*null_bitmap_mask bitand 0xffU).eqz(),
                                                               *null_bitmap_mask >> 8U, *null_bitmap_mask);
                                }
                                if (byte_delta)
                                    *null_bitmap_ptr += byte_delta; // advance pointer by `byte_delta`
                            };

                            if constexpr (IsStore) {
                                /*----- Store NULL bit depending on its type. -----*/
                                auto store = [&]<typename T>() {
                                    BLOCK_OPEN(stores) {
                                        advance_to_next_bit();

                                        auto [value, is_null] = env.get<T>(tuple_it->id).split(); // get value
                                        value.discard(); // handled at entry leaf
                                        setbit(null_bitmap_ptr->to<uint8_t*>(), is_null,
                                               null_bitmap_mask->to<uint8_t>()); // update bit
                                    }
                                };
                                visit(overloaded{
                                    [&](const Boolean&) { store.template operator()<_Bool>(); },
                                    [&](const Numeric &n) {
                                        switch (n.kind) {
                                            case Numeric::N_Int:
                                            case Numeric::N_Decimal:
                                                switch (n.size()) {
                                                    default: M_unreachable("invalid size");
                                                    case  8: store.template operator()<_I8 >(); break;
                                                    case 16: store.template operator()<_I16>(); break;
                                                    case 32: store.template operator()<_I32>(); break;
                                                    case 64: store.template operator()<_I64>(); break;
                                                }
                                                break;
                                            case Numeric::N_Float:
                                                if (n.size() <= 32)
                                                    store.template operator()<_Float>();
                                                else
                                                    store.template operator()<_Double>();
                                        }
                                    },
                                    [&](const CharacterSequence&) {
                                        BLOCK_OPEN(stores) {
                                            advance_to_next_bit();

                                            auto value = env.get<Ptr<Char>>(tuple_it->id); // get value
                                            setbit(null_bitmap_ptr->to<uint8_t*>(), value.is_nullptr(),
                                                   null_bitmap_mask->to<uint8_t>()); // update bit
                                        }
                                    },
                                    [&](const Date&) { store.template operator()<_I32>(); },
                                    [&](const DateTime&) { store.template operator()<_I64>(); },
                                    [](auto&&) { M_unreachable("invalid type"); },
                                }, *tuple_it->type);
                            } else {
                                const auto tuple_idx = std::distance(tuple_schema.begin(), tuple_it);
                                BLOCK_OPEN(loads) {
                                    advance_to_next_bit();

                                    U8 byte = *null_bitmap_ptr->to<uint8_t*>(); // load the byte
                                    Var<Bool> value(
                                        (byte bitand *null_bitmap_mask).to<bool>()
                                    ); // mask bit with dynamic mask
                                    new (&null_bits[tuple_idx]) Bool(value);
                                }
                            }

                            prev_layout_idx = layout_idx;
                        } else { // layout entry must not be NULL
#ifndef NDEBUG
                            if constexpr (IsStore) {
                                /*----- Check that value is also not NULL. -----*/
                                auto check = [&]<typename T>() {
                                    BLOCK_OPEN(stores) {
                                        Wasm_insist(env.get<T>(layout_entry.id).not_null(),
                                                    "value of non-nullable entry must not be NULL");
                                    }
                                };
                                visit(overloaded{
                                    [&](const Boolean&) { check.template operator()<_Bool>(); },
                                    [&](const Numeric &n) {
                                        switch (n.kind) {
                                            case Numeric::N_Int:
                                            case Numeric::N_Decimal:
                                                switch (n.size()) {
                                                    default: M_unreachable("invalid size");
                                                    case  8: check.template operator()<_I8 >(); break;
                                                    case 16: check.template operator()<_I16>(); break;
                                                    case 32: check.template operator()<_I32>(); break;
                                                    case 64: check.template operator()<_I64>(); break;
                                                }
                                                break;
                                            case Numeric::N_Float:
                                                if (n.size() <= 32)
                                                    check.template operator()<_Float>();
                                                else
                                                    check.template operator()<_Double>();
                                        }
                                    },
                                    [&](const CharacterSequence&) {
                                        Wasm_insist(not env.get<Ptr<Char>>(layout_entry.id).is_nullptr(),
                                                    "value of non-nullable entry must not be NULL");
                                    },
                                    [&](const Date&) { check.template operator()<_I32>(); },
                                    [&](const DateTime&) { check.template operator()<_I64>(); },
                                    [](auto&&) { M_unreachable("invalid type"); },
                                }, *layout_entry.type);
                            }
#endif
                        }
                    }

                    /*----- Final advancement of the pointer and mask to match the leaf's stride. -----*/
                    /* This is done here (and not together with the other stride jumps further below) since we only need
                     * to advance by `delta` bits since we already have advanced by `prev_layout_idx` bits. */
                    const auto delta = leaf_info.stride_in_bits - prev_layout_idx;
                    const uint8_t bit_delta  = delta % 8;
                    const int32_t byte_delta = delta / 8;
                    if (bit_delta) {
                        BLOCK_OPEN(jumps) {
                            *null_bitmap_mask <<= bit_delta; // advance mask by `bit_delta`
                            /* If the mask surpasses the first byte, advance pointer to the next byte... */
                            *null_bitmap_ptr += (*null_bitmap_mask bitand 0xffU).eqz().to<int32_t>();
                            /* ... and remove the lowest byte from the mask. */
                            *null_bitmap_mask = Select((*null_bitmap_mask bitand 0xffU).eqz(),
                                                       *null_bitmap_mask >> 8U, *null_bitmap_mask);
                        }
                    }
                    if (byte_delta) {
                        BLOCK_OPEN(jumps) {
                            *null_bitmap_ptr += byte_delta; // advance pointer
                        }
                    }
                } else { // NULL bitmap without bit stride can benefit from static masking of NULL bits
                    auto [it, inserted] = loading_context.try_emplace(key_t(bit_offset, leaf_info.stride_in_bits));
                    if (inserted) {
                        BLOCK_OPEN(inits) {
                            it->second.ptr = base_address.clone() + inode_offset_in_bits / 8;
                        }
                    }
                    auto &ptr = it->second.ptr;

                    /*----- For each tuple entry that can be NULL, create a store/load with static offset and mask. --*/
                    for (std::size_t tuple_idx = 0; tuple_idx != tuple_schema.num_entries(); ++tuple_idx) {
                        auto &tuple_entry = tuple_schema[tuple_idx];
                        M_insist(*tuple_entry.type == *layout_schema[tuple_entry.id].second.type);
                        M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                        if (tuple_entry.nullable()) { // entry may be NULL
                            const auto& [layout_idx, layout_entry] = layout_schema[tuple_entry.id];
                            const uint8_t bit_offset =
                                (additional_inode_offset_in_bits + leaf_info.offset_in_bits + layout_idx) % 8;
                            const int32_t byte_offset =
                                (additional_inode_offset_in_bits + leaf_info.offset_in_bits + layout_idx) / 8;
                            if constexpr (IsStore) {
                                /*----- Store NULL bit depending on its type. -----*/
                                auto store = [&]<typename T>() {
                                    BLOCK_OPEN(stores) {
                                        auto [value, is_null] = env.get<T>(tuple_entry.id).split(); // get value
                                        value.discard(); // handled at entry leaf
                                        Ptr<U8> byte_ptr =
                                            (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                                        setbit<U8>(byte_ptr, is_null, bit_offset); // update bit
                                    }
                                };
                                visit(overloaded{
                                    [&](const Boolean&) { store.template operator()<_Bool>(); },
                                    [&](const Numeric &n) {
                                        switch (n.kind) {
                                            case Numeric::N_Int:
                                            case Numeric::N_Decimal:
                                                switch (n.size()) {
                                                    default: M_unreachable("invalid size");
                                                    case  8: store.template operator()<_I8 >(); break;
                                                    case 16: store.template operator()<_I16>(); break;
                                                    case 32: store.template operator()<_I32>(); break;
                                                    case 64: store.template operator()<_I64>(); break;
                                                }
                                                break;
                                            case Numeric::N_Float:
                                                if (n.size() <= 32)
                                                    store.template operator()<_Float>();
                                                else
                                                    store.template operator()<_Double>();
                                        }
                                    },
                                    [&](const CharacterSequence&) {
                                        BLOCK_OPEN(stores) {
                                            auto value = env.get<Ptr<Char>>(tuple_entry.id); // get value
                                            Ptr<U8> byte_ptr =
                                                (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                                            setbit<U8>(byte_ptr, value.is_nullptr(), bit_offset); // update bit
                                        }
                                    },
                                    [&](const Date&) { store.template operator()<_I32>(); },
                                    [&](const DateTime&) { store.template operator()<_I64>(); },
                                    [](auto&&) { M_unreachable("invalid type"); },
                                }, *tuple_entry.type);
                            } else {
                                /*----- Load NULL bit. -----*/
                                BLOCK_OPEN(loads) {
                                    U8 byte = *(ptr + byte_offset).template to<uint8_t*>(); // load the byte
                                    const uint8_t static_mask = 1U << bit_offset;
                                    Var<Bool> value((byte bitand static_mask).to<bool>()); // mask bit with static mask
                                    new (&null_bits[tuple_idx]) Bool(value);
                                }
                            }
                        } else { // entry must not be NULL
#ifndef NDEBUG
                            if constexpr (IsStore) {
                                /*----- Check that value is also not NULL. -----*/
                                auto check = [&]<typename T>() {
                                    BLOCK_OPEN(stores) {
                                        Wasm_insist(env.get<T>(tuple_entry.id).not_null(),
                                                    "value of non-nullable entry must not be NULL");
                                    }
                                };
                                visit(overloaded{
                                    [&](const Boolean&) { check.template operator()<_Bool>(); },
                                    [&](const Numeric &n) {
                                        switch (n.kind) {
                                            case Numeric::N_Int:
                                            case Numeric::N_Decimal:
                                                switch (n.size()) {
                                                    default: M_unreachable("invalid size");
                                                    case  8: check.template operator()<_I8 >(); break;
                                                    case 16: check.template operator()<_I16>(); break;
                                                    case 32: check.template operator()<_I32>(); break;
                                                    case 64: check.template operator()<_I64>(); break;
                                                }
                                                break;
                                            case Numeric::N_Float:
                                                if (n.size() <= 32)
                                                    check.template operator()<_Float>();
                                                else
                                                    check.template operator()<_Double>();
                                        }
                                    },
                                    [&](const CharacterSequence&) {
                                        BLOCK_OPEN(stores) {
                                            Wasm_insist(not env.get<Ptr<Char>>(tuple_entry.id).is_nullptr(),
                                                        "value of non-nullable entry must not be NULL");
                                        }
                                    },
                                    [&](const Date&) { check.template operator()<_I32>(); },
                                    [&](const DateTime&) { check.template operator()<_I64>(); },
                                    [](auto&&) { M_unreachable("invalid type"); },
                                }, *tuple_entry.type);
                            }
#endif
                        }
                    }
                }
            } else { // regular entry
                auto &layout_entry = layout_schema[leaf_info.leaf.index()];
                M_insist(*layout_entry.type == *leaf_info.leaf.type());
                auto tuple_it = tuple_schema.find(layout_entry.id);
                if (tuple_it == tuple_schema.end())
                    continue; // entry not contained in tuple schema
                M_insist(*tuple_it->type == *layout_entry.type);
                const auto tuple_idx = std::distance(tuple_schema.begin(), tuple_it);

                auto [it, inserted] = loading_context.try_emplace(key_t(bit_offset, leaf_info.stride_in_bits));
                if (inserted) {
                    BLOCK_OPEN(inits) {
                        it->second.ptr = base_address.clone() + inode_offset_in_bits / 8;
                    }
                }
                auto &ptr = it->second.ptr;

                if (bit_stride) { // entry with bit stride requires dynamic masking
                    M_insist(tuple_it->type->is_boolean(),
                             "leaf bit stride currently only for `Boolean` supported");

                    M_insist(inserted == not it->second.mask.has_value());
                    if (inserted) {
                        BLOCK_OPEN(inits) {
                            it->second.mask = 1U << bit_offset; // init mask
                        }
                    }
                    Var<U32> &mask = *it->second.mask;

                    if constexpr (IsStore) {
                        /*----- Store value. -----*/
                        BLOCK_OPEN(stores) {
                            auto [value, is_null] = env.get<_Bool>(tuple_it->id).split(); // get value
                            is_null.discard(); // handled at NULL bitmap leaf
                            Ptr<U8> byte_ptr = (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                            setbit(byte_ptr, value, mask.to<uint8_t>()); // update bit
                        }
                    } else {
                        /*----- Load value. -----*/
                        BLOCK_OPEN(loads) {
                            U8 byte = *(ptr + byte_offset).template to<uint8_t*>(); // load byte
                            Var<Bool> value((byte bitand mask).to<bool>()); // mask bit with dynamic mask
                            values[tuple_idx].emplace<_Bool>(value);
                        }
                    }
                } else { // entry without bit stride; if masking is required, we can use a static mask
                    /*----- Store value depending on its type. -----*/
                    auto store = [&]<typename T>() {
                        using type = typename T::type;
                        M_insist(bit_offset == 0,
                                 "leaf offset of `Numeric`, `Date`, or `DateTime` must be byte aligned");
                        BLOCK_OPEN(stores) {
                            auto [value, is_null] = env.get<T>(tuple_it->id).split();
                            is_null.discard(); // handled at NULL bitmap leaf
                            *(ptr + byte_offset).template to<type*>() = value;
                        }
                    };
                    /*----- Load value depending on its type. -----*/
                    auto load = [&]<typename T>() {
                        using type = typename T::type;
                        M_insist(bit_offset == 0,
                                 "leaf offset of `Numeric`, `Date`, or `DateTime` must be byte aligned");
                        BLOCK_OPEN(loads) {
                            Var<PrimitiveExpr<type>> value(*(ptr + byte_offset).template to<type*>());
                            values[tuple_idx].emplace<T>(value);
                        }
                    };
                    /*----- Select call target (store or load) and visit attribute type. -----*/
#define CALL(TYPE) if constexpr (IsStore) store.template operator()<TYPE>(); else load.template operator()<TYPE>()
                    visit(overloaded{
                        [&](const Boolean&) {
                            if constexpr (IsStore) {
                                /*----- Store value. -----*/
                                BLOCK_OPEN(stores) {
                                    auto [value, is_null] = env.get<_Bool>(tuple_it->id).split(); // get value
                                    is_null.discard(); // handled at NULL bitmap leaf
                                    Ptr<U8> byte_ptr =
                                        (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                                    setbit<U8>(byte_ptr, value, bit_offset); // update bit
                                }
                            } else {
                                /*----- Load value. -----*/
                                BLOCK_OPEN(loads) {
                                    /* TODO: load byte once, create values with respective mask */
                                    U8 byte = *(ptr + byte_offset).template to<uint8_t*>(); // load byte
                                    const uint8_t static_mask = 1U << bit_offset;
                                    Var<Bool> value((byte bitand static_mask).to<bool>()); // mask bit with static mask
                                    values[tuple_idx].emplace<_Bool>(value);
                                }
                            }
                        },
                        [&](const Numeric &n) {
                            switch (n.kind) {
                                case Numeric::N_Int:
                                case Numeric::N_Decimal:
                                    switch (n.size()) {
                                        default: M_unreachable("invalid size");
                                        case  8: CALL(_I8 ); break;
                                        case 16: CALL(_I16); break;
                                        case 32: CALL(_I32); break;
                                        case 64: CALL(_I64); break;
                                    }
                                    break;
                                case Numeric::N_Float:
                                    if (n.size() <= 32)
                                        CALL(_Float);
                                    else
                                        CALL(_Double);
                            }
                        },
                        [&](const CharacterSequence &cs) {
                            M_insist(bit_offset == 0, "leaf offset of `CharacterSequence` must be byte aligned");
                            if constexpr (IsStore) {
                                /*----- Store value. -----*/
                                BLOCK_OPEN(stores) {
                                    auto value = env.get<Ptr<Char>>(tuple_it->id);
                                    IF (not value.clone().is_nullptr()) {
                                        Ptr<Char> address((ptr + byte_offset).template to<char*>());
                                        strncpy(address, value, U32(cs.size() / 8)).discard();
                                    };
                                }
                            } else {
                                /*----- Load value. -----*/
                                BLOCK_OPEN(loads) {
                                    Ptr<Char> address((ptr + byte_offset).template to<char*>());
                                    values[tuple_idx].emplace<Ptr<Char>>(address);
                                }
                            }
                        },
                        [&](const Date&) { CALL(_I32); },
                        [&](const DateTime&) { CALL(_I64); },
                        [](auto&&) { M_unreachable("invalid type"); },
                    }, *tuple_it->type);
#undef CALL
                }
            }
        }

        /*----- Recursive lambda to emit stride jumps by processing path from leaves (excluding) to the root. -----*/
        auto emit_stride_jumps = [&](decltype(levels.crbegin()) curr, const decltype(levels.crend()) end) -> void {
            auto rec = [&](decltype(levels.crbegin()) curr, const decltype(levels.crend()) end, auto rec) -> void {
                if (curr == end) return;

                const auto inner = std::prev(curr); // the child INode of `curr`
                M_insist(curr->num_tuples % inner->num_tuples == 0, "curr must be whole multiple of inner");

                /*----- Compute remaining stride for this level. -----*/
                const auto num_repetition_inner = curr->num_tuples / inner->num_tuples;
                const auto stride_remaining_in_bits = curr->stride_in_bits -
                                                      num_repetition_inner * inner->stride_in_bits;
                M_insist(stride_remaining_in_bits % 8 == 0,
                         "remaining stride of INodes must be whole multiple of a byte");

                /*----- If there is a remaining stride for this level, emit conditional stride jump. -----*/
                if (const int32_t remaining_stride_in_bytes = stride_remaining_in_bits / 8) [[likely]] {
                    Bool cond_mod = (tuple_id % uint32_t(curr->num_tuples)).eqz();
                    Bool cond_and = (tuple_id bitand uint32_t(curr->num_tuples - 1U)).eqz();
                    Bool cond = is_pow_2(curr->num_tuples) ? cond_and : cond_mod; // select implementation to use...
                    (is_pow_2(curr->num_tuples) ? cond_mod : cond_and).discard(); // ... and discard the other

                    /*----- Emit conditional stride jumps. -----*/
                    IF (cond) {
                        for (auto& [_, value] : loading_context)
                            value.ptr += remaining_stride_in_bytes; // emit stride jump
                        if (null_bitmap_ptr)
                            *null_bitmap_ptr += remaining_stride_in_bytes; // emit stride jump

                        /*----- Recurse within IF. -----*/
                        rec(std::next(curr), end, rec);
                    };
                } else {
                    /*----- Recurse without IF. -----*/
                    rec(std::next(curr), end, rec);
                }

            };
            rec(curr, end, rec);
        };

        /*----- Process path from DataLayout leaves to the root to emit stride jumps. -----*/
        BLOCK_OPEN(jumps) {
            /*----- Emit the per-leaf stride jumps, i.e. from one instance of the leaf to the next. -----*/
            for (auto& [key, value] : loading_context) {
                const uint8_t bit_stride  = key.second % 8;
                const int32_t byte_stride = key.second / 8;
                if (bit_stride) {
                    M_insist(value.mask.has_value());
                    *value.mask <<= bit_stride; // advance mask by `bit_stride`
                    /* If the mask surpasses the first byte, advance pointer to the next byte... */
                    value.ptr += (*value.mask bitand 0xffU).eqz().template to<int32_t>();
                    /* ... and remove the lowest byte from the mask. */
                    *value.mask = Select((*value.mask bitand 0xffU).eqz(), *value.mask >> 8U, *value.mask);
                }
                if (byte_stride)
                    value.ptr += byte_stride; // advance pointer
            }
            /* Omit the leaf stride jump for the NULL bitmap as it is already done together with the loading. */

            if (not levels.empty()) {
                /*----- Emit the stride jumps between each leaf to the beginning of the parent INode. -----*/
                Block lowest_inode_jumps(false);
                for (auto& [key, value] : loading_context) {
                    M_insist(levels.back().stride_in_bits % 8 == 0,
                             "stride of INodes must be multiples of a whole byte");
                    const auto stride_remaining_in_bits = levels.back().stride_in_bits -
                                                          levels.back().num_tuples * key.second;
                    const uint8_t remaining_bit_stride  = stride_remaining_in_bits % 8;
                    const int32_t remaining_byte_stride = stride_remaining_in_bits / 8;
                    if (remaining_bit_stride) [[likely]] {
                        M_insist(value.mask.has_value());
                        BLOCK_OPEN(lowest_inode_jumps) {
                            const uint8_t end_bit_offset = (key.first + levels.back().num_tuples * key.second) % 8;
                            M_insist(end_bit_offset != key.first);
                            /* Reset the mask to initial bit offset... */
                            *value.mask = 1U << key.first;
                            /* ... and advance pointer to next byte if resetting of the mask surpasses the current byte. */
                            value.ptr += int32_t(end_bit_offset > key.first);
                        }
                    }
                    if (remaining_byte_stride) [[likely]] {
                        BLOCK_OPEN(lowest_inode_jumps) {
                            value.ptr += remaining_byte_stride; // advance pointer
                        }
                    }
                }
                if (null_bitmap_ptr) {
                    M_insist(bool(null_bitmap_mask));
                    M_insist(levels.back().stride_in_bits % 8 == 0,
                             "stride of INodes must be multiples of a whole byte");
                    const auto stride_remaining_in_bits = levels.back().stride_in_bits -
                                                          levels.back().num_tuples * null_bitmap_stride_in_bits;
                    const uint8_t remaining_bit_stride  = stride_remaining_in_bits % 8;
                    const int32_t remaining_byte_stride = stride_remaining_in_bits / 8;
                    if (remaining_bit_stride) [[likely]] {
                        BLOCK_OPEN(lowest_inode_jumps) {
                            const uint8_t end_bit_offset =
                                (null_bitmap_bit_offset + levels.back().num_tuples * null_bitmap_stride_in_bits) % 8;
                            M_insist(end_bit_offset != null_bitmap_bit_offset);
                            /* Reset the mask to initial bit offset... */
                            *null_bitmap_mask = 1U << null_bitmap_bit_offset;
                            /* ... and advance pointer to next byte if resetting of the mask surpasses the current byte. */
                            *null_bitmap_ptr += int32_t(end_bit_offset > null_bitmap_bit_offset);
                        }
                    }
                    if (remaining_byte_stride) [[likely]] {
                        BLOCK_OPEN(lowest_inode_jumps) {
                            *null_bitmap_ptr += remaining_byte_stride; // advance pointer
                        }
                    }
                }

                /*----- Emit the stride jumps between all INodes starting at the parent of leaves to the root. -----*/
                if (not lowest_inode_jumps.empty()) [[likely]] {
                    Bool cond_mod = (tuple_id % uint32_t(levels.back().num_tuples)).eqz();
                    Bool cond_and = (tuple_id bitand uint32_t(levels.back().num_tuples - 1U)).eqz();
                    Bool cond = is_pow_2(levels.back().num_tuples) ? cond_and : cond_mod; // select implementation to use...
                    (is_pow_2(levels.back().num_tuples) ? cond_mod : cond_and).discard(); // ... and discard the other

                    /*----- Emit conditional stride jumps from outermost Block. -----*/
                    IF (cond) {
                        lowest_inode_jumps.attach_to_current();

                        /*----- Recurse within IF. -----*/
                        emit_stride_jumps(std::next(levels.crbegin()), levels.crend());
                    };
                } else {
                    /*----- Recurse without outermost IF block. -----*/
                    emit_stride_jumps(std::next(levels.crbegin()), levels.crend());
                }
            }
        }
    });

    if constexpr (not IsStore) {
        /*----- Combine actual values and possible NULL bits to a new `SQL_t` and add this to the environment. -----*/
        for (std::size_t idx = 0; idx != tuple_schema.num_entries(); ++idx) {
            auto &tuple_entry = tuple_schema[idx];
            std::visit(overloaded{
                [&]<typename T>(Expr<T> value) {
                    BLOCK_OPEN(loads) {
                        M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                        if (has_null_bitmap and tuple_entry.nullable()) {
                            Expr<T> combined(value.insist_not_null(), null_bits[idx]);
                            env.add(tuple_entry.id, combined);
                        } else {
                            env.add(tuple_entry.id, value);
                        }
                    }
                },
                [&](Ptr<Char> value) {
                    BLOCK_OPEN(loads) {
                        M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                        if (has_null_bitmap and tuple_entry.nullable()) {
                            Var<Ptr<Char>> combined(Select(null_bits[idx], Ptr<Char>::Nullptr(), value));
                            env.add(tuple_entry.id, combined);
                        } else {
                            Var<Ptr<Char>> _value(value); // introduce variable s.t. uses only load from it
                            env.add(tuple_entry.id, _value);
                        }
                    }
                },
                [](std::monostate) { M_unreachable("value must be loaded beforehand"); },
            }, values[idx]);
        }
    }

    /*----- Increment tuple ID after storing/loading one tuple. -----*/
    if constexpr (IsStore) {
        BLOCK_OPEN(stores) {
            tuple_id += 1U;
        }
    } else {
        BLOCK_OPEN(loads) {
            tuple_id += 1U;
        }
    }

    if constexpr (not IsStore) {
        /*----- Destroy created NULL bits. -----*/
        for (std::size_t idx = 0; idx != tuple_schema.num_entries(); ++idx) {
            M_insist(tuple_schema[idx].nullable() == layout_schema[tuple_schema[idx].id].second.nullable());
            if (has_null_bitmap and tuple_schema[idx].nullable())
                null_bits[idx].~Bool();
        }
    }
    base_address.discard(); // discard base address (as it was always cloned)

    return { std::move(inits), IsStore ? std::move(stores) : std::move(loads), std::move(jumps) };
}

}

}

template<VariableKind Kind>
std::tuple<m::wasm::Block, m::wasm::Block, m::wasm::Block>
m::wasm::compile_store_sequential(const Schema &tuple_schema, Ptr<void> base_address, const storage::DataLayout &layout,
                                  const Schema &layout_schema, Variable<uint32_t, Kind, false> &tuple_id,
                                  uint32_t initial_tuple_id)
{
    return compile_data_layout_sequential<true>(tuple_schema, base_address, layout, layout_schema, tuple_id,
                                                initial_tuple_id);
}

template<VariableKind Kind>
std::tuple<m::wasm::Block, m::wasm::Block, m::wasm::Block>
m::wasm::compile_load_sequential(const Schema &tuple_schema, Ptr<void> base_address, const storage::DataLayout &layout,
                                 const Schema &layout_schema, Variable<uint32_t, Kind, false> &tuple_id,
                                 uint32_t initial_tuple_id)
{
    return compile_data_layout_sequential<false>(tuple_schema, base_address, layout, layout_schema, tuple_id,
                                                 initial_tuple_id);
}

namespace m {

namespace wasm {

/** Compiles the data layout \p layout starting at memory address \p base_address and containing tuples of schema
 * \p layout_schema such that it stores/loads the single tuple with schema \p tuple_schema and ID \p tuple_id.
 *
 * If \tparam IsStore, emits the storing code into the current block, otherwise, emits the loading code into the
 * current block and adds the loaded values into the current environment. */
template<bool IsStore>
void compile_data_layout_point_access(const Schema &tuple_schema, Ptr<void> base_address,
                                      const storage::DataLayout &layout, const Schema &layout_schema, U32 tuple_id)
{
    ///> the values loaded for the entries in `tuple_schema`
    SQL_t values[tuple_schema.num_entries()];
    ///> the NULL information loaded for the entries in `tuple_schema`
    Bool *null_bits;
    if constexpr (not IsStore)
        null_bits = static_cast<Bool*>(alloca(sizeof(Bool) * tuple_schema.num_entries()));

    auto &env = CodeGenContext::Get().env(); // the current codegen environment

    /*----- Check whether any of the entries in `tuple_schema` can be NULL, so that we need the NULL bitmap. -----*/
    const bool needs_null_bitmap = [&]() {
        for (auto &tuple_entry : tuple_schema) {
            M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
            if (tuple_entry.nullable()) return true; // found an entry in `tuple_schema` that can be NULL
        }
        return false; // no attribute in `schema` can be NULL
    }();
    bool has_null_bitmap = false; // indicates whether the data layout specifies a NULL bitmap

    /*----- Visit the data layout. -----*/
    layout.for_sibling_leaves([&](const std::vector<DataLayout::leaf_info_t> &leaves,
                                  const DataLayout::level_info_stack_t &levels, uint64_t inode_offset_in_bits)
    {
        M_insist(inode_offset_in_bits % 8 == 0, "inode offset must be byte aligned");

        /*----- Compute additional initial INode offset in bits depending on the given initial tuple ID. -----*/
        auto compute_additional_offset = [&](U32 tuple_id) -> U64 {
            auto rec = [&](U32 curr_tuple_id, decltype(levels.cbegin()) curr, const decltype(levels.cend()) end,
                           auto rec) -> U64
            {
                if (curr == end) {
                    curr_tuple_id.discard();
                    return U64(0);
                }

                if (is_pow_2(curr->num_tuples)) {
                    U32 child_iter = curr_tuple_id.clone() >> uint32_t(__builtin_ctzl(curr->num_tuples));
                    U32 inner_tuple_id = curr_tuple_id bitand uint32_t(curr->num_tuples - 1U);
                    U64 offset_in_bits = child_iter * curr->stride_in_bits;
                    return offset_in_bits + rec(inner_tuple_id, std::next(curr), end, rec);
                } else {
                    U32 child_iter = curr_tuple_id.clone() / uint32_t(curr->num_tuples);
                    U32 inner_tuple_id = curr_tuple_id % uint32_t(curr->num_tuples);
                    U64 offset_in_bits = child_iter * curr->stride_in_bits;
                    return offset_in_bits + rec(inner_tuple_id, std::next(curr), end, rec);
                }
            };
            return rec(tuple_id, levels.cbegin(), levels.cend(), rec);
        };
        Var<U64> additional_inode_offset_in_bits(compute_additional_offset(tuple_id));

        /*----- Iterate over sibling leaves, i.e. leaf children of a common parent INode, to emit code. -----*/
        for (auto &leaf_info : leaves) {
            if (leaf_info.leaf.index() == layout_schema.num_entries()) { // NULL bitmap
                if (not needs_null_bitmap)
                    continue;

                M_insist(not has_null_bitmap, "at most one bitmap may be specified");
                has_null_bitmap = true;

                Var<Ptr<void>> ptr(base_address.clone() + inode_offset_in_bits / 8); // pointer to NULL bitmap

                /*----- For each tuple entry that can be NULL, create a store/load with offset and mask. --*/
                for (std::size_t tuple_idx = 0; tuple_idx != tuple_schema.num_entries(); ++tuple_idx) {
                    auto &tuple_entry = tuple_schema[tuple_idx];
                    M_insist(*tuple_entry.type == *layout_schema[tuple_entry.id].second.type);
                    M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                    if (tuple_entry.nullable()) { // entry may be NULL
                        const auto& [layout_idx, layout_entry] = layout_schema[tuple_entry.id];
                        auto offset_in_bits = additional_inode_offset_in_bits + (leaf_info.offset_in_bits + layout_idx);
                        U8  bit_offset  = (offset_in_bits.clone() bitand uint64_t(7)).to<uint8_t>(); // mod 8
                        I32 byte_offset = (offset_in_bits >> uint64_t(3)).make_signed().to<int32_t>(); // div 8
                        if constexpr (IsStore) {
                            /*----- Store NULL bit depending on its type. -----*/
                            auto store = [&]<typename T>() {
                                auto [value, is_null] = env.get<T>(tuple_entry.id).split(); // get value
                                value.discard(); // handled at entry leaf
                                Ptr<U8> byte_ptr = (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                                setbit<U8>(byte_ptr, is_null, uint8_t(1) << bit_offset); // update bit
                            };
                            visit(overloaded{
                                [&](const Boolean&) { store.template operator()<_Bool>(); },
                                [&](const Numeric &n) {
                                    switch (n.kind) {
                                        case Numeric::N_Int:
                                        case Numeric::N_Decimal:
                                            switch (n.size()) {
                                                default: M_unreachable("invalid size");
                                                case  8: store.template operator()<_I8 >(); break;
                                                case 16: store.template operator()<_I16>(); break;
                                                case 32: store.template operator()<_I32>(); break;
                                                case 64: store.template operator()<_I64>(); break;
                                            }
                                            break;
                                        case Numeric::N_Float:
                                            if (n.size() <= 32)
                                                store.template operator()<_Float>();
                                            else
                                                store.template operator()<_Double>();
                                    }
                                },
                                [&](const CharacterSequence&) {
                                    auto value = env.get<Ptr<Char>>(tuple_entry.id); // get value
                                    Ptr<U8> byte_ptr =
                                        (ptr + byte_offset).template to<uint8_t*>(); // compute byte address
                                    setbit<U8>(byte_ptr, value.is_nullptr(), uint8_t(1) << bit_offset); // update bit
                                },
                                [&](const Date&) { store.template operator()<_I32>(); },
                                [&](const DateTime&) { store.template operator()<_I64>(); },
                                [](auto&&) { M_unreachable("invalid type"); },
                            }, *tuple_entry.type);
                        } else {
                            /*----- Load NULL bit. -----*/
                            U8 byte = *(ptr + byte_offset).template to<uint8_t*>(); // load the byte
                            Var<Bool> value((byte bitand (uint8_t(1) << bit_offset)).to<bool>()); // mask bit
                            new (&null_bits[tuple_idx]) Bool(value);
                        }
                    } else { // entry must not be NULL
#ifndef NDEBUG
                        if constexpr (IsStore) {
                            /*----- Check that value is also not NULL. -----*/
                            auto check = [&]<typename T>() {
                                Wasm_insist(env.get<T>(tuple_entry.id).not_null(),
                                            "value of non-nullable entry must not be NULL");
                            };
                            visit(overloaded{
                                [&](const Boolean&) { check.template operator()<_Bool>(); },
                                [&](const Numeric &n) {
                                    switch (n.kind) {
                                        case Numeric::N_Int:
                                        case Numeric::N_Decimal:
                                            switch (n.size()) {
                                                default: M_unreachable("invalid size");
                                                case  8: check.template operator()<_I8 >(); break;
                                                case 16: check.template operator()<_I16>(); break;
                                                case 32: check.template operator()<_I32>(); break;
                                                case 64: check.template operator()<_I64>(); break;
                                            }
                                            break;
                                        case Numeric::N_Float:
                                            if (n.size() <= 32)
                                                check.template operator()<_Float>();
                                            else
                                                check.template operator()<_Double>();
                                    }
                                },
                                [&](const CharacterSequence&) {
                                    Wasm_insist(not env.get<Ptr<Char>>(tuple_entry.id).is_nullptr(),
                                                "value of non-nullable entry must not be NULL");
                                },
                                [&](const Date&) { check.template operator()<_I32>(); },
                                [&](const DateTime&) { check.template operator()<_I64>(); },
                                [](auto&&) { M_unreachable("invalid type"); },
                            }, *tuple_entry.type);
                        }
#endif
                    }
                }
            } else { // regular entry
                auto &layout_entry = layout_schema[leaf_info.leaf.index()];
                M_insist(*layout_entry.type == *leaf_info.leaf.type());
                auto tuple_it = tuple_schema.find(layout_entry.id);
                if (tuple_it == tuple_schema.end())
                    continue; // entry not contained in tuple schema
                M_insist(*tuple_it->type == *layout_entry.type);
                const auto tuple_idx = std::distance(tuple_schema.begin(), tuple_it);

                auto offset_in_bits = additional_inode_offset_in_bits + leaf_info.offset_in_bits;
                U8  bit_offset  = (offset_in_bits.clone() bitand uint64_t(7)).to<uint8_t>(); // mod 8
                I32 byte_offset = (offset_in_bits >> uint64_t(3)).make_signed().to<int32_t>(); // div 8

                Ptr<void> ptr = base_address.clone() + byte_offset + inode_offset_in_bits / 8; // pointer to entry

                /*----- Store value depending on its type. -----*/
                auto store = [&]<typename T>() {
                    using type = typename T::type;
                    Wasm_insist(bit_offset == 0U,
                                "leaf offset of `Numeric`, `Date`, or `DateTime` must be byte aligned");
                    auto [value, is_null] = env.get<T>(tuple_it->id).split();
                    is_null.discard(); // handled at NULL bitmap leaf
                    *ptr.template to<type*>() = value;
                };
                /*----- Load value depending on its type. -----*/
                auto load = [&]<typename T>() {
                    using type = typename T::type;
                    Wasm_insist(bit_offset == 0U,
                                "leaf offset of `Numeric`, `Date`, or `DateTime` must be byte aligned");
                    Var<PrimitiveExpr<type>> value(*ptr.template to<type*>());
                    values[tuple_idx].emplace<T>(value);
                };
                /*----- Select call target (store or load) and visit attribute type. -----*/
#define CALL(TYPE) if constexpr (IsStore) store.template operator()<TYPE>(); else load.template operator()<TYPE>()
                visit(overloaded{
                    [&](const Boolean&) {
                        if constexpr (IsStore) {
                            /*----- Store value. -----*/
                            auto [value, is_null] = env.get<_Bool>(tuple_it->id).split(); // get value
                            is_null.discard(); // handled at NULL bitmap leaf
                            setbit<U8>(ptr.template to<uint8_t*>(), value, uint8_t(1) << bit_offset); // update bit
                        } else {
                            /*----- Load value. -----*/
                            /* TODO: load byte once, create values with respective mask */
                            U8 byte = *ptr.template to<uint8_t*>(); // load byte
                            Var<Bool> value((byte bitand (uint8_t(1) << bit_offset)).to<bool>()); // mask bit
                            values[tuple_idx].emplace<_Bool>(value);
                        }
                    },
                    [&](const Numeric &n) {
                        switch (n.kind) {
                            case Numeric::N_Int:
                            case Numeric::N_Decimal:
                                switch (n.size()) {
                                    default: M_unreachable("invalid size");
                                    case  8: CALL(_I8 ); break;
                                    case 16: CALL(_I16); break;
                                    case 32: CALL(_I32); break;
                                    case 64: CALL(_I64); break;
                                }
                                break;
                            case Numeric::N_Float:
                                if (n.size() <= 32)
                                    CALL(_Float);
                                else
                                    CALL(_Double);
                        }
                    },
                    [&](const CharacterSequence &cs) {
                        Wasm_insist(bit_offset == 0U, "leaf offset of `CharacterSequence` must be byte aligned");
                        if constexpr (IsStore) {
                            /*----- Store value. -----*/
                            auto value = env.get<Ptr<Char>>(tuple_it->id);
                            IF (not value.clone().is_nullptr()) {
                                strncpy(ptr.template to<char*>(), value, U32(cs.size() / 8)).discard();
                            };
                        } else {
                            /*----- Load value. -----*/
                            values[tuple_idx].emplace<Ptr<Char>>(ptr.template to<char*>());
                        }
                    },
                    [&](const Date&) { CALL(_I32); },
                    [&](const DateTime&) { CALL(_I64); },
                    [](auto&&) { M_unreachable("invalid type"); },
                }, *tuple_it->type);
#undef CALL
            }
        }
    });

    if constexpr (not IsStore) {
        /*----- Combine actual values and possible NULL bits to a new `SQL_t` and add this to the environment. -----*/
        for (std::size_t idx = 0; idx != tuple_schema.num_entries(); ++idx) {
            auto &tuple_entry = tuple_schema[idx];
            std::visit(overloaded{
                [&]<typename T>(Expr<T> value) {
                    M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                    if (has_null_bitmap and tuple_entry.nullable()) {
                        Expr<T> combined(value.insist_not_null(), null_bits[idx]);
                        env.add(tuple_entry.id, combined);
                    } else {
                        env.add(tuple_entry.id, value);
                    }
                },
                [&](Ptr<Char> value) {
                    M_insist(tuple_entry.nullable() == layout_schema[tuple_entry.id].second.nullable());
                    if (has_null_bitmap and tuple_entry.nullable()) {
                        Var<Ptr<Char>> combined(Select(null_bits[idx], Ptr<Char>::Nullptr(), value));
                        env.add(tuple_entry.id, combined);
                    } else {
                        Var<Ptr<Char>> _value(value); // introduce variable s.t. uses only load from it
                        env.add(tuple_entry.id, _value);
                    }
                },
                [](std::monostate) { M_unreachable("value must be loaded beforehand"); },
            }, values[idx]);
        }
    }

    if constexpr (not IsStore) {
        /*----- Destroy created NULL bits. -----*/
        for (std::size_t idx = 0; idx != tuple_schema.num_entries(); ++idx) {
            M_insist(tuple_schema[idx].nullable() == layout_schema[tuple_schema[idx].id].second.nullable());
            if (has_null_bitmap and tuple_schema[idx].nullable())
                null_bits[idx].~Bool();
        }
    }
    base_address.discard(); // discard base address (as it was always cloned)
}

}

}

void m::wasm::compile_store_point_access(const Schema &tuple_schema, Ptr<void> base_address, const DataLayout &layout,
                                         const Schema &layout_schema, U32 tuple_id)
{
    return compile_data_layout_point_access<true>(tuple_schema, base_address, layout, layout_schema, tuple_id);
}

void m::wasm::compile_load_point_access(const Schema &tuple_schema, Ptr<void> base_address, const DataLayout &layout,
                                        const Schema &layout_schema, U32 tuple_id)
{
    return compile_data_layout_point_access<false>(tuple_schema, base_address, layout, layout_schema, tuple_id);
}


/*======================================================================================================================
 * Buffer
 *====================================================================================================================*/

template<bool IsGlobal>
Buffer<IsGlobal>::Buffer(const Schema &schema, const DataLayoutFactory &factory, std::size_t num_tuples,
                         MatchBase::callback_t Pipeline)
    : schema_(std::cref(schema))
    , layout_(factory.make(schema, num_tuples))
    , Pipeline_(std::move(Pipeline))
{
    if (layout_.is_finite()) {
        /*----- Pre-allocate memory for entire buffer. Use maximal possible alignment requirement of 8 bytes. -----*/
        const auto child_size_in_bytes = (layout_.stride_in_bits() + 7) / 8;
        const auto num_children =
            (layout_.num_tuples() + layout_.child().num_tuples() - 1) / layout_.child().num_tuples();
        base_address_ = Module::Allocator().pre_allocate(num_children * child_size_in_bytes, /* alignment= */ 8);
    }
}

template<bool IsGlobal>
buffer_load_proxy_t<IsGlobal> Buffer<IsGlobal>::create_load_proxy(proxy_param_t tuple_schema) const
{
    return tuple_schema ? buffer_load_proxy_t(*this, tuple_schema->get()) : buffer_load_proxy_t(*this, schema_);
}

template<bool IsGlobal>
buffer_store_proxy_t<IsGlobal> Buffer<IsGlobal>::create_store_proxy(proxy_param_t tuple_schema) const
{
    return tuple_schema ? buffer_store_proxy_t(*this, tuple_schema->get()) : buffer_store_proxy_t(*this, schema_);
}

template<bool IsGlobal>
buffer_swap_proxy_t<IsGlobal> Buffer<IsGlobal>::create_swap_proxy(proxy_param_t tuple_schema) const
{
    return tuple_schema ? buffer_swap_proxy_t(*this, tuple_schema->get()) : buffer_swap_proxy_t(*this, schema_);
}

template<bool IsGlobal>
void Buffer<IsGlobal>::resume_pipeline()
{
    /*----- Create function on-demand to assert that all needed identifiers are already created. -----*/
    if (not resume_pipeline_) {
        /*----- Create function to resume the pipeline for each tuple contained in the buffer. -----*/
        FUNCTION(resume_pipeline, fn_t)
        {
            auto S = CodeGenContext::Get().scoped_environment(); // create scoped environment for this function
            if (Pipeline_) { // Pipeline callback not empty, i.e. performs some work
                /*----- Access base address and size depending on whether they are globals or parameters. -----*/
                Ptr<void> base_address = [&](){
                    if constexpr (IsGlobal) return base_address_.val(); else return PARAMETER(0);
                }();
                U32 size = [&](){ if constexpr (IsGlobal) return size_.val(); else return PARAMETER(1); }();

                /*----- Compile data layout to generate sequential load from buffer. -----*/
                Var<U32> load_tuple_id; // default initialized to 0
                auto [load_inits, loads, load_jumps] =
                    compile_load_sequential(schema_, base_address, layout_, schema_, load_tuple_id);

                /*----- Generate loop for loading entire buffer, with the pipeline emitted into the loop body. -----*/
                load_inits.attach_to_current();
                WHILE (load_tuple_id < size) {
                    loads.attach_to_current();
                    Pipeline_();
                    load_jumps.attach_to_current();
                }
            }
        }
        resume_pipeline_ = std::move(resume_pipeline);
    }

    /*----- Call created function. -----*/
    if constexpr (IsGlobal)
        (*resume_pipeline_)(); // no argument since base address and size are globals
    else
        (*resume_pipeline_)(base_address_, size_); // base address and size as arguments since they are locals
}

template<bool IsGlobal>
void Buffer<IsGlobal>::resume_pipeline_inline()
{
    M_insist(not resume_pipeline_);
    if (Pipeline_) { // Pipeline callback not empty, i.e. performs some work
        /*----- Compile data layout to generate sequential load from buffer. -----*/
        Var<U32> load_tuple_id(0); // explicitly (re-)set tuple ID to 0
        auto [load_inits, loads, load_jumps] =
            compile_load_sequential(schema_, base_address_, layout_, schema_, load_tuple_id);

        /*----- Generate loop for loading entire buffer, with the pipeline emitted into the loop body. -----*/
        load_inits.attach_to_current();
        WHILE (load_tuple_id < size_) {
            loads.attach_to_current();
            Pipeline_();
            load_jumps.attach_to_current();
        }
    }
}

template<bool IsGlobal>
void Buffer<IsGlobal>::consume()
{
    /*----- Compile data layout to generate sequential store into the buffer. -----*/
    auto [_store_inits, stores, _store_jumps] =
        compile_store_sequential(schema_, base_address_, layout_, schema_, size_);
    /* since structured bindings cannot be used in lambda capture */
    Block store_inits(std::move(_store_inits)), store_jumps(std::move(_store_jumps));

    IF (size_ == 0U) { // buffer empty
        if (not layout_.is_finite()) {
            /*----- Set initial capacity. -----*/
            capacity_ = layout_.child().num_tuples();

            /*----- Allocate memory for one child instance. Use maximal possible alignment requirement of 8 bytes. ---*/
            const auto child_size_in_bytes = (layout_.stride_in_bits() + 7) / 8;
            base_address_ = Module::Allocator().allocate(child_size_in_bytes, /* alignment= */ 8);
        }

        /*----- Emit initialization code for storing (i.e. (re-)set to first buffer slot). -----*/
        store_inits.attach_to_current();
    };

    /*----- Emit storing code and increment size of buffer. -----*/
    stores.attach_to_current();

    if (layout_.is_finite()) {
        IF (size_ == uint32_t(layout_.num_tuples())) { // buffer full
            /*----- Resume pipeline for each tuple in buffer and size of buffer to 0. -----*/
            resume_pipeline();
            size_ = 0U;
        } ELSE { // buffer not full
            /*----- Emit advancing code to next buffer slot. -----*/
            store_jumps.attach_to_current();
        };
    } else {
        M_insist(bool(capacity_));
        IF (size_ == *capacity_) { // buffer full
            /*----- Resize buffer by doubling its capacity. -----*/
            const uint32_t child_size_in_bytes = (layout_.stride_in_bits() + 7) / 8;
            auto buffer_size_in_bytes = (*capacity_ / uint32_t(layout_.child().num_tuples())) * child_size_in_bytes;
            auto ptr = Module::Allocator().allocate(buffer_size_in_bytes.clone());
            Wasm_insist(ptr == base_address_ + buffer_size_in_bytes.make_signed(),
                        "buffer could not be resized sequentially in memory");
            *capacity_ *= 2U;
        };

        /*----- Emit advancing code to next buffer slot. -----*/
        store_jumps.attach_to_current();
    }
}

// explicit instantiations to prevent linker errors
template struct m::wasm::Buffer<false>;
template struct m::wasm::Buffer<true>;


/*======================================================================================================================
 * buffer accesses
 *====================================================================================================================*/

template<bool IsGlobal>
void buffer_swap_proxy_t<IsGlobal>::operator()(U32 first, U32 second)
{
    using std::swap;

    auto &old_env = CodeGenContext::Get().env();
    Environment env;

    /*---- Swap each entry individually to reduce number of variables needed at once. -----*/
    for (auto &e : schema_.get()) {
        /*----- Create schema for single entry and load and store proxies for it. -----*/
        Schema entry_schema;
        entry_schema.add(e.id, e.type);
        auto load  = buffer_.get().create_load_proxy(entry_schema);
        auto store = buffer_.get().create_store_proxy(entry_schema);

        /*----- Load entry of first tuple into fresh environment. -----*/
        swap(old_env, env);
        load(first.clone());
        swap(old_env, env);

        /*----- Temporarily save entry of first tuple by creating variable or separate string buffer. -----*/
        std::visit(overloaded {
            [&](Ptr<Char> value) -> void {
                auto &cs = as<const CharacterSequence>(*e.type);
                const uint32_t length = cs.size() / 8;
                auto ptr = Module::Allocator().pre_malloc<char>(length);
                strncpy(ptr.clone(), value, U32(length)).discard();
                env.add(e.id, ptr);
            },
            [&]<typename T>(Expr<T> value) -> void {
                Var<Expr<T>> var(value);
                env.add(e.id, var);
            },
            [](std::monostate) -> void { M_unreachable("value must be loaded beforehand"); }
        }, env.extract(e.id));

        /*----- Load entry of second tuple in scoped environment and store it directly at first tuples address. -----*/
        {
            auto S = CodeGenContext::Get().scoped_environment();
            load(second.clone());
            store(first.clone());
        }

        /*----- Store temporarily saved entry of first tuple at second tuples address. ----*/
        swap(old_env, env);
        store(second.clone());
        swap(old_env, env);
    }

    first.discard(); // since it was always cloned
    second.discard(); // since it was always cloned
}

// explicit instantiations to prevent linker errors
template struct m::wasm::buffer_swap_proxy_t<false>;
template struct m::wasm::buffer_swap_proxy_t<true>;


/*======================================================================================================================
 * string comparison
 *====================================================================================================================*/

_I32 m::wasm::strncmp(const CharacterSequence &ty_left, const CharacterSequence &ty_right,
                      Ptr<Char> _left, Ptr<Char> _right, U32 len)
{
    Wasm_insist(len.clone() != 0U, "length to compare must not be 0");

    Var<Ptr<Char>> left (_left);
    Var<Ptr<Char>> right(_right);
    _Var<I32> result; // always set here

    IF (left.is_nullptr() or right.is_nullptr()) {
        /*----- If either side is `nullptr` (representing `NULL`), then the result is `NULL`. -----*/
        result = _I32::Null();
    } ELSE {
        if (ty_left.length == 1 and ty_right.length == 1) {
            /*----- Special handling of single char strings. -----*/
            result = (*left > *right).to<int32_t>() - (*left < *right).to<int32_t>();
        } else {
            /*----- Compare character-wise. -----*/
            auto _len = len.clone();
            I32 len_left  = Select(len.clone() < ty_left.length,  len.clone().make_signed(), int32_t(ty_left.length));
            I32 len_right = Select(_len        < ty_right.length, len.make_signed(),         int32_t(ty_right.length));
            Var<Ptr<Char>> end_left (left  + len_left);
            Var<Ptr<Char>> end_right(right + len_right);

            LOOP() {
                if (ty_left.is_varying and ty_right.is_varying) { // both strings end with NUL byte
                    /* Check whether one side is shorter than the other. */
                    result = (left != end_left).to<int32_t>() - (right != end_right).to<int32_t>();
                    BREAK(result != 0 or left == end_left); // at the end of either or both strings

                    /* Compare by current character. Loading is valid since we have not seen the terminating NUL byte
                     * yet. */
                    result = (*left > *right).to<int32_t>() - (*left < *right).to<int32_t>();
                    BREAK(result != 0); // found first position where strings differ
                    BREAK(*left == 0); // reached end of identical strings
                } else { // at least one string may not end with NUL byte
                    /* Check whether one side is shorter than the other. Load next character with in-bounds checks
                     * since the strings may not be NUL byte terminated. */
                    Var<Char> val_left, val_right;
                    IF (left != end_left) {
                        val_left = *left;
                    } ELSE {
                        val_left = '\0';
                    };
                    IF (right != end_right) {
                        val_right = *right;
                    } ELSE {
                        val_right = '\0';
                    };

                    /* Compare by current character. */
                    result = (val_left > val_right).to<int32_t>() - (val_left < val_right).to<int32_t>();
                    BREAK(result != 0); // found first position where strings differ
                    BREAK(val_left == 0); // reached end of identical strings
                }

                /* Advance to next character. */
                left += 1;
                right += 1;
                CONTINUE();
            }
        }
    };

    return result;
}

_I32 m::wasm::strcmp(const CharacterSequence &ty_left, const CharacterSequence &ty_right,
                     Ptr<Char> left, Ptr<Char> right)
{
    /* Delegate to `strncmp` with length set to minimum of both string lengths **plus** 1 since we need to check if
     * one string is a prefix of the other, i.e. all of its characters are equal but it is shorter than the other. */
    U32 len(std::min<uint32_t>(ty_left.length, ty_left.length) + 1U);
    return strncmp(ty_left, ty_right, left, right, len);
}

_Bool m::wasm::strncmp(const CharacterSequence &ty_left, const CharacterSequence &ty_right,
                       Ptr<Char> left, Ptr<Char> right, U32 len, cmp_op op)
{
    _I32 res = strncmp(ty_left, ty_right, left, right, len);

    switch (op) {
        case EQ: return res == 0;
        case NE: return res != 0;
        case LT: return res <  0;
        case LE: return res <= 0;
        case GT: return res >  0;
        case GE: return res >= 0;
    }
}

_Bool m::wasm::strcmp(const CharacterSequence &ty_left, const CharacterSequence &ty_right,
                      Ptr<Char> left, Ptr<Char> right, cmp_op op)
{
    _I32 res = strcmp(ty_left, ty_right, left, right);

    switch (op) {
        case EQ: return res == 0;
        case NE: return res != 0;
        case LT: return res <  0;
        case LE: return res <= 0;
        case GT: return res >  0;
        case GE: return res >= 0;
    }
}


/*======================================================================================================================
 * string copy
 *====================================================================================================================*/

Ptr<Char> m::wasm::strncpy(Ptr<Char> _dst, Ptr<Char> _src, U32 count)
{
    Var<Ptr<Char>> src(_src);
    Var<Ptr<Char>> dst(_dst);

    Wasm_insist(not src.is_nullptr(), "source must not be nullptr");
    Wasm_insist(not dst.is_nullptr(), "destination must not be nullptr");

    Var<Ptr<Char>> src_end(src + count.make_signed());
    WHILE (src != src_end) {
        *dst = *src;
        BREAK(*src == '\0'); // break on terminating NUL byte
        src += 1;
        dst += 1;
    }

    return dst;
}


/*======================================================================================================================
 * WasmLike
 *====================================================================================================================*/

_Bool m::wasm::like(const CharacterSequence &ty_str, const CharacterSequence &ty_pattern,
                    Ptr<Char> _str, Ptr<Char> _pattern, const char escape_char)
{
    M_insist('_' != escape_char and '%' != escape_char, "illegal escape character");

    int32_t str_length = ty_str.length, pattern_length = ty_pattern.length;

    if (str_length == 0 and pattern_length == 0) {
        _str.discard();
        _pattern.discard();
        return _Bool(true);
    }

    _Var<Bool> result; // always set here

    auto [_val_str, is_null_str] = _str.split();
    auto [_val_pattern, is_null_pattern] = _pattern.split();
    Ptr<Char> val_str(_val_str), val_pattern(_val_pattern); // since structured bindings cannot be used in lambda capture

    IF (is_null_str or is_null_pattern) {
        /*----- If either side is `NULL`, then the result is `NULL`. -----*/
        result = _Bool::Null();
    } ELSE {
        /*----- Allocate memory for the dynamic programming table. -----*/
        /* Invariant: dp[i][j] == true iff val_pattern[:i] contains val_str[:j]. Row i and column j is located at
         * dp + (i - 1) * (`length_str` + 1) + (j - 1). */
        auto num_entries = (str_length + 1) * (pattern_length + 1);
        const Var<Ptr<Bool>> dp = Module::Allocator().malloc<bool>(num_entries);

        /*----- Initialize table with all entries set to false. -----*/
        Var<Ptr<Bool>> entry(dp.val());
        WHILE (entry < dp + num_entries) {
            *entry = false;
            entry += 1;
        }

        /*----- Reset entry pointer to first entry. -----*/
        entry = dp.val();

        /*----- Create pointers to track locations of current characters of `_str` and `_pattern`. -----*/
        Var<Ptr<Char>> str(val_str.clone());
        Var<Ptr<Char>> pattern(val_pattern.clone());

        /*----- Compute ends of str and pattern. -----*/
        /* Create constant local variables to ensure correct pointers since `src` and `pattern` will change. */
        const Var<Ptr<Char>> end_str(str + str_length);
        const Var<Ptr<Char>> end_pattern(pattern + pattern_length);

        /*----- Create variables for the current byte of str and pattern. -----*/
        Var<Char> byte_str, byte_pattern; // always loaded before first access

        /*----- Initialize first column. -----*/
        /* Iterate until current byte of pattern is not a `%`-wildcard and set the respective entries to true. */
        DO_WHILE (byte_pattern == '%') {
            byte_pattern = Select(pattern < end_pattern, *pattern, '\0');
            *entry = true;
            entry += str_length + 1;
            pattern += 1;
        }

        /*----- Compute entire table. -----*/
        /* Create variable for the actual length of str. */
        Var<I32> len_str(0);

        /* Create flag whether the current byte of pattern is not escaped. */
        Var<Bool> is_not_escaped(true);

        /* Reset entry pointer to second row and second column. */
        entry = dp + (str_length + 2);

        /* Reset pattern to first character. */
        pattern = val_pattern;

        /* Load first byte from pattern if in bounds. */
        byte_pattern = Select(pattern < end_pattern, *pattern, '\0');

        /* Create loop iterating as long as the current byte of pattern is not NUL. */
        WHILE (byte_pattern != '\0') {
            /* If current byte of pattern is not escaped and equals `escape_char`, advance pattern to next byte and load
             * it. Additionally, mark this byte as escaped and check for invalid escape sequences. */
            IF (is_not_escaped and byte_pattern == escape_char) {
                pattern += 1;
                byte_pattern = Select(pattern < end_pattern, *pattern, '\0');

                /* Check whether current byte of pattern is a validly escaped character, i.e. `_`, `%` or `escape_char`.
                 * If not, throw an exception. */
                IF (byte_pattern != '_' and byte_pattern != '%' and byte_pattern != escape_char) {
                    Throw(exception::invalid_escape_sequence);
                };

                is_not_escaped = false;
            };

            /* Reset actual length of str. */
            len_str = 0;

            /* Load first byte from str if in bounds. */
            byte_str = Select(str < end_str, *str, '\0');

            /* Create loop iterating as long as the current byte of str is not NUL. */
            WHILE (byte_str != '\0') {
                /* Increment actual length of str. */
                len_str += 1;

                IF (is_not_escaped and byte_pattern == '%') {
                    /* Store disjunction of above and left entry. */
                    *entry = *(entry - (str_length + 1)) or *(entry - 1);
                } ELSE {
                    IF ((is_not_escaped and byte_pattern == '_') or byte_pattern == byte_str) {
                        /* Store above left entry. */
                        *entry = *(entry - (str_length + 2));
                    };
                };

                /* Advance entry pointer to next entry, advance str to next byte, and load next byte from str if in
                 * bounds. */
                entry += 1;
                str += 1;
                byte_str = Select(str < end_str, *str, '\0');
            }

            /* Advance entry pointer to second column in the next row, reset str to first character, advance pattern to
             * next byte, load next byte from pattern if in bounds, and reset is_not_escaped to true. */
            entry += (str_length + 1) - len_str;
            str = val_str;
            pattern += 1;
            byte_pattern = Select(pattern < end_pattern, *pattern, '\0');
            is_not_escaped = true;
        }

        /*----- Compute result. -----*/
        /* Entry pointer points currently to the second column in the first row after the pattern has ended. Therefore,
         * we have to go one row up and len_str - 1 columns to the right, i.e. the result is located at
         * entry - (`length_str` + 1) + len_str - 1 = entry + len_str - (`length_str` + 2). */
        result = *(entry + len_str - (str_length + 2));

        /*----- Free allocated space. -----*/
        Module::Allocator().free(dp, num_entries);
    };

    return result;
}


/*======================================================================================================================
 * comparator
 *====================================================================================================================*/

template<bool IsGlobal>
I32 m::wasm::compare(buffer_load_proxy_t<IsGlobal> &load, U32 left, U32 right,
                     const std::vector<std::pair<const m::Expr*, bool>> &order)
{
    using std::swap;

    Var<I32> result(0); // explicitly (re-)set result to 0

    auto &old_env = CodeGenContext::Get().env();
    Environment env_left, env_right;

    /*----- Load left tuple. -----*/
    swap(old_env, env_left);
    load(left);
    swap(old_env, env_left);

    /*----- Load right tuple. -----*/
    swap(old_env, env_right);
    load(right);
    swap(old_env, env_right);

    /*----- Compile ordering. -----*/
    for (auto &o : order) {
        SQL_t _val_left = env_left.compile(*o.first); // compile order expression for left tuple

        std::visit(overloaded {
            [&]<typename T>(Expr<T> val_left) -> void {
                Expr<T> val_right = env_right.compile<Expr<T>>(*o.first); // compile order expression for right tuple

#if 0
                /* XXX: default c'tor not (yet) viable because its constraint is checked before variable_storage
                 *      types are instantiated, once this is supported use the following code and remove the lambda
                 *      for the _Bool case */
                using type = std::conditional_t<std::is_same_v<T, bool>, _I32, Expr<T>>;
                Var<type> left, right;
                if constexpr (std::is_same_v<T, bool>) {
                    left  = val_left.template to<int32_t>();
                    right = val_right.template to<int32_t>();
                } else {
                    left  = val_left;
                    right = val_right;
                }
#else
                Var<Expr<T>> left(val_left), right(val_right);
#endif

                /*----- Compare both with current order expression and update result. -----*/
                I32 cmp_null = right.is_null().template to<int32_t>() - left.is_null().template to<int32_t>();
                _I32 _val_lt = (left < right).template to<int32_t>();
                _I32 _val_gt = (left > right).template to<int32_t>();
                _I32 _cmp_val = o.second ? _val_gt - _val_lt : _val_lt - _val_gt;
                auto [cmp_val, cmp_is_null] = _cmp_val.split();
                cmp_is_null.discard();
                I32 cmp = (cmp_null << 1) + cmp_val; // potentially-null value of comparison is overruled by cmp_null
                result <<= 1; // shift result s.t. first difference will determine order
                result += cmp; // add current comparison to result
            },
            [&](_Bool val_left) -> void {
                _Bool val_right = env_right.compile<_Bool>(*o.first); // compile order expression for right tuple

                _Var<I32> left(val_left.template to<int32_t>()), right(val_right.template to<int32_t>());

                /*----- Compare both with current order expression and update result. -----*/
                I32 cmp_null = right.is_null().template to<int32_t>() - left.is_null().template to<int32_t>();
                _I32 _val_lt = (left < right).template to<int32_t>();
                _I32 _val_gt = (left > right).template to<int32_t>();
                _I32 _cmp_val = o.second ? _val_gt - _val_lt : _val_lt - _val_gt;
                auto [cmp_val, cmp_is_null] = _cmp_val.split();
                cmp_is_null.discard();
                I32 cmp = (cmp_null << 1) + cmp_val; // potentially-null value of comparison is overruled by cmp_null
                result <<= 1; // shift result s.t. first difference will determine order
                result += cmp; // add current comparison to result
            },
            [&](Ptr<Char> val_left) -> void {
                auto &cs = as<const CharacterSequence>(*o.first->type());

                Ptr<Char> val_right = env_right.compile<Ptr<Char>>(*o.first); // compile order expression for right tuple

                Var<Ptr<Char>> left(val_left), right(val_right);

                /*----- Compare both with current order expression and update result. -----*/
                I32 cmp_null = right.is_nullptr().to<int32_t>() - left.is_nullptr().to<int32_t>();
                _I32 _delta = o.second ? strcmp(cs, cs, left, right) : strcmp(cs, cs, right, left);
                auto [cmp_val, cmp_is_null] = signum(_delta).split();
                cmp_is_null.discard();
                I32 cmp = (cmp_null << 1) + cmp_val; // potentially-null value of comparison is overruled by cmp_null
                result <<= 1; // shift result s.t. first difference will determine order
                result += cmp; // add current comparison to result
            },
            [](std::monostate) -> void { M_unreachable("invalid expression"); }
        }, _val_left);
    }

    return result;
}

// explicit instantiations to prevent linker errors
template I32 m::wasm::compare(buffer_load_proxy_t<false> &load, U32 left, U32 right,
                              const std::vector<std::pair<const m::Expr*, bool>> &order);
template I32 m::wasm::compare(buffer_load_proxy_t<true> &load, U32 left, U32 right,
                              const std::vector<std::pair<const m::Expr*, bool>> &order);
