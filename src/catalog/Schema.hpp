#pragma once

#include "util/fn.hpp"
#include "util/macro.hpp"
#include "util/Pool.hpp"
#include "util/StringPool.hpp"
#include <functional>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace db {

/*======================================================================================================================
 * SQL Types
 *====================================================================================================================*/

struct ErrorType;
struct PrimitiveType;
struct Boolean;
struct CharacterSequence;
struct Numeric;
struct FnType;

struct Type
{
#define category_t(X) X(TY_Scalar), X(TY_Vector)
    DECLARE_ENUM(category_t); ///< a category for whether this type is scalar or vector
    protected:
    static constexpr const char *CATEGORY_TO_STR_[] = { ENUM_TO_STR(category_t) };
#undef category_t

    protected:
    static Pool<Type> types_; ///< a pool of parameterized types

    public:
    Type() = default;
    Type(const Type&) = delete;
    Type(Type&&) = default;
    virtual ~Type() { }

    virtual bool operator==(const Type &other) const = 0;
    bool operator!=(const Type &other) const { return not operator==(other); }

    bool is_error() const { return (void*) this == Get_Error(); }
    bool is_primitive() const { return is<const PrimitiveType>(this); }
    bool is_boolean() const { return is<const Boolean>(this); }
    bool is_character_sequence() const { return is<const CharacterSequence>(this); }
    bool is_numeric() const { return is<const Numeric>(this); }

    virtual uint64_t hash() const = 0;

    virtual void print(std::ostream &out) const = 0;
    virtual void dump(std::ostream &out) const = 0;
    void dump() const;

    friend std::ostream & operator<<(std::ostream &out, const Type &t) {
        t.print(out);
        return out;
    }

    /* Type factory methods */
    static const ErrorType * Get_Error();
    static const Boolean * Get_Boolean(category_t category);
    static const CharacterSequence * Get_Char(category_t category, std::size_t length);
    static const CharacterSequence * Get_Varchar(category_t category, std::size_t length);
    static const Numeric * Get_Decimal(category_t category, unsigned digits, unsigned scale);
    static const Numeric * Get_Integer(category_t category, unsigned num_bytes);
    static const Numeric * Get_Float(category_t category);
    static const Numeric * Get_Double(category_t category);
    static const FnType * Get_Function(const Type *return_type, std::vector<const Type*> parameter_types);
};

}

namespace std {

template<>
struct hash<db::Type>
{
    uint64_t operator()(const db::Type &type) const { return type.hash(); }
};

}

namespace db {

/** Primitive types are used for values. */
struct PrimitiveType : Type
{
    category_t category; ///< whether this type is scalar or vector

    PrimitiveType(category_t category) : category(category) { }
    PrimitiveType(const PrimitiveType&) = delete;
    PrimitiveType(PrimitiveType&&) = default;
    virtual ~PrimitiveType() { }

    bool is_scalar() const { return category == TY_Scalar; }
    bool is_vectorial() const { return category == TY_Vector; }

    /** Convert this type to a scalar. */
    virtual const PrimitiveType *as_scalar() const = 0;

    /** Convert this type to a vectorial. */
    virtual const PrimitiveType *as_vectorial() const = 0;
};

/** The error type.  Used when parsing of a data type fails or when semantic analysis detects a type error. */
struct ErrorType: Type
{
    friend struct Type;

    private:
    ErrorType() { }

    public:
    ErrorType(ErrorType&&) = default;

    bool operator==(const Type &other) const;

    uint64_t hash() const;

    void print(std::ostream &out) const;
    void dump(std::ostream &out) const;
};

/** The boolean type. */
struct Boolean : PrimitiveType
{
    friend struct Type;

    private:
    Boolean(category_t category) : PrimitiveType(category) { }

    public:
    Boolean(Boolean&&) = default;

    bool operator==(const Type &other) const;

    uint64_t hash() const;

    void print(std::ostream &out) const;
    void dump(std::ostream &out) const;

    virtual const PrimitiveType *as_scalar() const;
    virtual const PrimitiveType *as_vectorial() const;
};

/** The type of character strings, both fixed length and varying. */
struct CharacterSequence : PrimitiveType
{
    friend struct Type;

    std::size_t length; ///> the maximum length of the string in bytes
    bool is_varying; ///> true if varying, false otherwise; corresponds to Char(N) and Varchar(N)

    private:
    CharacterSequence(category_t category, std::size_t length, bool is_varying)
        : PrimitiveType(category)
        , length(length)
        , is_varying(is_varying)
    { }

    public:
    CharacterSequence(CharacterSequence&&) = default;

    bool operator==(const Type &other) const;

    uint64_t hash() const;

    void print(std::ostream &out) const;
    void dump(std::ostream &out) const;

    virtual const PrimitiveType *as_scalar() const;
    virtual const PrimitiveType *as_vectorial() const;
};

/** The numeric type represents integer and floating-point types of different precision, scale, and exactness. */
struct Numeric : PrimitiveType
{
    friend struct Type;

    /** The maximal number of decimal digits that can be accurately represented by DECIMAL(p,s). */
    static constexpr std::size_t MAX_DECIMAL_PRECISION = 19;

#define kind_t(X) X(N_Int), X(N_Float), X(N_Decimal)
    DECLARE_ENUM(kind_t) kind; ///> the kind of numeric type
    private:
    static constexpr const char *KIND_TO_STR_[] = { ENUM_TO_STR(kind_t) };
#undef kind_t
    public:
    /** The precision gives the maximum number of digits that can be represented by that type.  Its interpretation
     * depends on the kind:
     *  For INT, precision is the number of bytes.
     *  For FLOAT and DOUBLE, precision is the size of the type in bits, i.e. 32 and 64, respectively.
     *  For DECIMAL, precision is the number of decimal digits that can be represented.
     */
    unsigned precision; ///> the number of bits used to represent the number
    unsigned scale; ///> the number of decimal digits right of the decimal point

    private:
    Numeric(category_t category, kind_t kind, unsigned precision, unsigned scale)
        : PrimitiveType(category)
        , kind(kind)
        , precision(precision)
        , scale(scale)
    { }

    public:
    Numeric(Numeric&&) = default;

    bool operator==(const Type &other) const;

    uint64_t hash() const;

    void print(std::ostream &out) const;
    void dump(std::ostream &out) const;

    virtual const PrimitiveType *as_scalar() const;
    virtual const PrimitiveType *as_vectorial() const;
};

/** The function type defines the type and count of the arguments and the type of the return value of a SQL function. */
struct FnType : Type
{
    friend struct Type;

    const Type *return_type; ///> the type of the return value
    std::vector<const Type *> parameter_types; ///> the types of the parameters

    private:
    FnType(const Type *return_type, std::vector<const Type*> parameter_types)
        : return_type(notnull(return_type))
        , parameter_types(parameter_types)
    { }

    public:
    FnType(FnType&&) = default;

    bool operator==(const Type &other) const;

    uint64_t hash() const;

    void print(std::ostream &out) const;
    void dump(std::ostream &out) const;
};

/*======================================================================================================================
 * Attribute, Table, Database
 *====================================================================================================================*/

struct Table;
struct Database;
struct Catalog;

/** An attribute of a table.  Every attribute belongs to exactly one table.  */
struct Attribute
{
    friend struct Table;

    std::size_t id; ///> the internal identifier of the attribute, unique within its table
    const Table &table; ///> the table the attribute belongs to
    const PrimitiveType *type; ///> the type of the attribute
    const char *name; ///> the name of the attribute

    private:
    explicit Attribute(std::size_t id, const Table &table, const PrimitiveType *type, const char *name)
        : id(id)
        , table(table)
        , type(notnull(type))
        , name(notnull(name))
    {
        insist(type->is_vectorial()); // attributes are always of vectorial type
    }

    public:
    Attribute(const Attribute&) = delete;
    Attribute(Attribute&&) = default;

    friend std::ostream & operator<<(std::ostream &out, const Attribute &attr) {
        return out << '`' << attr.name << "` " << *attr.type;
    }

    void dump(std::ostream &out) const;
    void dump() const;
};

/** A table is a sorted set of attributes. */
struct Table
{
    const char *name;
    private:
    using table_type = std::vector<Attribute>;
    /** the attributes of this table */
    table_type attrs_;
    /** maps attribute names to their position within the table */
    std::unordered_map<const char*, table_type::size_type> name_to_attr_;

    public:
    Table(const char *name) : name(name) { }
    ~Table();

    std::size_t size() const { return attrs_.size(); }

    table_type::const_iterator begin()  const { return attrs_.cbegin(); }
    table_type::const_iterator end()    const { return attrs_.cend(); }
    table_type::const_iterator cbegin() const { return attrs_.cbegin(); }
    table_type::const_iterator cend()   const { return attrs_.cend(); }

    const Attribute & at(std::size_t i) const { return attrs_.at(i); }
    const Attribute & at(const char *name) const { return attrs_[name_to_attr_.at(name)]; }
    const Attribute & operator[](std::size_t i) const { return at(i); }
    const Attribute & operator[](const char *name) const { return at(name); }

    const Attribute & push_back(const PrimitiveType *type, const char *name);

    void dump(std::ostream &out) const;
    void dump() const;
};

/** Defines a function.  There are functions pre-defined in the SQL standard and user-defined functions. */
struct Function
{
#define kind_t(X) \
    X(FN_Scalar), \
    X(FN_Aggregate)

    enum fnid_t {
#define DB_FUNCTION(NAME, KIND) FN_ ## NAME,
#include "tables/Functions.tbl"
#undef DB_FUNCTION
        FN_UDF, // for all user-defined functions
    };

    const char *name; ///> the name of the function
    fnid_t fnid; ///> the function id
    DECLARE_ENUM(kind_t) kind; ///< the function kind: Scalar, Aggregate, etc.

    Function(const char *name, fnid_t fnid, kind_t kind) : name(name), fnid(fnid), kind(kind) { }

    bool is_UDF() const { return fnid == FN_UDF; }

    bool is_scalar() const { return kind == FN_Scalar; }
    bool is_aggregate() const { return kind == FN_Aggregate; }

    void dump(std::ostream &out) const;
    void dump() const;

    private:
    static constexpr const char *FNID_TO_STR_[] = {
#define DB_FUNCTION(NAME, KIND) "FN_" #NAME,
#include "tables/Functions.tbl"
#undef DB_FUNCTION
        "FN_UDF",
    };
    static constexpr const char *KIND_TO_STR_[] = { ENUM_TO_STR(kind_t) };
#undef kind_t
};

/** A description of a database.  It is a set of tables, functions, and statistics. */
struct Database
{
    friend struct Catalog;

    public:
    const char *name;
    private:
    std::unordered_map<const char*, Table*> tables_; ///> the tables of this database
    std::unordered_map<const char*, Function*> functions_; ///> functions defined in this database

    private:
    Database(const char *name);

    public:
    ~Database();

    std::size_t size() const { return tables_.size(); }

    /*===== Tables ===================================================================================================*/
    Table & get_table(const char *name) const { return *tables_.at(name); }
    Table & add_table(const char *name) {
        auto it = tables_.find(name);
        if (it != tables_.end()) throw std::invalid_argument("table with that name already exists");
        it = tables_.emplace_hint(it, name, new Table(name));
        return *it->second;
    }
    Table & add(Table *r) {
        auto it = tables_.find(r->name);
        if (it != tables_.end()) throw std::invalid_argument("table with that name already exists");
        it = tables_.emplace_hint(it, r->name, r);
        return *it->second;
    }

    /*===== Functions ================================================================================================*/
    const Function * get_function(const char *name) const { return functions_.at(name); }
};

/** The catalog keeps track of all meta information of the database system.  There is always exactly one catalog. */
struct Catalog
{
    private:
    StringPool pool; ///> pool of strings
    std::unordered_map<const char*, Database*> databases_; ///> the databases
    Database *database_in_use_ = nullptr; ///> the currently used database
    std::unordered_map<const char*, Function*> standard_functions_; ///> functions defined by the SQL standard

    private:
    Catalog();
    Catalog(const Catalog&) = delete;

    public:
    ~Catalog();

    static Catalog & Get() {
        static Catalog the_catalog_;
        return the_catalog_;
    }

    std::size_t num_databases() const { return databases_.size(); }

    StringPool & get_pool() { return pool; }
    const StringPool & get_pool() const { return pool; }

    /*===== Database =================================================================================================*/
    Database & add_database(const char *name) {
        auto it = databases_.find(name);
        if (it != databases_.end()) throw std::invalid_argument("database with that name already exist");
        it = databases_.emplace_hint(it, name, new Database(name));
        return *it->second;
    }
    Database & get_database(const char *name) const { return *databases_.at(name); }
    bool drop_database(const char *name) {
        if (has_database_in_use() and get_database_in_use().name == name)
            throw std::invalid_argument("Cannot drop database; currently in use.");
        return databases_.erase(name) != 0;
    }
    bool drop_database(const Database &S) { return drop_database(S.name); }

    bool has_database_in_use() const { return database_in_use_ != nullptr; }
    Database & get_database_in_use() {
        if (not has_database_in_use())
            throw std::logic_error("no database currently in use");
        return *database_in_use_;
    }
    const Database & get_database_in_use() const {
        if (not has_database_in_use())
            throw std::logic_error("no database currently in use");
        return *database_in_use_;
    }
    void set_database_in_use(Database &s) { database_in_use_ = &s; }
    void unset_database_in_use() { database_in_use_ = nullptr; }

    /*===== Functions ================================================================================================*/
    const Function * get_function(const char *name) const { return standard_functions_.at(name); }
};

}
