#pragma once

#include "IR/CNF.hpp"
#include <memory>
#include <vector>


namespace db {

struct DataSource;
struct Join;
struct QueryGraph;
struct Stmt;

/** A data source in the query graph.  A data source provides a sequence of tuples, optionally with a filter condition
 * they must fulfill.  Data sources can be joined with one another. */
struct DataSource
{
    private:
    cnf::CNF filter_; ///< filter condition on this data source
    std::vector<Join*> joins_; ///< joins with this data source
    const char *alias_; ///< the alias of this data source

    public:
    DataSource(const char *alias) : alias_(alias) { }
    virtual ~DataSource() { }

    const char * alias() const { return alias_; }
    cnf::CNF filter() const { return filter_; }
    void update_filter(cnf::CNF filter) { filter_ = filter_ and filter; }
    void add_join(Join *join) { joins_.emplace_back(join); }
    const auto & joins() const { return joins_; }
};

/** A base table is a data source that is materialized and stored persistently. */
struct BaseTable : DataSource
{
    private:
    const Table &table_; ///< the table providing the tuples

    public:
    BaseTable(const Table &table, const char *alias) : DataSource(alias), table_(table) { }

    const Table & table() const { return table_; }
};

/** A (nested) query is a data source that must be computed. */
struct Query : DataSource
{
    private:
    QueryGraph *query_graph_; ///< query graph of the sub-query

    public:
    Query(const char *alias, QueryGraph *query_graph) : DataSource(alias), query_graph_(query_graph) { }
    ~Query();

    QueryGraph * query_graph() const { return query_graph_; }
};

/** A join combines source tables by a join condition. */
struct Join
{
    using sources_t = std::vector<DataSource*>;

    private:
    cnf::CNF condition_; ///< join condition
    sources_t sources_; ///< the sources to join

    public:
    Join(cnf::CNF condition, sources_t sources) : condition_(condition) , sources_(sources) { }

    cnf::CNF condition() const { return condition_; }
    const sources_t & sources() const { return sources_; }
};

/** The query graph represents all data sources and joins in a graph structure.  It is used as an intermediate
 * representation of a query. */
struct QueryGraph
{
    friend struct GraphBuilder;

    private:
    using projection_type = std::pair<const Expr*, const char*>;
    using order_type = std::pair<const Expr*, bool>; ///> true means ascending, false means descending

    std::vector<DataSource*> sources_; ///< collection of all data sources in this query graph
    std::vector<Join*> joins_; ///< collection of all joins in this query graph

    std::vector<const Expr*> group_by_; ///< the grouping keys
    std::vector<const Expr*> aggregates_; ///< the aggregates to compute
    std::vector<projection_type> projections_; ///< the data to compute
    std::vector<order_type> order_by_; ///< the order
    struct { uint64_t limit = 0, offset; } limit_; ///< limit: limit and offset

    public:
    ~QueryGraph();

    static std::unique_ptr<QueryGraph> Build(const Stmt &stmt);

    const auto & sources() const { return sources_; }
    const auto & joins() const { return joins_; }
    const auto & group_by() const { return group_by_; }
    const auto & aggregates() const { return aggregates_; }
    const auto & projections() const { return projections_; }
    const auto & order_by() const { return order_by_; }
    auto limit() const { return limit_; }

    /** Translates the query graph to dot. */
    void dot(std::ostream &out) const;

    void dump(std::ostream &out) const;
    void dump() const;
};

}
