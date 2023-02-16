#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <mutable/catalog/CardinalityEstimator.hpp>
#include <mutable/catalog/Type.hpp>
#include <mutable/mutable-config.hpp>
#include <mutable/storage/DataLayout.hpp>
#include <mutable/storage/Store.hpp>
#include <mutable/util/ADT.hpp>
#include <mutable/util/exception.hpp>
#include <mutable/util/fn.hpp>
#include <mutable/util/macro.hpp>
#include <type_traits>
#include <unordered_map>
#include <vector>


namespace m {

namespace storage {

// forward declarations
struct DataLayoutFactory;

}

/** A `Schema` represents a sequence of identifiers, optionally with a prefix, and their associated types.  The `Schema`
 * allows identifiers of the same name with different prefix.  */
struct M_EXPORT Schema
{
    /** An `Identifier` is composed of a name and an optional prefix. */
    struct Identifier
    {
        const char *prefix; ///< prefix of this `Identifier`, may be `nullptr`
        const char *name; ///< the name of this `Identifier`

        Identifier() = default; // XXX: ok?
        Identifier(const char *name) : prefix(nullptr), name(name) { }
        Identifier(const char *prefix, const char *name)
            : prefix(prefix) , name(name)
        {
            if (prefix != nullptr and strlen(prefix) == 0)
                throw invalid_argument("prefix must not be the empty string");
        }
        explicit Identifier(const ast::Expr&);

        bool operator==(Identifier other) const {
            return this->prefix == other.prefix and this->name == other.name;
        }
        bool operator!=(Identifier other) const { return not operator==(other); }

M_LCOV_EXCL_START
        friend std::ostream & operator<<(std::ostream &out, Identifier id) {
            if (id.prefix)
                out << id.prefix << '.';
            return out << id.name;
        }
M_LCOV_EXCL_STOP
    };

    struct entry_type
    {
        enum constraints_t : uint64_t
        {
            NULLABLE = 0b1, ///< entry may be NULL
        };

        Identifier id;
        const Type *type;
        constraints_t constraints;

        public:
        entry_type(Identifier id, const Type *type, constraints_t constraints = NULLABLE)
            : id(id)
            , type(M_notnull(type))
            , constraints(constraints) /* TODO: compute from table constraint */
        { }

        bool nullable() const { return bool(NULLABLE & constraints); }
    };

    private:
    std::vector<entry_type> entries_;

    public:
    using iterator = decltype(entries_)::iterator;
    using const_iterator = decltype(entries_)::const_iterator;

    const std::vector<entry_type> & entries() const { return entries_; }

    iterator begin() { return entries_.begin(); }
    iterator end()   { return entries_.end(); }
    const_iterator begin() const { return entries_.cbegin(); }
    const_iterator end()   const { return entries_.cend(); }
    const_iterator cbegin() const { return entries_.cbegin(); }
    const_iterator cend()   const { return entries_.cend(); }

    /** Returns the number of entries in this `Schema`. */
    std::size_t num_entries() const { return entries_.size(); }

    /** Returns an iterator to the entry with the given `Identifier` `id`, or `end()` if no such entry exists.  */
    iterator find(Identifier id) {
        auto pred = [&id](const entry_type &e) -> bool { return e.id == id; }; // match qualified
        auto it = std::find_if(begin(), end(), pred);
        if (it != end() and std::find_if(std::next(it), end(), pred) != end())
            throw invalid_argument("duplicate identifier, lookup ambiguous");
        return it;
    }
    /** Returns an iterator to the entry with the given `Identifier` `id`, or `end()` if no such entry exists.  */
    const_iterator find(Identifier id) const { return const_cast<Schema*>(this)->find(id); }

    /** Returns `true` iff this `Schema` contains an entry with `Identifier` `id`. */
    bool has(Identifier id) const { return find(id) != end(); }

    /** Returns the entry at index `idx` with in-bounds checking. */
    const entry_type & at(std::size_t idx) const {
        if (idx >= entries_.size())
            throw out_of_range("index out of bounds");
        return entries_[idx];
    }
    /** Returns the entry at index `idx`. */
    const entry_type & operator[](std::size_t idx) const {
        M_insist(idx < entries_.size(), "index out of bounds");
        return entries_[idx];
    }

    /** Returns a `std::pair` of the index and a reference to the entry with `Identifier` `id` with in-bounds checking.
     */
    std::pair<std::size_t, const entry_type&> at(Identifier id) const {
        auto pos = find(id);
        if (pos == end())
            throw out_of_range("identifier not found");
        return { std::distance(begin(), pos), *pos };
    }
    /** Returns a `std::pair` of the index and a reference to the entry with `Identifier` `id`. */
    std::pair<std::size_t, const entry_type&> operator[](Identifier id) const {
        auto pos = find(id);
        M_insist(pos != end(), "identifier not found");
        return { std::distance(begin(), pos), *pos };
    }

    /** Adds a new entry `id` of type `type` to this `Schema`. */
    void add(Identifier id, const Type *type) { entries_.emplace_back(id, type); }
    /** Adds a new entry `id` of type `type` with constraints `constraints` to this `Schema`. */
    void add(Identifier id, const Type *type, entry_type::constraints_t constraints) {
        entries_.emplace_back(id, type, constraints);
    }

    /** Returns a deduplicated version of `this` `Schema`, i.e. duplicate entries are only contained once.  */
    Schema deduplicate() const {
        Schema res;
        for (auto &e : *this) {
            if (not res.has(e.id))
                res.add(e.id, e.type);
        }
        return res;
    }

    /** Returns a copy of `this` `Schema` where all entries with `NoneType` are removed..  */
    Schema drop_none() const {
        Schema res;
        for (auto &e : *this) {
            if (not e.type->is_none())
                res.add(e.id, e.type);
        }
        return res;
    }

    /** Adds all entries of `other` to `this` `Schema`. */
    Schema & operator+=(const Schema &other) {
        for (auto &e : other)
            entries_.emplace_back(e);
        return *this;
    }

    /** Adds all entries of `other` to `this` `Schema` using *set semantics*.  If an entry of `other` with a particular
     * `Identifier` already exists in `this`, it is not added again. */
    Schema & operator|=(const Schema &other) {
        for (auto &e : other) {
            if (not has(e.id))
                entries_.emplace_back(e);
        }
        return *this;
    }

    bool operator==(const Schema &other) const {
        return std::all_of(this->begin(), this->end(), [&](const entry_type &p) { return other.has(p.id); }) and
               std::all_of(other.begin(), other.end(), [&](const entry_type &p) { return this->has(p.id); });
    }
    bool operator!=(const Schema &other) const { return not operator==(other); }

M_LCOV_EXCL_START
    friend std::ostream & operator<<(std::ostream &out, const Schema &schema) {
        out << "{[";
        for (auto it = schema.begin(), end = schema.end(); it != end; ++it) {
            if (it != schema.begin()) out << ',';
            out << ' ' << it->id << " :" << *it->type;
        }
        return out << " ]}";
    }
M_LCOV_EXCL_STOP

    void dump(std::ostream &out) const;
    void dump() const;
};

inline Schema operator+(const Schema &left, const Schema &right)
{
    Schema S(left);
    S += right;
    return S;
}

/** Computes the *set intersection* of two `Schema`s. */
inline Schema operator&(const Schema &left, const Schema &right)
{
    Schema res;
    for (auto &e : left) {
        auto it = right.find(e.id);
        if (it != right.end()) {
            if (e.type != it->type)
                throw invalid_argument("type mismatch");
            res.add(e.id, e.type);
        }
    }
    return res;
}

inline Schema operator|(const Schema &left, const Schema &right)
{
    Schema res(left);
    res |= right;
    return res;
}


/*======================================================================================================================
 * Attribute, Table, Function, Database
 *====================================================================================================================*/

struct Table;

/** An attribute of a table.  Every attribute belongs to exactly one table.  */
struct M_EXPORT Attribute
{
    friend struct Table;

    std::size_t id; ///< the internal identifier of the attribute, unique within its table
    const Table &table; ///< the table the attribute belongs to
    const PrimitiveType *type; ///< the type of the attribute
    const char *name; ///< the name of the attribute
    bool nullable = true; ///< the flag indicating whether the attribute may be NULL

    private:
    explicit Attribute(std::size_t id, const Table &table, const PrimitiveType *type, const char *name)
        : id(id)
        , table(table)
        , type(M_notnull(type))
        , name(M_notnull(name))
    {
        if (not type->is_vectorial())
            throw invalid_argument("attributes must be of vectorial type");
    }

    public:
    Attribute(const Attribute&) = delete;
    Attribute(Attribute&&) = default;

    /** Compares to attributes.  Attributes are equal if they have the same `id` and belong to the same `table`. */
    bool operator==(const Attribute &other) const { return &this->table == &other.table and this->id == other.id; }
    bool operator!=(const Attribute &other) const { return not operator==(other); }

M_LCOV_EXCL_START
    friend std::ostream & operator<<(std::ostream &out, const Attribute &attr) {
        return out << '`' << attr.name << "` " << *attr.type;
    }
M_LCOV_EXCL_STOP

    void dump(std::ostream &out) const;
    void dump() const;
};

/** Checks that the type of the `attr` matches the template type `T`.  Throws `std::logic_error` on error. */
template<typename T>
bool type_check(const Attribute &attr)
{
    auto ty = attr.type;

    /* Boolean */
    if constexpr (std::is_same_v<T, bool>) {
        if (is<const Boolean>(ty))
            return true;
    }

    /* CharacterSequence */
    if constexpr (std::is_same_v<T, std::string>) {
        if (auto s = cast<const CharacterSequence>(ty)) {
            if (not s->is_varying)
                return true;
        }
    }
    if constexpr (std::is_same_v<T, const char*>) {
        if (auto s = cast<const CharacterSequence>(ty)) {
            if (not s->is_varying)
                return true;
        }
    }

    /* Numeric */
    if constexpr (std::is_arithmetic_v<T>) {
        if (auto n = cast<const Numeric>(ty)) {
            switch (n->kind) {
                case Numeric::N_Int:
                    if (std::is_integral_v<T> and sizeof(T) * 8 == ty->size())
                        return true;
                    break;

                case Numeric::N_Float:
                    if (std::is_floating_point_v<T> and sizeof(T) * 8 == ty->size())
                        return true;
                    break;

                case Numeric::N_Decimal:
                    if (std::is_integral_v<T> and ceil_to_pow_2(ty->size()) == 8 * sizeof(T))
                        return true;
                    break;
            }
        }
    }

    return false;
}

/** A table is a sorted set of attributes. */
struct M_EXPORT Table
{
    const char *name; ///< the name of the table
    private:
    using table_type = std::vector<Attribute>;
    table_type attrs_; ///< the attributes of this table, maintained as a sorted set
    std::unordered_map<const char*, table_type::size_type> name_to_attr_; ///< maps attribute names to attributes
    std::unique_ptr<Store> store_; ///< the store backing this table; may be `nullptr`
    storage::DataLayout layout_; ///< the physical data layout for this table
    SmallBitset primary_key_; ///< the primary key of this table, maintained as a `SmallBitset` over attribute id's

    public:
    Table(const char *name) : name(name) { }

    /** Returns the number of attributes in this table. */
    std::size_t num_attrs() const { return attrs_.size(); }

    table_type::const_iterator begin()  const { return attrs_.cbegin(); }
    table_type::const_iterator end()    const { return attrs_.cend(); }
    table_type::const_iterator cbegin() const { return attrs_.cbegin(); }
    table_type::const_iterator cend()   const { return attrs_.cend(); }

    /** Returns the attribute with the given `id`.  Throws `std::out_of_range` if no attribute with the given `id`
     * exists. */
    Attribute & at(std::size_t id) {
        if (id >= attrs_.size())
            throw std::out_of_range("id out of bounds");
        auto &attr = attrs_[id];
        M_insist(attr.id == id, "attribute ID mismatch");
        return attr;
    }
    const Attribute & at(std::size_t id) const { return const_cast<Table*>(this)->at(id); }
    /** Returns the attribute with the given `id`. */
    Attribute & operator[](std::size_t id) {
        auto &attr = attrs_[id];
        M_insist(attr.id == id, "attribute ID mismatch");
        return attr;
    }
    const Attribute & operator[](std::size_t id) const { return const_cast<Table*>(this)->operator[](id); }

    /** Returns the attribute with the given `name`.  Throws `std::out_of_range` if no attribute with the given `name`
     * exists. */
    Attribute & at(const char *name) {
        if (auto it = name_to_attr_.find(name); it != name_to_attr_.end())
            return at(it->second);
        throw std::out_of_range("name does not exists");
    }
    const Attribute & at(const char *name) const { return const_cast<Table*>(this)->at(name_to_attr_.at(name)); }
    /** Returns the attribute with the given `name`. */
    Attribute & operator[](const char *name) { return operator[](name_to_attr_.find(name)->second); }
    const Attribute & operator[](const char *name) const { return const_cast<Table*>(this)->operator[](name); }

    /** Returns a reference to the backing store. */
    Store & store() const { return *store_; }
    /** Sets the backing store for this table.  `new_store` must not be `nullptr`. */
    void store(std::unique_ptr<Store> new_store) { using std::swap; swap(store_, new_store); }

    /** Returns a reference to the physical data layout. */
    const storage::DataLayout & layout() const { M_insist(bool(layout_)); return layout_; }
    /** Sets the physical data layout for this table. */
    void layout(storage::DataLayout &&new_layout) { layout_ = std::move(new_layout); }
    /** Sets the physical data layout for this table by calling `factory.make()`. */
    void layout(const storage::DataLayoutFactory &factory);

    /** Returns all attributes forming the primary key. */
    std::vector<const Attribute*> primary_key() const {
        std::vector<const Attribute*> res;
        for (auto id : primary_key_)
            res.push_back(&operator[](id));
        return res;
    }
    /** Adds an attribute with the given `name` to the primary key of this table. Throws `std::out_of_range` if no
     * attribute with the given `name` exists. */
    void add_primary_key(const char *name) {
        auto &attr = at(name);
        primary_key_(attr.id) = true;
    }

    /** Adds a new attribute with the given `name` and `type` to the table.  Throws `std::invalid_argument` if the
     * `name` is already in use. */
    void push_back(const char *name, const PrimitiveType *type) {
        auto res = name_to_attr_.emplace(name, attrs_.size());
        if (not res.second)
            throw std::invalid_argument("attribute name already in use");
        attrs_.emplace_back(Attribute(attrs_.size(), *this, type, name));
    }

    /** Returns a `Schema` for this `Table`. */
    Schema schema() const;

    void dump(std::ostream &out) const;
    void dump() const;
};

/** Defines a function.  There are functions pre-defined in the SQL standard and user-defined functions. */
struct M_EXPORT Function
{
#define kind_t(X) \
    X(FN_Scalar) \
    X(FN_Aggregate)

    enum fnid_t {
#define M_FUNCTION(NAME, KIND) FN_ ## NAME,
#include <mutable/tables/Functions.tbl>
#undef M_FUNCTION
        FN_UDF, // for all user-defined functions
    };

    const char *name; ///< the name of the function
    fnid_t fnid; ///< the function id
    M_DECLARE_ENUM(kind_t) kind; ///< the function kind: Scalar, Aggregate, etc.

    Function(const char *name, fnid_t fnid, kind_t kind) : name(name), fnid(fnid), kind(kind) { }

    /** Returns `true` iff this is a user-defined function. */
    bool is_UDF() const { return fnid == FN_UDF; }

    /** Returns `true` iff this function is scalar, i.e.\ if it is evaluated *per tuple*. */
    bool is_scalar() const { return kind == FN_Scalar; }
    /** Returns `true` iff this function is an aggregation, i.e.\ if it is evaluated *on all tuples*. */
    bool is_aggregate() const { return kind == FN_Aggregate; }

    void dump(std::ostream &out) const;
    void dump() const;

    private:
    static constexpr const char *FNID_TO_STR_[] = {
#define M_FUNCTION(NAME, KIND) "FN_" #NAME,
#include <mutable/tables/Functions.tbl>
#undef M_FUNCTION
            "FN_UDF",
    };
    static constexpr const char *KIND_TO_STR_[] = { M_ENUM_TO_STR(kind_t) };
#undef kind_t
};

/** A `Database` is a set of `Table`s, `Function`s, and `Statistics`. */
struct M_EXPORT Database
{
    friend struct Catalog;

    public:
    const char *name; ///< the name of the database
    private:
    std::unordered_map<const char*, Table*> tables_; ///< the tables of this database
    std::unordered_map<const char*, Function*> functions_; ///< functions defined in this database
    std::unique_ptr<CardinalityEstimator> cardinality_estimator_; ///< the `CardinalityEstimator` of this `Database`

    private:
    Database(const char *name);

    public:
    ~Database();

    /** Returns the number of tables in this `Database`. */
    std::size_t size() const { return tables_.size(); }
    auto begin_tables() const { return tables_.cbegin(); }
    auto end_tables() const { return tables_.cend(); }

    /*===== Tables ===================================================================================================*/
    /** Returns a reference to the `Table` with the given `name`.  Throws `std::out_of_range` if no `Table` with the
     * given `name` exists in this `Database`. */
    Table & get_table(const char *name) const { return *tables_.at(name); }
    /** Adds a new `Table` to this `Database`.  Throws `std::invalid_argument` if a `Table` with the given `name`
     * already exists. */
    Table & add_table(const char *name) {
        auto it = tables_.find(name);
        if (it != tables_.end()) throw std::invalid_argument("table with that name already exists");
        it = tables_.emplace_hint(it, name, new Table(name));
        return *it->second;
    }
    /** Adds a new `Table` to this `Database`.  TODO implement transfer of ownership with unique_ptr */
    Table & add(Table *r) {
        auto it = tables_.find(r->name);
        if (it != tables_.end()) throw std::invalid_argument("table with that name already exists");
        it = tables_.emplace_hint(it, r->name, r);
        return *it->second;
    }

    /*===== Functions ================================================================================================*/
    /** Returns a reference to the `Function` with the given `name`.  First searches this `Database` instance.  If no
     * `Function` with the given `name` is found, searches the global `Catalog`.  Throws `std::invalid_argument` if no
     * `Function` with the given `name` exists. */
    const Function * get_function(const char *name) const;

    /*===== Statistics ===============================================================================================*/
    /** Sets the `CardinalityEstimator` of this `Database`.  Returns the old `CardinalityEstimator`.
     *
     * @return the old `CardinalityEstimator`, may be `nullptr`
     */
    std::unique_ptr<CardinalityEstimator> cardinality_estimator(std::unique_ptr<CardinalityEstimator> CE) {
        auto old = std::move(cardinality_estimator_); cardinality_estimator_ = std::move(CE); return old;
    }
    const CardinalityEstimator & cardinality_estimator() const { return *cardinality_estimator_; }
};

}

namespace std {

/** Specializes `std::hash<T>` for `m::Schema::Identifier`. */
template<>
struct hash<m::Schema::Identifier>
{
    uint64_t operator()(m::Schema::Identifier id) const {
        m::StrHash h;
        uint64_t hash = h(id.name);
        if (id.prefix)
            hash *= h(id.prefix);
        return hash;
    }
};

/** Specializes `std::hash<T>` for `m::Attribute`. */
template<>
struct hash<m::Attribute>
{
    uint64_t operator()(const m::Attribute &attr) const {
        m::StrHash h;
        return h(attr.table.name) * (attr.id + 1);
    }
};

}
