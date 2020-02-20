#pragma once

#include "catalog/CostFunction.hpp"
#include "IR/QueryGraph.hpp"
#include "util/macro.hpp"
#include <cstdint>
#include <unordered_map>
#include <vector>


namespace db {

struct PlanTable;

/** An interface for all plan enumerators. */
struct PlanEnumerator
{
    using Subproblem = QueryGraph::Subproblem;

    virtual ~PlanEnumerator() { }

    /** Enumerate subplans and fill plan table. */
    virtual void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const = 0;
};

/** Computes an arbitrary join order (deterministically). */
struct DummyPlanEnumerator final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

/** Computes the join order using size-based dynamic programming. */
struct DPsize final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

/** Computes the join order using size-based dynamic programming.  In addition to `DPsize`, applies the following
 * optimizations.  First, do not enumerate symmetric subproblems.  Second, in case both subproblems are of equal size,
 * consider only subproblem succeeding the first subproblem. */
struct DPsizeOpt final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

/** Computes the join order using subset-based dynamic programming. */
struct DPsub final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

/** Computes the join order using subset-based dynamic programming.  In comparison to `DPsub`, do not enumerate
 * symmetric subproblems. */
struct DPsubOpt final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

/** Computes the join order using connected subgraph complement pairs. */
struct DPccp final : PlanEnumerator
{
    void operator()(const QueryGraph &G, const CostFunction &cf, PlanTable &PT) const override;
};

}
