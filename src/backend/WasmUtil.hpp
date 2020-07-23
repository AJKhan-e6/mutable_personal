#pragma once

#include "catalog/Schema.hpp"
#include "catalog/Type.hpp"
#include "IR/CNF.hpp"
#include <binaryen-c.h>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>


namespace db {

struct Type;

inline BinaryenType get_binaryen_type(const Type *ty)
{
    insist(not ty->is_error());

    if (ty->is_boolean()) return BinaryenTypeInt32();

    if (auto n = cast<const Numeric>(ty)) {
        if (n->kind == Numeric::N_Float) {
            if (n->size() == 32) return BinaryenTypeFloat32();
            else                 return BinaryenTypeFloat64();
        }

        switch (n->size()) {
            case 8:  /* not supported, fall through */
            case 16: /* not supported, fall through */
            case 32: return BinaryenTypeInt32();
            case 64: return BinaryenTypeInt64();
        }
    }

    unreachable("unsupported type");
}

inline BinaryenExpressionRef convert(BinaryenModuleRef module, BinaryenExpressionRef expr,
                                     const Type *original, const Type *target)
{
#define CONVERT(CONVERSION) BinaryenUnary(module, Binaryen##CONVERSION(), expr)
    auto O = as<const Numeric>(original);
    auto T = as<const Numeric>(target);
    if (O->as_vectorial() == T->as_vectorial()) return expr; // no conversion required

    if (T->is_double()) {
        if (O->is_float())
            return CONVERT(PromoteFloat32); // f32 to f64
        if (O->is_integral()) {
            if (O->size() == 64)
                return CONVERT(ConvertSInt64ToFloat64); // i64 to f64
            else
                return CONVERT(ConvertSInt32ToFloat64); // i32 to f64
        }
        if (O->is_decimal()) {
            unreachable("not implemented");
        }
    }

    if (T->is_float()) {
        if (O->is_integral()) {
            if (O->size() == 64)
                return CONVERT(ConvertSInt64ToFloat32); // i64 to f32
            else
                return CONVERT(ConvertSInt32ToFloat32); // i32 to f32
        }
        if (O->is_decimal()) {
            unreachable("not implemented");
        }
    }

    if (T->is_integral()) {
        if (T->size() == 64) {
            if (O->is_integral()) {
                if (O->size() == 64)
                    return expr; // i64 to i64; no conversion required
                else
                    return CONVERT(ExtendSInt32); // i32 to i64
            }
        }
        if (T->size() == 32) {
            if (O->is_integral()) {
                if (O->size() == 64)
                    return CONVERT(WrapInt64); // i64 to i32
                else
                    return expr; // i32 to i32; no conversion required
            }
        }
    }

    unreachable("unsupported conversion");
#undef CONVERT
};

inline BinaryenExpressionRef reinterpret(BinaryenModuleRef module, const BinaryenExpressionRef expr,
                                         const BinaryenType target)
{
#define CONVERT(CONVERSION, EXPR) BinaryenUnary(module, Binaryen##CONVERSION(), EXPR)
    const BinaryenType original = BinaryenExpressionGetType(expr);
    if (original == target) return expr;

    if (target == BinaryenTypeInt64()) {
        if (original == BinaryenTypeInt32())
            return CONVERT(ExtendUInt32, expr); // i32 to i64
        if (original == BinaryenTypeFloat32())
            return CONVERT(ExtendUInt32, CONVERT(ReinterpretFloat32, expr)); // f32 to i64
        if (original == BinaryenTypeFloat64())
            return CONVERT(ReinterpretFloat64, expr); // f64 to i64
    }

    unreachable("unsupported reinterpretation");
#undef CONVERT
}

/** A helper class to provide a context for compilation of expressions. */
struct WasmCGContext : ConstASTExprVisitor
{
    private:
    BinaryenModuleRef module_; ///< the module
    ///> Maps `Schema::Identifier`s to `BinaryenExpressionRef`s that evaluate to 0 if NULL and 1 otherwise
    std::unordered_map<Schema::Identifier, BinaryenExpressionRef> nulls_;
    ///> Maps `Schema::Identifier`s to `BinaryenExpressionRef`s that evaluate to the current value
    std::unordered_map<Schema::Identifier, BinaryenExpressionRef> values_;

    BinaryenExpressionRef expr_; ///< a temporary used for recursive construction of expressions

    public:
    WasmCGContext(BinaryenModuleRef module) : module_(module) { }
    WasmCGContext(const WasmCGContext&) = delete;
    WasmCGContext(WasmCGContext&&) = default;

    BinaryenModuleRef module() const { return module_; }

    bool has(Schema::Identifier id) const { return values_.find(id) != values_.end(); }

    void add(Schema::Identifier id, BinaryenExpressionRef val) {
        auto res = values_.emplace(id, val);
        insist(res.second, "duplicate ID");
    }

    BinaryenExpressionRef get_null(Schema::Identifier id) const {
        auto it = nulls_.find(id);
        insist(it != nulls_.end(), "no entry for identifier");
        return it->second;
    }

    BinaryenExpressionRef get_value(Schema::Identifier id) const {
        auto it = values_.find(id);
        insist(it != values_.end(), "no entry for identifier");
        return it->second;
    }

    BinaryenExpressionRef operator[](Schema::Identifier id) const { return get_value(id); }

    /** Compiles an AST expression to a `BinaryenExpressionRef`. */
    BinaryenExpressionRef compile(const Expr &e) const {
        const_cast<WasmCGContext*>(this)->operator()(e);
        return expr_;
    }

    /** Compiles a `cnf::CNF` to a `BinaryenExpressionRef`.  (Without short-circuit evaluation!) */
    BinaryenExpressionRef compile(const cnf::CNF &cnf) const;

    void dump(std::ostream &out) const;
    void dump() const;

    private:
    using ConstASTExprVisitor::operator();
#define DECLARE(CLASS) void operator()(const CLASS &op) override;
    DB_AST_EXPR_LIST(DECLARE)
#undef DECLARE
};

/** A helper class to generate accesses into a structure. */
struct WasmStruct
{
    private:
    BinaryenModuleRef module_;
    std::size_t size_; ///< the size in bytes of the struct
    std::size_t *offsets_; ///< stores the offsets within the structure
    public:
    const Schema &schema; ///< the schema of the struct

    WasmStruct(BinaryenModuleRef module, const Schema &schema)
        : module_(module)
        , offsets_(new std::size_t[schema.num_entries()])
        , schema(schema)
    {
        /*----- Compute struct size. ---------------------------------------------------------------------------------*/
        std::size_t offset = 0;
        std::size_t alignment = 0;
        std::size_t idx = 0;
        for (auto &attr : schema) {
            const std::size_t size_in_bytes = attr.type->size() < 8 ? 1 : attr.type->size() / 8;
            alignment = std::max(alignment, size_in_bytes);
            if (offset % size_in_bytes)
                offset += size_in_bytes - (offset % size_in_bytes); // self-align
            offsets_[idx++] = offset;
            offset += size_in_bytes;
        }
        if (offset % alignment)
            offset += alignment - (offset % alignment);
        size_ = offset;
    }

    WasmStruct(const WasmStruct&) = delete;

    ~WasmStruct() { delete[] offsets_; }

    std::size_t size() const { return size_; }

    std::size_t offset(std::size_t idx) const { insist(idx < schema.num_entries()); return offsets_[idx]; }

    WasmCGContext create_load_context(BinaryenExpressionRef b_ptr, std::size_t struc_offset = 0) const {
        WasmCGContext context(module_);
        std::size_t idx = 0;
        for (auto &attr : schema) {
            const std::size_t size_in_bytes = attr.type->size() < 8 ? 1 : attr.type->size() / 8;
            BinaryenType b_attr_type = get_binaryen_type(attr.type);

            /*----- Load value from struct.  -------------------------------------------------------------------------*/
            auto b_val = BinaryenLoad(
                /* module= */ module_,
                /* bytes=  */ size_in_bytes,
                /* signed= */ true,
                /* offset= */ offset(idx++) + struc_offset,
                /* align=  */ struc_offset % size_in_bytes ? 1 : 0,
                /* type=   */ b_attr_type,
                /* ptr=    */ b_ptr
            );
            context.add(attr.id, b_val);
        }
        return context;
    }

    BinaryenExpressionRef store(BinaryenExpressionRef b_ptr, Schema::Identifier id, BinaryenExpressionRef b_val,
                                std::size_t struc_offset = 0) const {
        std::size_t idx = 0;
        for (auto &attr : schema) {
            if (attr.id == id) break;
            ++idx;
        }
        insist(idx < schema.num_entries(), "unknown identifier");

        auto &attr = schema[idx];
        const std::size_t size_in_bytes = attr.type->size() < 8 ? 1 : attr.type->size() / 8;
        return BinaryenStore(
            /* module= */ module_,
            /* bytes=  */ size_in_bytes,
            /* offset= */ offset(idx) + struc_offset,
            /* align=  */ struc_offset % size_in_bytes ? 1 : 0,
            /* ptr=    */ b_ptr,
            /* value=  */ b_val,
            /* type=   */ get_binaryen_type(attr.type)
        );
    }

    void dump(std::ostream &out) const;
    void dump() const;
};

/** Helper class to construct WASM blocks. */
struct BlockBuilder
{
    friend void swap(BlockBuilder &first, BlockBuilder &second) {
        using std::swap;
        swap(first.module_,      second.module_);
        swap(first.name_,        second.name_);
        swap(first.exprs_,       second.exprs_);
        swap(first.return_type_, second.return_type_);
    }

    private:
    BinaryenModuleRef module_; ///< the WebAssembly module
    const char *name_ = nullptr; ///< the block name
    std::vector<BinaryenExpressionRef> exprs_; ///< list of expressions in the block
    BinaryenType return_type_; ///< the result type of the block (i.e. the type of the last expression)

    public:
    BlockBuilder(BinaryenModuleRef module, const char *name = nullptr)
        : module_(module)
        , name_(strdupn(name))
        , return_type_(BinaryenTypeAuto())
    { }
    BlockBuilder(const BlockBuilder&) = delete;
    BlockBuilder(BlockBuilder &&other) { swap(*this, other); }
    ~BlockBuilder() { free((void*) name_); }

    BinaryenModuleRef module() const { return module_; }

    BlockBuilder & operator=(BlockBuilder other) { swap(*this, other); return *this; }

    void add(BinaryenExpressionRef expr) { exprs_.push_back(expr); }
    BlockBuilder & operator+=(BinaryenExpressionRef expr) { add(expr); return *this; }

    void name(const char *name) { free((void*) name_); name_ = strdupn(name); }
    const char * name() const { return name_; }

    void set_return_type(BinaryenType ty) { return_type_ = ty; }

    BinaryenExpressionRef finalize() {
        return BinaryenBlock(
            /* module=      */ module_,
            /* name=        */ name_,
            /* children=    */ &exprs_[0],
            /* numChildren= */ exprs_.size(),
            /* type=        */ return_type_
        );
    }
};

/** Helper class to construct WASM functions. */
struct FunctionBuilder
{
    private:
    BinaryenModuleRef module_; ///< the WebAssembly module
    const char *name_ = nullptr; ///< the name of this function
    BinaryenType result_type_; ///< the result type
    BinaryenType parameter_type_; ///< the compound type of all parameters
    std::vector<BinaryenType> locals_; ///< the types of local variables
    BlockBuilder block_; ///< the function body

    public:
    FunctionBuilder(BinaryenModuleRef module, const char *name,
                    BinaryenType result_type, std::vector<BinaryenType> parameter_types)
        : module_(module)
        , name_(strdup(notnull(name)))
        , result_type_(result_type)
        , parameter_type_(BinaryenTypeCreate(&parameter_types[0], parameter_types.size()))
        , block_(module, (std::string(name) + ".body").c_str())
    { }

    FunctionBuilder(const FunctionBuilder&) = delete;

    ~FunctionBuilder() { free((void*) name_); }

    /** Create a `BinaryenFunctionRef` with the current block and locals. */
    BinaryenFunctionRef finalize() {
        return BinaryenAddFunction(
            /* module=      */ module_,
            /* name=        */ name_,
            /* params=      */ parameter_type_,
            /* results=     */ result_type_,
            /* varTypes=    */ &locals_[0],
            /* numVarTypes= */ locals_.size(),
            /* body=        */ block_.finalize()
        );
    }

    /** Returns the function body. */
    BlockBuilder & block() { return block_; }
    /** Returns the function body. */
    const BlockBuilder & block() const { return block_; }

    const char * name() const { return name_; }

    /** Add a fresh local variable to the function and return a `BinaryenLocalGet` expression to access it. */
    BinaryenExpressionRef add_local(BinaryenType ty) {
        std::size_t idx = BinaryenTypeArity(parameter_type_) + locals_.size();
        locals_.push_back(ty);
        return BinaryenLocalGet(module_, idx, ty);
    }
};

struct WasmVariable
{
    private:
    BinaryenExpressionRef b_var_;

    public:
    WasmVariable(FunctionBuilder &fn, BinaryenType ty) :
        b_var_(fn.add_local(ty))
    { }

    WasmVariable(const WasmVariable&) = delete;
    WasmVariable(WasmVariable&&) = default;

    const WasmVariable & set(BlockBuilder &block, BinaryenExpressionRef b_expr) const {
        block += BinaryenLocalSet(
            /* module= */ block.module(),
            /* index=  */ BinaryenLocalGetGetIndex(b_var_),
            /* value=  */ b_expr
        );
        return *this;
    }

    BinaryenExpressionRef operator()() const { return b_var_; }

    operator BinaryenExpressionRef() const { return b_var_; }
};

struct WasmLoop
{
    private:
    BlockBuilder body_;
    const char *name_;

    public:
    WasmLoop(BinaryenModuleRef module, const char *name)
        : body_(module, (std::string(name) + ".body").c_str())
        , name_(strdupn(notnull(name)))
    { }

    virtual ~WasmLoop() { free((void*) name_); }

    BlockBuilder & body() { return body_; }
    const BlockBuilder & body() const { return body_; }
    void name(const char *name) { free((void*) name_); name_ = strdupn(name); }
    const char * name() const { return name_; }

    operator BlockBuilder&() { return body(); }

    WasmLoop & operator+=(BinaryenExpressionRef b_expr) { body() += b_expr; return *this; }

    BinaryenExpressionRef continu(BinaryenExpressionRef condition = nullptr) {
        return BinaryenBreak(
            /* module=    */ body().module(),
            /* name=      */ name(),
            /* condition= */ condition,
            /* value=     */ nullptr
        );
    }

    virtual BinaryenExpressionRef finalize() {
        return BinaryenLoop(
            /* module= */ body().module(),
            /* name=   */ name(),
            /* body=   */ body().finalize()
        );
    }
};

struct WasmDoWhile : WasmLoop
{
    private:
    BinaryenExpressionRef condition_;

    public:
    WasmDoWhile(BinaryenModuleRef module, const char *name, BinaryenExpressionRef condition)
        : WasmLoop(module, name)
        , condition_(condition)
    { }

    BinaryenExpressionRef condition() const { return condition_; }

    virtual BinaryenExpressionRef finalize() {
        body() += continu(condition());
        return WasmLoop::finalize();
    }
};

struct WasmWhile : WasmDoWhile
{
    public:
    WasmWhile(BinaryenModuleRef module, const char *name, BinaryenExpressionRef condition)
        : WasmDoWhile(module, name, condition)
    { }

    BinaryenExpressionRef finalize() override {
        auto loop = WasmDoWhile::finalize();
        return BinaryenIf(
            /* module=    */ body().module(),
            /* condition= */ condition(),
            /* ifTrue=    */ loop,
            /* ifFalse=   */ nullptr
        );
    }
};

struct WasmCompare
{
    using order_type = std::pair<const Expr*, bool>;

    private:
    BinaryenModuleRef module_;
    public:
    const WasmStruct &struc;
    const std::vector<order_type> &order; ///< the attributes to sort by

    WasmCompare(BinaryenModuleRef module, const WasmStruct &struc, const std::vector<order_type> &order)
        : module_(module)
        , struc(struc)
        , order(order)
    { }

    BinaryenExpressionRef emit(FunctionBuilder &fn, BlockBuilder &block,
                               const WasmCGContext &left, const WasmCGContext &right);

    static BinaryenExpressionRef Eq(BinaryenModuleRef module, const Type &ty,
                                    BinaryenExpressionRef left, BinaryenExpressionRef right);
    static BinaryenExpressionRef Ne(BinaryenModuleRef module, const Type &ty,
                                    BinaryenExpressionRef left, BinaryenExpressionRef right);
};

struct WasmSwap
{
    BinaryenModuleRef module;
    FunctionBuilder &fn;
    std::unordered_map<BinaryenType, BinaryenExpressionRef> swap_temp;

    WasmSwap(BinaryenModuleRef module, FunctionBuilder &fn) : module(module) , fn(fn) { }

    void emit(BlockBuilder &block, const WasmStruct &struc,
              BinaryenExpressionRef b_first, BinaryenExpressionRef b_second);
};

struct WasmLimits
{
    static BinaryenLiteral min(const Type &type);
    static BinaryenLiteral lowest(const Type &type);
    static BinaryenLiteral max(const Type &type);
    static BinaryenLiteral NaN(const Type &type);
    static BinaryenLiteral infinity(const Type &type);
};


template<typename T>
BinaryenLiteral wasm_constant(const T &val, const Type &type)
{
    struct V : ConstTypeVisitor
    {
        const T &value;
        BinaryenLiteral literal;

        V(const T &value) : value(value) { }

        using ConstTypeVisitor::operator();
        void operator()(Const<ErrorType>&) { unreachable("not allowed"); }
        void operator()(Const<Boolean>&) { literal = BinaryenLiteralInt32(value); }
        void operator()(Const<CharacterSequence>&) { unreachable("not supported"); }
        void operator()(Const<Numeric> &ty) {
            switch (ty.kind) {
                case Numeric::N_Int:
                    if (ty.size() == 32)
                        literal = BinaryenLiteralInt32(value);
                    else
                        literal = BinaryenLiteralInt64(value);
                    break;

                case Numeric::N_Decimal:
                    unreachable("not supported");

                case Numeric::N_Float:
                    if (ty.size() == 32)
                        literal = BinaryenLiteralFloat32(value);
                    else
                        literal = BinaryenLiteralFloat64(value);
                    break;
            }
        }
        void operator()(Const<FnType>&) { unreachable("not allowed"); }
    };

    V v(val);
    v(type);
    return v.literal;
}

}
