#pragma once

#include "IR/CostModel.hpp"
#include "IR/JoinOrderer.hpp"


namespace db {

/** The optimizer interface.
 *
 * The optimizer applies a join ordering algorithm to a join graph to compute a join order that minimizes the costs
 * under a given cost model.
 * Additionally, the optimizer may apply several semantics preserving transformations to improve performance.  Such
 * transformations include query unnesting and predicate inference.
 */
struct Optimizer
{
    private:
    const JoinOrderer &orderer_;
    const CostModel &cm_;

    public:
    Optimizer(const JoinOrderer &orderer, const CostModel &cm) : orderer_(orderer), cm_(cm) { }

    auto & join_orderer() const { return orderer_; };
    auto & cost_model() const { return cm_; }

    /** Apply this optimizer to the given join graph to compute an operator tree. */
    auto operator()(const JoinGraph &G) const { return join_orderer()(G, cost_model()); }
};

}
