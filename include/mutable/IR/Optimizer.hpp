#pragma once

#include <mutable/catalog/CostFunction.hpp>
#include <mutable/IR/PlanEnumerator.hpp>
#include <mutable/IR/PlanTable.hpp>


namespace m {

/** The optimizer interface.
 *
 * The optimizer applies a join ordering algorithm to a query graph to compute a join order that minimizes the costs
 * under a given cost function.
 * Additionally, the optimizer may apply several semantics preserving transformations to improve performance.  Such
 * transformations include query unnesting and predicate inference.
 */
struct Optimizer
{
    using Subproblem = QueryGraph::Subproblem;
    using projection_type = QueryGraph::projection_type;
    using order_type = QueryGraph::order_type;

    private:
    const PlanEnumerator &pe_;
    const CostFunction &cf_;

    public:
    Optimizer(const PlanEnumerator &pe, const CostFunction &cf) : pe_(pe), cf_(cf) { }

    auto & plan_enumerator() const { return pe_; }
    auto & cost_function() const { return cf_; }

    /** Apply this optimizer to the given query graph to compute an operator tree. */
    std::unique_ptr<Producer> operator()(const QueryGraph &G) const { return optimize(G).first; }

    /** Recursively computes and constructs an optimial plan for the given query graph.  Selects a `PlanTable*` type to
     * represent the internal state of planning progress, then delegates to `optimize_with_plantable<>()`.  */
    std::pair<std::unique_ptr<Producer>, PlanTableEntry>
    optimize(const QueryGraph &G) const;

    /** Recursively computes and constructs an optimial plan for the given query graph, using the given `PlanTable` type
     * to represent the state of planning progress.  */
    template<typename PlanTable>
    std::pair<std::unique_ptr<Producer>, PlanTable>
    optimize_with_plantable(const QueryGraph &G) const;

    private:
    /** Optimizes a plan table after initialization of the data source entries. */
    template<typename PlanTable>
    void optimize_locally(const QueryGraph &G, PlanTable &plan_table) const;

    /** Constructs an operator tree given a solved plan table and the plans to compute the data sources of the query. */
    template<typename PlanTable>
    std::unique_ptr<Producer> construct_plan(const QueryGraph &G, const PlanTable &plan_table,
                                             Producer * const *source_plans) const;

    /** Returns `true` iff there is an element in `projections` which is needed by `order_by`. */
    bool projection_needed(const std::vector<projection_type> &projections,
                           const std::vector<order_type> &order_by) const;
};

}
