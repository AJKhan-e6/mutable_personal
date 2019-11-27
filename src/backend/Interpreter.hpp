#pragma once

#include "backend/Backend.hpp"
#include "catalog/Schema.hpp"
#include "IR/Operator.hpp"
#include "IR/OperatorVisitor.hpp"
#include "util/macro.hpp"
#include <unordered_map>


namespace db {

struct StackMachineBuilder;

/** A stack machine that evaluates an expression. */
struct StackMachine
{
    friend struct StackMachineBuilder;

    enum class Opcode : uint8_t
    {
#define DB_OPCODE(CODE, ...) CODE,
#include "tables/Opcodes.tbl"
#undef DB_OPCODE
    };

    using index_t = std::size_t;

    const OperatorSchema schema;

    static constexpr const char *OPCODE_TO_STR[] = {
#define DB_OPCODE(CODE, ...) #CODE,
#include "tables/Opcodes.tbl"
#undef DB_OPCODE
    };

    static const std::unordered_map<std::string, Opcode> STR_TO_OPCODE;

    std::vector<Opcode> ops; ///< a sequence of operations to perform
    private:
    std::vector<value_type> context_; ///< the context of the stack machine, e.g. constants or global variables
    std::vector<value_type> stack_; ///< the stack of current values

    public:
    StackMachine() { }
    StackMachine(const OperatorSchema &schema, const Expr &expr);
    StackMachine(const OperatorSchema &schema);

    StackMachine(const StackMachine&) = delete;
    StackMachine(StackMachine&&) = default;

    void emit(const Expr &expr);
    void emit(const cnf::CNF &cnf);

    tuple_type && operator()(const tuple_type &t);
    tuple_type && operator()() { return operator()(tuple_type()); }

    /* The following macros are used to automatically generate methods to emit a particular opcode.  For example, for
     * the opcode `Pop`, we will define a function `emit_Pop()`, that appends the `Pop` opcode to the current opcode
     * sequence.  For opcodes that require an argument, a function with the respective parameter is defined and that
     * parameter is inserted into the opcode sequence.  For example, the opcode `Ld_Ctx` requires a single parameter
     * with the index of the context value.  The macro will expand to the method `emit_Ld_Ctx(uint8_t idx)`, that first
     * appends the `Ld_Ctx` opcode to the opcode sequence and then appends the `idx` parameter to the opcode sequence.
     */
#define SELECT(XXX, _1, _2, FN, ...) FN(__VA_ARGS__)
#define ARGS_0(I, ...)
#define ARGS_1(I, II, ARG0, ...) uint8_t ARG0
#define ARGS_2(I, II, III, ARG0, ARG1, ...) uint8_t ARG0, uint8_t ARG1
#define ARGS(...) SELECT(__VA_ARGS__, ARGS_2, ARGS_1, ARGS_0, __VA_ARGS__)
#define PUSH_0(I, ...)
#define PUSH_1(I, II, ARG0, ...) \
    ops.push_back(static_cast<Opcode>((ARG0)));
#define PUSH_2(I, II, III, ARG0, ARG1, ...) \
    ops.push_back(static_cast<Opcode>((ARG0))); \
    ops.push_back(static_cast<Opcode>((ARG1)));
#define PUSH(...) SELECT(__VA_ARGS__, PUSH_2, PUSH_1, PUSH_0, __VA_ARGS__)

#define DB_OPCODE(...) \
    void CAT(emit_, FIRST(__VA_ARGS__)) ( ARGS(__VA_ARGS__) ) { \
        ops.push_back(StackMachine::Opcode:: FIRST(__VA_ARGS__) ); \
        PUSH(__VA_ARGS__) \
    }

#include "tables/Opcodes.tbl"

#undef DB_OPCODE
#undef SELECT
#undef ARGS_0
#undef ARGS_1
#undef ARGS_2
#undef ARGS
#undef PUSH_0
#undef PUSH_1
#undef PUSH_2
#undef PUSH

    /** Append the given opcode to the opcode sequence. */
    void emit(Opcode opc) { ops.push_back(opc); }

    /** Adds a value to the context and returns its assigned index. */
    std::size_t add(value_type value) {
        auto idx = context_.size();
        context_.push_back(value);
        return idx;
    }

    void set(std::size_t idx, value_type value) {
        insist(idx < context_.size(), "index out of bounds");
        context_[idx] = value;
    }

    /** Adds a value to the context and emits a load instruction to load this value to the top of the stack. */
    std::size_t add_and_emit_load(value_type value) {
        auto idx = add(value);
        emit_Ld_Ctx(idx);
        return idx;
    }

    void dump(std::ostream &out) const;
    void dump() const;
};

bool eval(const OperatorSchema &schema, const cnf::CNF &cnf, const tuple_type &tuple);

/** Evaluates SQL operator trees on the database. */
struct Interpreter : Backend, ConstOperatorVisitor
{
    public:
    Interpreter() = default;

    void execute(const Operator &plan) const override { (*const_cast<Interpreter*>(this))(plan); }

    using ConstOperatorVisitor::operator();

#define DECLARE(CLASS) \
    void operator()(Const<CLASS> &op) override
#define DECLARE_CONSUMER(CLASS) \
    DECLARE(CLASS); \
    void operator()(Const<CLASS> &op, tuple_type &t) override

    DECLARE(ScanOperator);

    DECLARE_CONSUMER(CallbackOperator);
    DECLARE_CONSUMER(FilterOperator);
    DECLARE_CONSUMER(JoinOperator);
    DECLARE_CONSUMER(ProjectionOperator);
    DECLARE_CONSUMER(LimitOperator);
    DECLARE_CONSUMER(GroupingOperator);
    DECLARE_CONSUMER(SortingOperator);

#undef DECLARE_CONSUMER
#undef DECLARE

    static value_type eval(const Constant &c)
    {
        errno = 0;
        switch (c.tok.type) {
            default: unreachable("illegal token");

            /* Integer */
            case TK_OCT_INT:
                return int64_t(strtoll(c.tok.text, nullptr, 8));

            case TK_DEC_INT:
                return int64_t(strtoll(c.tok.text, nullptr, 10));

            case TK_HEX_INT:
                return int64_t(strtoll(c.tok.text, nullptr, 16));

            /* Float */
            case TK_DEC_FLOAT:
                return strtod(c.tok.text, nullptr);

            case TK_HEX_FLOAT:
                unreachable("not implemented");

            /* String */
            case TK_STRING_LITERAL:
                return interpret(c.tok.text);

            /* Boolean */
            case TK_True:
                return true;

            case TK_False:
                return false;
        }
        insist(errno == 0, "constant could not be parsed");
    }

};

}
