#include "backend/StackMachine.hpp"

#include "backend/Interpreter.hpp"
#include <functional>


using namespace db;


/*======================================================================================================================
 * Helper functions
 *====================================================================================================================*/

/** Return the type suffix for the given `PrimitiveType` `ty`. */
const char * tystr(const PrimitiveType *ty) {
    if (ty->is_boolean())
        return "_b";
    if (ty->is_character_sequence())
        return "_s";
    auto n = as<const Numeric>(ty);
    switch (n->kind) {
        case Numeric::N_Int:
        case Numeric::N_Decimal:
            return "_i";
        case Numeric::N_Float:
            return n->precision == 32 ? "_f" : "_d";
    }
};


/*======================================================================================================================
 * StackMachineBuilder
 *====================================================================================================================*/

struct db::StackMachineBuilder : ConstASTVisitor
{
    private:
    StackMachine &stack_machine_;
    const Schema &schema_;
    const std::size_t tuple_id;

    public:
    StackMachineBuilder(StackMachine &stack_machine, const Schema &in_schema, const Expr &expr, std::size_t tuple_id)
        : stack_machine_(stack_machine)
        , schema_(in_schema)
        , tuple_id(tuple_id)
    {
        (*this)(expr); // compute the command sequence
    }

    private:
    using ConstASTVisitor::operator();

    /* Expressions */
    void operator()(Const<ErrorExpr>&) override { unreachable("invalid expression"); }
    void operator()(Const<Designator> &e) override;
    void operator()(Const<Constant> &e) override;
    void operator()(Const<FnApplicationExpr> &e) override;
    void operator()(Const<UnaryExpr> &e) override;
    void operator()(Const<BinaryExpr> &e) override;

    /* Clauses */
    void operator()(Const<ErrorClause>&) override { unreachable("not supported"); }
    void operator()(Const<SelectClause>&) override { unreachable("not supported"); }
    void operator()(Const<FromClause>&) override { unreachable("not supported"); }
    void operator()(Const<WhereClause>&) override { unreachable("not supported"); }
    void operator()(Const<GroupByClause>&) override { unreachable("not supported"); }
    void operator()(Const<HavingClause>&) override { unreachable("not supported"); }
    void operator()(Const<OrderByClause>&) override { unreachable("not supported"); }
    void operator()(Const<LimitClause>&) override { unreachable("not supported"); }

    /* Statements */
    void operator()(Const<ErrorStmt>&) override { unreachable("not supported"); }
    void operator()(Const<EmptyStmt>&) override { unreachable("not supported"); }
    void operator()(Const<CreateDatabaseStmt>&) override { unreachable("not supported"); }
    void operator()(Const<UseDatabaseStmt>&) override { unreachable("not supported"); }
    void operator()(Const<CreateTableStmt>&) override { unreachable("not supported"); }
    void operator()(Const<SelectStmt>&) override { unreachable("not supported"); }
    void operator()(Const<InsertStmt>&) override { unreachable("not supported"); }
    void operator()(Const<UpdateStmt>&) override { unreachable("not supported"); }
    void operator()(Const<DeleteStmt>&) override { unreachable("not supported"); }
    void operator()(Const<DSVImportStmt>&) override { unreachable("not supported"); }
};

void StackMachineBuilder::operator()(Const<Designator> &e)
{
    /* Given the designator, identify the position of its value in the tuple.  */
    std::size_t idx;
    if (e.has_explicit_table_name())
        idx = schema_[{e.table_name.text, e.attr_name.text}].first;
    else
        idx = schema_[{nullptr, e.attr_name.text}].first;
    insist(idx < schema_.num_entries(), "index out of bounds");
    stack_machine_.emit_Ld_Tup(tuple_id, idx);
}

void StackMachineBuilder::operator()(Const<Constant> &e) {
    if (e.tok == TK_Null)
        stack_machine_.emit_Push_Null();
    else
        stack_machine_.add_and_emit_load(Interpreter::eval(e));
}

void StackMachineBuilder::operator()(Const<FnApplicationExpr> &e)
{
    auto &C = Catalog::Get();
    auto &fn = e.get_function();

    /* Load the arguments for the function call. */
    switch (fn.fnid) {
        default:
            unreachable("function kind not implemented");

        case Function::FN_UDF:
            unreachable("UDFs not yet supported");

        case Function::FN_ISNULL:
            insist(e.args.size() == 1);
            (*this)(*e.args[0]);
            stack_machine_.emit_Is_Null();
            break;

        /*----- Type casts -------------------------------------------------------------------------------------------*/
        case Function::FN_INT: {
            insist(e.args.size() == 1);
            (*this)(*e.args[0]);
            auto ty = e.args[0]->type();
            if (ty->is_float())
                stack_machine_.emit_Cast_i_f();
            else if (ty->is_double())
                stack_machine_.emit_Cast_i_d();
            else if (ty->is_decimal()) {
                unreachable("not implemented");
            } else if (ty->is_boolean()) {
                stack_machine_.emit_Cast_i_b();
            } else {
                /* nothing to be done */
            }
            break;
        }

        /*----- Aggregate functions ----------------------------------------------------------------------------------*/
        case Function::FN_COUNT:
        case Function::FN_MIN:
        case Function::FN_MAX:
        case Function::FN_SUM:
        case Function::FN_AVG: {
            std::ostringstream oss;
            oss << e;
            auto name = C.pool(oss.str().c_str());
            auto idx = schema_[{name}].first;
            insist(idx < schema_.num_entries(), "index out of bounds");
            stack_machine_.emit_Ld_Tup(tuple_id, idx);
            return;
        }
    }
}

void StackMachineBuilder::operator()(Const<UnaryExpr> &e)
{
    (*this)(*e.expr);
    auto ty = e.expr->type();

    switch (e.op().type) {
        default:
            unreachable("illegal token type");

        case TK_PLUS:
            /* nothing to be done */
            break;

        case TK_MINUS: {
            auto n = as<const Numeric>(ty);
            switch (n->kind) {
                case Numeric::N_Int:
                case Numeric::N_Decimal:
                    stack_machine_.emit_Minus_i();
                    break;

                case Numeric::N_Float:
                    if (n->precision == 32)
                        stack_machine_.emit_Minus_f();
                    else
                        stack_machine_.emit_Minus_d();
            }
            break;
        }

        case TK_TILDE:
            if (ty->is_integral())
                stack_machine_.emit_Neg_i();
            else if (ty->is_boolean())
                stack_machine_.emit_Not_b(); // negation of bool is always logical
            else
                unreachable("illegal type");
            break;

        case TK_Not:
            insist(ty->is_boolean(), "illegal type");
            stack_machine_.emit_Not_b();
            break;
    }
}

void StackMachineBuilder::operator()(Const<BinaryExpr> &e)
{
    auto ty = as<const PrimitiveType>(e.type());
    auto ty_lhs = as<const PrimitiveType>(e.lhs->type());
    auto ty_rhs = as<const PrimitiveType>(e.rhs->type());
    auto tystr_to = tystr(ty);

    /* Emit instructions to convert the current top-of-stack of type `from_ty` to type `to_ty`. */
    auto emit_cast = [&](const PrimitiveType *from_ty, const PrimitiveType *to_ty) {
        std::string tystr_to = tystr(to_ty);
        std::string tystr_from = tystr(from_ty);
        /* Cast LHS if necessary. */
        if (tystr_from != tystr_to) {
            std::string opstr = "Cast" + tystr_to + tystr_from;
            auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
            stack_machine_.emit(opcode);
        }
    };

    /* Emit instructions to bring the current top-of-stack with precision of `from_ty` to precision of `to_ty`. */
    auto scale = [&](const PrimitiveType *from_ty, const PrimitiveType *to_ty) {
        auto n_from = as<const Numeric>(from_ty);
        auto n_to = as<const Numeric>(to_ty);
        if (n_from->scale < n_to->scale) {
            insist(n_to->is_decimal(), "only decimals have a scale");
            /* Scale up. */
            auto delta = n_to->scale - n_from->scale;
            const int64_t factor = powi<int64_t>(10, delta);
            switch (n_from->kind) {
                case Numeric::N_Float:
                    if (n_from->precision == 32) {
                        stack_machine_.add_and_emit_load(float(factor));
                        stack_machine_.emit_Mul_f(); // multiply by scale factor
                    } else {
                        stack_machine_.add_and_emit_load(double(factor));
                        stack_machine_.emit_Mul_d(); // multiply by scale factor
                    }
                    break;

                case Numeric::N_Decimal:
                case Numeric::N_Int:
                    stack_machine_.add_and_emit_load(factor);
                    stack_machine_.emit_Mul_i(); // multiply by scale factor
                    break;
            }
        } else if (n_from->scale > n_to->scale) {
            insist(n_from->is_decimal(), "only decimals have a scale");
            /* Scale down. */
            auto delta = n_from->scale - n_to->scale;
            const int64_t factor = powi<int64_t>(10, delta);
            switch (n_from->kind) {
                case Numeric::N_Float:
                    if (n_from->precision == 32) {
                        stack_machine_.add_and_emit_load(float(1.f / factor));
                        stack_machine_.emit_Mul_f(); // multiply by inverse scale factor
                    } else {
                        stack_machine_.add_and_emit_load(double(1. / factor));
                        stack_machine_.emit_Mul_d(); // multiply by inverse scale factor
                    }
                    break;

                case Numeric::N_Decimal:
                    stack_machine_.add_and_emit_load(factor);
                    stack_machine_.emit_Div_i(); // divide by scale factor
                    break;

                case Numeric::N_Int:
                    unreachable("int cannot be scaled down");
            }
        }
    };

    auto load_numeric = [&](auto val, const Numeric *n) {
        switch (n->kind) {
            case Numeric::N_Int:
            case Numeric::N_Decimal:
                return stack_machine_.add_and_emit_load(int64_t(val));

            case Numeric::N_Float:
                if (n->precision == 32)
                    return stack_machine_.add_and_emit_load(float(val));
                else
                    return stack_machine_.add_and_emit_load(double(val));
                break;
        }
    };

    std::string opname;
    switch (e.op().type) {
        default: unreachable("illegal operator");

        /*----- Arithmetic operators ---------------------------------------------------------------------------------*/
        case TK_PLUS:           opname = "Add"; break;
        case TK_MINUS:          opname = "Sub"; break;
        case TK_ASTERISK:       opname = "Mul"; break;
        case TK_SLASH:          opname = "Div"; break;
        case TK_PERCENT:        opname = "Mod"; break;

        /*----- Concatenation operator -------------------------------------------------------------------------------*/
        case TK_DOTDOT:         opname = "Cat"; break;

        /*----- Comparison operators ---------------------------------------------------------------------------------*/
        case TK_LESS:           opname = "LT";  break;
        case TK_GREATER:        opname = "GT";  break;
        case TK_LESS_EQUAL:     opname = "LE";  break;
        case TK_GREATER_EQUAL:  opname = "GE";  break;
        case TK_EQUAL:          opname = "Eq";  break;
        case TK_BANG_EQUAL:     opname = "NE";  break;

        /*----- Logical operators ------------------------------------------------------------------------------------*/
        case TK_And:            opname = "And"; break;
        case TK_Or:             opname = "Or";  break;
    }

    switch (e.op().type) {
        default: unreachable("illegal operator");

        /*----- Arithmetic operators ---------------------------------------------------------------------------------*/
        case TK_PLUS:
        case TK_MINUS: {
            (*this)(*e.lhs);
            scale(ty_lhs, ty);
            emit_cast(ty_lhs, ty);

            (*this)(*e.rhs);
            scale(ty_rhs, ty);
            emit_cast(ty_rhs, ty);

            std::string opstr = e.op().type == TK_PLUS ? "Add" : "Sub";
            opstr += tystr_to;
            auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
            stack_machine_.emit(opcode);
            break;
        }

        case TK_ASTERISK: {
            auto n_lhs = as<const Numeric>(ty_lhs);
            auto n_rhs = as<const Numeric>(ty_rhs);
            auto n_res = as<const Numeric>(ty);
            int64_t the_scale = 0;

            (*this)(*e.lhs);
            if (n_lhs->is_floating_point()) {
                scale(n_lhs, n_res); // scale float up before cast to preserve decimal places
                the_scale += n_res->scale;
            } else {
                the_scale += n_lhs->scale;
            }
            emit_cast(n_lhs, n_res);

            (*this)(*e.rhs);
            if (n_rhs->is_floating_point()) {
                scale(n_rhs, n_res); // scale float up before cast to preserve decimal places
                the_scale += n_res->scale;
            } else {
                the_scale += n_rhs->scale;
            }
            emit_cast(n_rhs, n_res);

            std::string opstr = "Mul";
            opstr += tystr_to;
            auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
            stack_machine_.emit(opcode); // Mul_x

            /* Scale down again, if necessary. */
            the_scale -= n_res->scale;
            insist(the_scale >= 0);
            if (the_scale != 0) {
                insist(n_res->is_decimal());
                const int64_t factor = powi<int64_t>(10, the_scale);
                load_numeric(factor, n_res);
                stack_machine_.emit_Div_i();
            }

            break;
        }

        case TK_SLASH: {
            /* Division with potentially different numeric types is a tricky thing.  Not only must we convert the values
             * from their original data type to the type of the result, but we also must scale them correctly.
             *
             * The effective scale of the result is computed as `scale_lhs - scale_rhs`.
             *
             * (1) If the effective scale of the result is *less* than the expected scale of the result, the LHS must be
             *     scaled up.
             *
             * (2) If the effective scale of the result is *greater* than the expected scale of the result, the result
             *     must be scaled down.
             *
             * With these rules we can achieve maximum precision within the rules of the type system.
             */

            auto n_lhs = as<const Numeric>(ty_lhs);
            auto n_rhs = as<const Numeric>(ty_rhs);
            auto n_res = as<const Numeric>(ty);
            int64_t the_scale = 0;

            (*this)(*e.lhs);
            if (n_lhs->is_floating_point()) {
                scale(n_lhs, n_res); // scale float up before cast to preserve decimal places
                the_scale += n_res->scale;
            } else {
                the_scale += n_lhs->scale;
            }
            emit_cast(n_lhs, n_res);

            if (n_rhs->is_floating_point())
                the_scale -= n_res->scale;
            else
                the_scale -= n_rhs->scale;

            if (the_scale < n_res->scale) {
                const int64_t factor = powi<int64_t>(10, n_res->scale - the_scale); // scale up
                load_numeric(factor, n_res);
                stack_machine_.emit_Mul_i();
            }

            (*this)(*e.rhs);
            if (n_rhs->is_floating_point())
                scale(n_rhs, n_res); // scale float up before cast to preserve decimal places
            emit_cast(n_rhs, n_res);

            std::string opstr = "Div";
            opstr += tystr_to;
            auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
            stack_machine_.emit(opcode); // Div_x

            if (the_scale > n_res->scale) {
                const int64_t factor = powi<int64_t>(10, the_scale - n_res->scale); // scale down
                load_numeric(factor, n_res);
                stack_machine_.emit_Div_i();
            }

            break;
        }

        case TK_PERCENT:
            (*this)(*e.lhs);
            (*this)(*e.rhs);
            stack_machine_.emit_Mod_i();
            break;

        /*----- Concatenation operator -------------------------------------------------------------------------------*/
        case TK_DOTDOT:
            (*this)(*e.lhs);
            (*this)(*e.rhs);
            stack_machine_.emit_Cat_s();
            break;

        /*----- Comparison operators ---------------------------------------------------------------------------------*/
        case TK_LESS:
        case TK_GREATER:
        case TK_LESS_EQUAL:
        case TK_GREATER_EQUAL:
        case TK_EQUAL:
        case TK_BANG_EQUAL:
            if (ty_lhs->is_numeric()) {
                insist(ty_rhs->is_numeric());
                auto n_lhs = as<const Numeric>(ty_lhs);
                auto n_rhs = as<const Numeric>(ty_rhs);
                auto n_res = arithmetic_join(n_lhs, n_rhs);

                (*this)(*e.lhs);
                scale(n_lhs, n_res);
                emit_cast(n_lhs, n_res);

                (*this)(*e.rhs);
                scale(n_rhs, n_res);
                emit_cast(n_rhs, n_res);

                std::string opstr = opname + tystr(n_res);
                auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
                stack_machine_.emit(opcode);
            } else {
                (*this)(*e.lhs);
                (*this)(*e.rhs);
                std::string opstr = opname + tystr(ty_lhs);
                auto opcode = StackMachine::STR_TO_OPCODE.at(opstr);
                stack_machine_.emit(opcode);
            }
            break;

        /*----- Logical operators ------------------------------------------------------------------------------------*/
        case TK_And:
            (*this)(*e.lhs);
            (*this)(*e.rhs);
            stack_machine_.emit_And_b();
            break;

        case TK_Or:
            (*this)(*e.lhs);
            (*this)(*e.rhs);
            stack_machine_.emit_Or_b();
            break;
    }
}


/*======================================================================================================================
 * Stack Machine
 *====================================================================================================================*/

const std::unordered_map<std::string, StackMachine::Opcode> StackMachine::STR_TO_OPCODE = {
#define DB_OPCODE(CODE, ...) { #CODE, StackMachine::Opcode:: CODE },
#include "tables/Opcodes.tbl"
#undef DB_OPCODE
};

StackMachine::StackMachine(Schema in_schema, const Expr &expr)
    : in_schema(in_schema)
{
    emit(expr, 1);
    // TODO emit St
}

StackMachine::StackMachine(Schema in_schema, const cnf::CNF &cnf)
    : in_schema(in_schema)
{
    emit(cnf);
    // TODO emit St
}

void StackMachine::emit(const Expr &expr, std::size_t tuple_id)
{
    StackMachineBuilder Builder(*this, in_schema, expr, tuple_id); // compute the command sequence for this stack machine
}

void StackMachine::emit(const cnf::CNF &cnf, std::size_t tuple_id)
{
    /* Compile filter into stack machine.  TODO: short-circuit evaluation. */
    for (auto clause_it = cnf.cbegin(); clause_it != cnf.cend(); ++clause_it) {
        auto &C = *clause_it;
        for (auto pred_it = C.cbegin(); pred_it != C.cend(); ++pred_it) {
            auto &P = *pred_it;
            emit(*P.expr(), tuple_id); // emit code for predicate
            if (P.negative())
                ops.push_back(StackMachine::Opcode::Not_b); // negate if negative
            if (pred_it != C.cbegin())
                ops.push_back(StackMachine::Opcode::Or_b);
        }
        if (clause_it != std::prev(cnf.cend()))
            ops.push_back(StackMachine::Opcode::Stop_False); // a single false clause renders the CNF false
        if (clause_it != cnf.cbegin())
            ops.push_back(StackMachine::Opcode::And_b);
    }
    out_schema.push_back(Type::Get_Boolean(Type::TY_Vector));
}

void StackMachine::emit_St_Tup(std::size_t tuple_id, std::size_t index, const Type *ty)
{
    if (ty->is_none()) {
        emit_St_Tup_Null(tuple_id, index);
    } else {
        std::ostringstream oss;
        oss << "St_Tup" << tystr(as<const PrimitiveType>(ty));
        auto opcode = StackMachine::STR_TO_OPCODE.at(oss.str());
        emit(opcode);
        emit(static_cast<Opcode>(tuple_id));
        emit(static_cast<Opcode>(index));
    }
}

void StackMachine::operator()(Tuple **tuples) const
{
    static const void *labels[] = {
#define DB_OPCODE(CODE, ...) && CODE,
#include "tables/Opcodes.tbl"
#undef DB_OPCODE
    };

    const_cast<StackMachine*>(this)->emit_Stop();
    if (not values_) {
        values_ = new Value[required_stack_size()];
        null_bits_ = new bool[required_stack_size()]();
    }
    top_ = 0; // points to the top of the stack, i.e. the top-most entry
    op_ = ops.cbegin();
    auto p_mem = memory_; // pointer to free memory; used like a linear allocator

#define NEXT goto *labels[std::size_t(*op_++)]

#define PUSH(VAL, NUL) { \
    insist(top_ < required_stack_size(), "index out of bounds"); \
    values_[top_] = (VAL); \
    null_bits_[top_] = (NUL); \
    ++top_; \
}
#define POP() --top_
#define TOP_IS_NULL (null_bits_[top_ - 1UL])
#define TOP (values_[top_ - 1UL])

    NEXT;


/*======================================================================================================================
 * Control flow operations
 *====================================================================================================================*/

Stop_Z: {
    insist(top_ >= 1);
    if (TOP.as_i() == 0) goto Stop; // stop evaluation on ZERO
}
NEXT;

Stop_NZ: {
    insist(top_ >= 1);
    if (TOP.as_i() != 0) goto Stop; // stop evaluation on NOT ZERO
}
NEXT;

Stop_False: {
    insist(top_ >= 1);
    if (not TOP.as_b()) goto Stop; // stop evaluation on FALSE
}
NEXT;

Stop_True: {
    insist(top_ >= 1);
    if (TOP.as_b()) goto Stop; // stop evaluation on TRUE
}
NEXT;


/*======================================================================================================================
 * Stack manipulation operations
 *====================================================================================================================*/

Pop:
    POP();
    NEXT;

Push_Null:
    PUSH(0, true);
    NEXT;


/*======================================================================================================================
 * Tuple Access Operations
 *====================================================================================================================*/

Ld_Tup: {
    std::size_t tuple_id = std::size_t(*op_++);
    std::size_t index = std::size_t(*op_++);
    auto &t = *tuples[tuple_id];
    PUSH(t[index], t.is_null(index));
}
NEXT;

St_Tup_Null: {
    std::size_t tuple_id = std::size_t(*op_++);
    std::size_t index = std::size_t(*op_++);
    auto &t = *tuples[tuple_id];
    t.null(index);
}
NEXT;

St_Tup_b:
St_Tup_i:
St_Tup_f:
St_Tup_d: {
    std::size_t tuple_id = std::size_t(*op_++);
    std::size_t index = std::size_t(*op_++);
    auto &t = *tuples[tuple_id];
    t.set(index, TOP, TOP_IS_NULL);
}
NEXT;

St_Tup_s: {
    std::size_t tuple_id = std::size_t(*op_++);
    std::size_t index = std::size_t(*op_++);
    auto &t = *tuples[tuple_id];
    t.not_null(index);
    strcpy(reinterpret_cast<char*>(t[index].as_p()), reinterpret_cast<char*>(TOP.as_p()));
}
NEXT;


/*======================================================================================================================
 * Output operations
 *====================================================================================================================*/

/* Load a value from the context to the top of the value_stack_. */
Ld_Ctx: {
    std::size_t idx = std::size_t(*op_++);
    insist(idx < context_.size(), "index out of bounds");
    PUSH(context_[idx], false);
}
NEXT;

Upd_Ctx: {
    std::size_t idx = static_cast<std::size_t>(*op_++);
    insist(idx < context_.size(), "index out of bounds");
    const_cast<StackMachine*>(this)->context_[idx] = TOP;
}
NEXT;

/*----- Load from row store ------------------------------------------------------------------------------------------*/
#define PREPARE \
    insist(top_ >= 3); \
\
    /* Get value bit offset. */ \
    auto value_off = std::size_t(TOP.as_i()); \
    const std::size_t bytes = value_off / 8; \
    POP(); \
\
    /* Get null bit offset. */ \
    auto null_off = std::size_t(TOP.as_i()); \
    POP(); \
\
    /* Row address. */ \
    auto addr = reinterpret_cast<uint8_t*>(TOP.as_i());

#define PREPARE_LOAD \
    PREPARE \
\
    /* Check if null. */ \
    { \
        const std::size_t bytes = null_off / 8; \
        const std::size_t bits = null_off % 8; \
        bool is_null = not bool((*(addr + bytes) >> bits) & 0x1); \
        TOP_IS_NULL = is_null; \
        if (is_null) \
            NEXT; \
    } \

Ld_RS_i8: {
    PREPARE_LOAD;
    TOP = int64_t(*reinterpret_cast<int8_t*>(addr + bytes));
}
NEXT;

Ld_RS_i16: {
    PREPARE_LOAD;
    TOP = int64_t(*reinterpret_cast<int16_t*>(addr + bytes));
}
NEXT;

Ld_RS_i32: {
    PREPARE_LOAD;
    TOP = int64_t(*reinterpret_cast<int32_t*>(addr + bytes));
}
NEXT;

Ld_RS_i64: {
    PREPARE_LOAD;
    TOP = *reinterpret_cast<int64_t*>(addr + bytes);
}
NEXT;

Ld_RS_f: {
    PREPARE_LOAD;
    TOP = *reinterpret_cast<float*>(addr + bytes);
}
NEXT;

Ld_RS_d: {
    PREPARE_LOAD;
    TOP = *reinterpret_cast<double*>(addr + bytes);
}
NEXT;

Ld_RS_s: {
    auto len = TOP.as_i();
    POP();
    PREPARE_LOAD;
    strncpy(reinterpret_cast<char*>(p_mem), reinterpret_cast<char*>(addr + bytes), len);
    p_mem[len] = 0; // terminating NUL byte
    TOP = p_mem;
    p_mem += len + 1;
}
NEXT;

Ld_RS_b: {
    PREPARE_LOAD;
    const std::size_t bits = value_off % 8;
    TOP = bool((*reinterpret_cast<uint8_t*>(addr + bytes) >> bits) & 0x1);
}
NEXT;

#undef PREPARE_LOAD

#define PREPARE_STORE(TYPE) \
    PREPARE \
    POP(); \
\
    /* Set null bit. */ \
    { \
        const std::size_t bytes = null_off / 8; \
        const std::size_t bits = null_off % 8; \
        bool is_null = TOP_IS_NULL; \
        setbit(addr + bytes, not is_null, bits); \
        if (is_null) { \
            POP(); \
            NEXT; \
        } \
    } \
\
    auto val = TOP; \
    POP();

St_RS_i8: {
    PREPARE_STORE(int64_t);
    auto p = reinterpret_cast<int8_t*>(addr + bytes);
    *p = val.as_i();;
}
NEXT;

St_RS_i16: {
    PREPARE_STORE(int64_t);
    auto p = reinterpret_cast<int16_t*>(addr + bytes);
    *p = val.as_i();
}
NEXT;

St_RS_i32: {
    PREPARE_STORE(int64_t);
    auto p = reinterpret_cast<int32_t*>(addr + bytes);
    *p = val.as_i();
}
NEXT;

St_RS_i64: {
    PREPARE_STORE(int64_t);
    auto p = reinterpret_cast<int64_t*>(addr + bytes);
    *p = val.as_i();
}
NEXT;

St_RS_f: {
    PREPARE_STORE(float);
    auto p = reinterpret_cast<float*>(addr + bytes);
    *p = val.as_f();
}
NEXT;

St_RS_d: {
    PREPARE_STORE(double);
    auto p = reinterpret_cast<double*>(addr + bytes);
    *p = val.as_d();
}
NEXT;

St_RS_s: {
    /* TODO not yet supported */
    POP();
    PREPARE_STORE(char);
    (void) bytes;
    (void) val;
}
NEXT;

St_RS_b: {
    PREPARE_STORE(bool);
    const auto bits = value_off % 8;
    setbit(addr + bytes, val.as_b(), bits);
}
NEXT;

#undef PREPARE_STORE

#undef PREPARE

/*----- Load from column store ---------------------------------------------------------------------------------------*/
#define PREPARE(TYPE) \
    insist(top_ >= 4); \
\
    /* Get attribute id. */ \
    auto attr_id = std::size_t(TOP.as_i()); \
    POP(); \
\
    /* Get address of value column. */ \
    TYPE *value_col_addr = reinterpret_cast<TYPE*>(uintptr_t(TOP.as_i())); \
    POP(); \
\
    /* Get address of null bitmap column. */ \
    int64_t *null_bitmap_col_addr = reinterpret_cast<int64_t*>(uintptr_t(TOP.as_i())); \
    POP(); \
\
    /* Get row id. */ \
    auto row_id = std::size_t(TOP.as_i());

#define PREPARE_LOAD(TYPE) \
    PREPARE(TYPE) \
\
    /* Check if null. */ \
    bool is_null = not ((null_bitmap_col_addr[row_id] >> attr_id) & 0x1); \
    TOP_IS_NULL = is_null;

Ld_CS_i8: {
    PREPARE_LOAD(int8_t);
    TOP = int64_t(value_col_addr[row_id]);
}
NEXT;

Ld_CS_i16: {
    PREPARE_LOAD(int16_t);
    TOP = int64_t(value_col_addr[row_id]);
}
NEXT;

Ld_CS_i32: {
    PREPARE_LOAD(int32_t);
    TOP = int64_t(value_col_addr[row_id]);
}
NEXT;

Ld_CS_i64: {
    PREPARE_LOAD(int64_t);
    TOP = int64_t(value_col_addr[row_id]);
}
NEXT;

Ld_CS_f: {
    PREPARE_LOAD(float);
    TOP = float(value_col_addr[row_id]);
}
NEXT;

Ld_CS_d: {
    PREPARE_LOAD(double);
    TOP = double(value_col_addr[row_id]);
}
NEXT;

Ld_CS_s: {
    auto len = TOP.as_i();
    POP();
    PREPARE_LOAD(char);
    auto str = value_col_addr + len * row_id;
    strncpy(reinterpret_cast<char*>(p_mem), reinterpret_cast<char*>(str), len);
    p_mem[len] = 0; // terminating NUL byte
    TOP = p_mem;
    p_mem += len + 1;
}
NEXT;

Ld_CS_b: {
    PREPARE_LOAD(uint8_t);
    const std::size_t bytes = row_id / 8;
    const std::size_t bits = row_id % 8;
    TOP = bool((value_col_addr[bytes] >> bits) & 0x1);
}
NEXT;

#undef PREPARE_LOAD

#define PREPARE_STORE(TO_TYPE, FROM_TYPE) \
    PREPARE(TO_TYPE); \
    POP(); \
\
    /* Set null bit. */ \
    { \
        bool is_null = TOP_IS_NULL; \
        setbit(&null_bitmap_col_addr[row_id], not is_null, attr_id); \
        if (is_null) { \
            POP(); \
            NEXT; \
        } \
    } \
\
    auto val = TOP; \
    POP();

St_CS_i8: {
    PREPARE_STORE(int8_t, int64_t);
    value_col_addr[row_id] = val.as_i();
}
NEXT;

St_CS_i16: {
    PREPARE_STORE(int16_t, int64_t);
    value_col_addr[row_id] = val.as_i();
}
NEXT;

St_CS_i32: {
    PREPARE_STORE(int32_t, int64_t);
    value_col_addr[row_id] = val.as_i();
}
NEXT;

St_CS_i64: {
    PREPARE_STORE(int64_t, int64_t);
    value_col_addr[row_id] = val.as_i();
}
NEXT;

St_CS_f: {
    PREPARE_STORE(float, float);
    value_col_addr[row_id] = val.as_f();
}
NEXT;

St_CS_d: {
    PREPARE_STORE(double, double);
    value_col_addr[row_id] = val.as_d();
}
NEXT;

St_CS_s: {
    /* TODO not yet supported */
    POP();
    PREPARE_STORE(char, char);
    (void) value_col_addr;
    (void) val;
}
NEXT;

St_CS_b: {
    PREPARE_STORE(bool, bool);
    const auto bytes = row_id / 8;
    const auto bits = row_id % 8;
    setbit(value_col_addr + bytes, val.as_b(), bits);
}
NEXT;

#undef PREPARE_STORE

#undef PREPARE


/*======================================================================================================================
 * Arithmetical operations
 *====================================================================================================================*/

#define UNARY(OP, TYPE) { \
    insist(top_ >= 1); \
    TYPE val = TOP.as<TYPE>(); \
    TOP = OP(val); \
} \
NEXT;

#define BINARY(OP, TYPE) { \
    insist(top_ >= 2); \
    TYPE rhs = TOP.as<TYPE>(); \
    bool is_rhs_null = TOP_IS_NULL; \
    POP(); \
    TYPE lhs = TOP.as<TYPE>(); \
    TOP = OP(lhs, rhs); \
    TOP_IS_NULL = TOP_IS_NULL or is_rhs_null; \
} \
NEXT;

/* Integral increment. */
Inc: UNARY(++, int64_t);

/* Integral decrement. */
Dec: UNARY(--, int64_t);

/* Bitwise negation */
Neg_i: UNARY(~, int64_t);

/* Arithmetic negation */
Minus_i: UNARY(-, int64_t);
Minus_f: UNARY(-, float);
Minus_d: UNARY(-, double);

/* Add two values. */
Add_i: BINARY(std::plus{}, int64_t);
Add_f: BINARY(std::plus{}, float);
Add_d: BINARY(std::plus{}, double);

/* Subtract two values. */
Sub_i: BINARY(std::minus{}, int64_t);
Sub_f: BINARY(std::minus{}, float);
Sub_d: BINARY(std::minus{}, double);

/* Multiply two values. */
Mul_i: BINARY(std::multiplies{}, int64_t);
Mul_f: BINARY(std::multiplies{}, float);
Mul_d: BINARY(std::multiplies{}, double);

/* Divide two values. */
Div_i: BINARY(std::divides{}, int64_t);
Div_f: BINARY(std::divides{}, float);
Div_d: BINARY(std::divides{}, double);

/* Modulo divide two values. */
Mod_i: BINARY(std::modulus{}, int64_t);

/* Concatenate two strings. */
Cat_s: {
    bool rhs_is_null = TOP_IS_NULL;
    Value rhs = TOP;
    POP();
    bool lhs_is_null = TOP_IS_NULL;
    Value lhs = TOP;

    if (rhs_is_null)
        NEXT; // nothing to be done
    if (lhs_is_null) {
        TOP = rhs;
        TOP_IS_NULL = rhs_is_null;
        NEXT;
    }

    insist(not rhs_is_null and not lhs_is_null);
    char *dest = reinterpret_cast<char*>(p_mem);
    TOP = dest;
    /* Append LHS. */
    for (auto src = lhs.as<char*>(); *src; ++src, ++dest)
        *dest = *src;
    /* Append RHS. */
    for (auto src = rhs.as<char*>(); *src; ++src, ++dest)
        *dest = *src;
    *dest++ = 0; // terminating NUL byte
    p_mem = reinterpret_cast<uint8_t*>(dest);
}
NEXT;


/*======================================================================================================================
 * Logical operations
 *====================================================================================================================*/

/* Logical not */
Not_b: UNARY(not, bool);

/* Logical and with three-valued logic (https://en.wikipedia.org/wiki/Three-valued_logic#Kleene_and_Priest_logics). */
And_b: {
    insist(top_ >= 2);
    bool rhs = TOP.as<bool>();
    bool is_rhs_null = TOP_IS_NULL;
    POP();
    bool lhs = TOP.as<bool>();
    bool is_lhs_null = TOP_IS_NULL;
    TOP = lhs and rhs;
    TOP_IS_NULL = (lhs or is_lhs_null) and (rhs or is_rhs_null) and (is_lhs_null or is_rhs_null);
}
NEXT;

/* Logical or with three-valued logic (https://en.wikipedia.org/wiki/Three-valued_logic#Kleene_and_Priest_logics). */
Or_b: {
    insist(top_ >= 2);
    bool rhs = TOP.as<bool>();
    bool is_rhs_null = TOP_IS_NULL;
    POP();
    bool lhs = TOP.as<bool>();
    bool is_lhs_null = TOP_IS_NULL;
    TOP = lhs or rhs;
    TOP_IS_NULL = (not lhs or is_lhs_null) and (not rhs or is_rhs_null) and (is_lhs_null or is_rhs_null);
}
NEXT;


/*======================================================================================================================
 * Comparison operations
 *====================================================================================================================*/

Eq_i: BINARY(std::equal_to{}, int64_t);
Eq_f: BINARY(std::equal_to{}, float);
Eq_d: BINARY(std::equal_to{}, double);
Eq_b: BINARY(std::equal_to{}, bool);
Eq_s: BINARY(streq, char*);

NE_i: BINARY(std::not_equal_to{}, int64_t);
NE_f: BINARY(std::not_equal_to{}, float);
NE_d: BINARY(std::not_equal_to{}, double);
NE_b: BINARY(std::not_equal_to{}, bool);
NE_s: BINARY(not streq, char*);

LT_i: BINARY(std::less{}, int64_t);
LT_f: BINARY(std::less{}, float);
LT_d: BINARY(std::less{}, double);
LT_s: BINARY(0 > strcmp, char*)

GT_i: BINARY(std::greater{}, int64_t);
GT_f: BINARY(std::greater{}, float);
GT_d: BINARY(std::greater{}, double);
GT_s: BINARY(0 < strcmp, char*);

LE_i: BINARY(std::less_equal{}, int64_t);
LE_f: BINARY(std::less_equal{}, float);
LE_d: BINARY(std::less_equal{}, double);
LE_s: BINARY(0 >= strcmp, char*);

GE_i: BINARY(std::greater_equal{}, int64_t);
GE_f: BINARY(std::greater_equal{}, float);
GE_d: BINARY(std::greater_equal{}, double);
GE_s: BINARY(0 <= strcmp, char*);

#define CMP(TYPE) { \
    insist(top_ >= 2); \
    TYPE rhs = TOP.as<TYPE>(); \
    bool is_rhs_null = TOP_IS_NULL; \
    POP(); \
    TYPE lhs = TOP.as<TYPE>(); \
    bool is_lhs_null = TOP_IS_NULL; \
    TOP = int64_t(lhs >= rhs) - int64_t(lhs <= rhs); \
    TOP_IS_NULL = is_lhs_null or is_rhs_null; \
} \
NEXT;

Cmp_i: CMP(int64_t);
Cmp_f: CMP(float);
Cmp_d: CMP(double);
Cmp_b: CMP(bool);
Cmp_s: BINARY(strcmp, char*);

#undef CMP


/*======================================================================================================================
 * Intrinsic functions
 *====================================================================================================================*/

Is_Null:
    TOP = bool(TOP_IS_NULL);
    TOP_IS_NULL = false;
    NEXT;

/* Cast to int. */
Cast_i_f: UNARY((int64_t), float);
Cast_i_d: UNARY((int64_t), double);
Cast_i_b: UNARY((int64_t), bool);

/* Cast to float. */
Cast_f_i: UNARY((float), int64_t);
Cast_f_d: UNARY((float), double);

/* Cast to double. */
Cast_d_i: UNARY((double), int64_t);
Cast_d_f: UNARY((double), float);

#undef BINARY
#undef UNARY

Stop:
    const_cast<StackMachine*>(this)->ops.pop_back(); // terminating Stop

#if 0
    for (std::size_t i = 0; i != top_; ++i) {
        if (auto cs = cast<const CharacterSequence>(out_schema[i])) {
            out->not_null(i);
            strncpy((*out)[i].as<char*>(), values_[i].as<char*>(), cs->length);
            (*out)[i].as<char*>()[cs->length] = 0; // terminating NUL byte
        } else {
            out->set(i, values_[i], null_bits_[i]);
        }
    }
#endif

    op_ = ops.cbegin();
    top_ = 0;
}

void StackMachine::dump(std::ostream &out) const
{
    out << "StackMachine\n    Context: [";
    for (auto it = context_.cbegin(); it != context_.cend(); ++it) {
        if (it != context_.cbegin()) out << ", ";
        out << *it;
    }
    out << ']'
        << "\n    Input Schema:  " << in_schema
        << "\n    Output Schema: {[";
    for (auto it = out_schema.begin(), end = out_schema.end(); it != end; ++it) {
        if (it != out_schema.begin()) out << ',';
        out << ' ' << **it;
    }
    out << " ]}"
        << "\n    Opcode Sequence:\n";
    const std::size_t current_op = op_ - ops.begin();
    for (std::size_t i = 0; i != ops.size(); ++i) {
        auto opc = ops[i];
        if (i == current_op)
            out << "    --> ";
        else
            out << "        ";
        out << "[0x" << std::hex << std::setfill('0') << std::setw(4) << i << std::dec << "]: "
            << StackMachine::OPCODE_TO_STR[static_cast<std::size_t>(opc)];
        switch (opc) {
            /* Opcodes with *two* operands. */
            case Opcode::Ld_Tup:
            case Opcode::St_Tup_i:
            case Opcode::St_Tup_f:
            case Opcode::St_Tup_d:
            case Opcode::St_Tup_s:
            case Opcode::St_Tup_b:
                ++i;
                out << ' ' << static_cast<int64_t>(ops[i]);
                /* fall through */

            /* Opcodes with *one* operand. */
            case Opcode::Ld_Ctx:
            case Opcode::Upd_Ctx:
                ++i;
                out << ' ' << static_cast<int64_t>(ops[i]);
                /* fall through */

            default:;
        }
        out << '\n';
    }
    out << "    Stack:\n";
    for (std::size_t i = top_; i --> 0; ) {
        if (null_bits_[i])
            out << "      NULL\n";
        else
            out << "      " << values_[i] << '\n';
    }
    out.flush();
}

void StackMachine::dump() const { dump(std::cerr); }
