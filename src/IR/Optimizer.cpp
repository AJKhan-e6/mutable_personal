#include "mutable/IR/Optimizer.hpp"

#include "mutable/IR/Operator.hpp"
#include "mutable/storage/Store.hpp"


using namespace m;


/** Returns `true` iff the given join predicate in `cnf::CNF` formula is an equi-join. */
bool is_equi_join(const cnf::CNF &cnf)
{
    if (cnf.size() != 1) return false;
    auto &clause = cnf[0];
    if (clause.size() != 1) return false;
    auto &literal = clause[0];
    if (literal.negative()) return false;
    auto expr = literal.expr();
    auto binary = cast<const BinaryExpr>(expr);
    if (not binary or binary->tok != TK_EQUAL) return false;
    return is<const Designator>(binary->lhs) and is<const Designator>(binary->rhs);
}

void PlanTable::dump(std::ostream &out) const { out << *this << std::endl; }
void PlanTable::dump() const { dump(std::cerr); }

std::unique_ptr<Producer> Optimizer::operator()(const QueryGraph &G) const
{
    return std::move(optimize(G).first);
}

std::pair<std::unique_ptr<Producer>, PlanTable> Optimizer::optimize(const QueryGraph &G) const
{
    PlanTable plan_table(G);
    const auto num_sources = G.sources().size();
    auto &C = Catalog::Get();
    auto &DB = C.get_database_in_use();
    auto &CE = DB.cardinality_estimator();

    if (num_sources == 0)
        return { std::make_unique<ProjectionOperator>(G.projections()), std::move(plan_table) };

    /*----- Initialize plan table and compute plans for data sources. ------------------------------------------------*/
    Producer **source_plans = new Producer*[num_sources];
    for (auto ds : G.sources()) {
        Subproblem s(1UL << ds->id());
        if (auto bt = cast<const BaseTable>(ds)) {
            /* Produce a scan for base tables. */
            plan_table[s].cost = 0;
            plan_table[s].model = CE.estimate_scan(G, s);
            auto &store = bt->table().store();
            source_plans[ds->id()] = new ScanOperator(store, bt->alias());
        }
        else {
            /* Recursively solve nested queries. */
            auto Q = as<const Query>(ds);
            auto [sub_plan, sub_table] = optimize(*Q->query_graph());
            auto &sub = sub_table.get_final();

            /* Prefix every attribute of the nested query with the nested query's alias. */
            Schema S;
            for (auto &e : sub_plan->schema())
                S.add({Q->alias(), e.id.name}, e.type);
            sub_plan->schema() = S;

            /* Update the plan table with the `DataModel` and cost of the nested query and save the plan in the array of
             * source plans. */
            plan_table[s].cost = sub.cost;
            plan_table[s].model = std::move(sub.model);
            source_plans[ds->id()] = sub_plan.release();
        }
        /* Apply filter, if any. */
        if (ds->filter().size()) {
            auto filter = new FilterOperator(ds->filter());
            filter->add_child(source_plans[ds->id()]);
            source_plans[ds->id()] = filter;
            auto new_model = CE.estimate_filter(*plan_table[s].model, filter->filter());
            plan_table[s].model = std::move(new_model);
        }
    }

    optimize_locally(G, plan_table);
    auto plan = construct_plan(G, plan_table, source_plans).release();
    auto &entry = plan_table.get_final();

    /* Perform grouping */
    if (not G.group_by().empty() or not G.aggregates().empty()) {
        /* Compute `DataModel` after grouping. */
        auto new_model = CE.estimate_grouping(*entry.model, G.group_by()); // TODO provide aggregates
        entry.model = std::move(new_model);
        // TODO pick "best" algorithm
        auto group_by = new GroupingOperator(G.group_by(), G.aggregates(), GroupingOperator::G_Hashing);
        group_by->add_child(plan);
        plan = group_by;
    }

    /* Perform ordering */
    if (not G.order_by().empty()) {
        // TODO estimate data model
        auto order_by = new SortingOperator(G.order_by());
        order_by->add_child(plan);
        plan = order_by;
    }

    /* Perform projection */
    if (not G.projections().empty() or G.projection_is_anti()) {
        // TODO estimate data model
        auto projection = new ProjectionOperator(G.projections(), G.projection_is_anti());
        projection->add_child(plan);
        plan = projection;
    }

    /* Limit. */
    if (G.limit().limit or G.limit().offset) {
        /* Compute `DataModel` after limit. */
        auto new_model = CE.estimate_limit(*entry.model, G.limit().limit, G.limit().offset);
        entry.model = std::move(new_model);
        // TODO estimate data model
        auto limit = new LimitOperator(G.limit().limit, G.limit().offset);
        limit->add_child(plan);
        plan = limit;
    }

    plan->minimize_schema();
    delete[] source_plans;
    return std::make_pair(std::unique_ptr<Producer>(plan), std::move(plan_table));
}

void Optimizer::optimize_locally(const QueryGraph &G, PlanTable &plan_table) const
{
    plan_enumerator()(G, cost_function(), plan_table);
}

std::unique_ptr<Producer>
Optimizer::construct_plan(const QueryGraph &G, PlanTable &plan_table, Producer **source_plans) const
{
    auto joins = G.joins(); // create a "set" of all joins

    /* Use nested lambdas to implement recursive lambda using CPS. */
    const auto construct_recursive = [&](Subproblem s) -> Producer* {
        auto construct_plan_impl = [&](Subproblem s, auto &construct_plan_rec) -> Producer* {
            auto subproblems = plan_table[s].get_subproblems();
            if (subproblems.empty()) {
                insist(s.size() == 1);
                return source_plans[*s.begin()];
            } else {
                /* Compute plan for each sub problem.  Must happen *before* calculating the join predicate. */
                std::vector<Producer*> sub_plans;
                for (auto sub : subproblems)
                    sub_plans.push_back(construct_plan_rec(sub, construct_plan_rec));

                /* Calculate the join predicate. */
                cnf::CNF join_condition;
                for (auto it = joins.begin(); it != joins.end(); ) {
                    Subproblem join_sources;
                    /* Compute subproblem of sources to join. */
                    for (auto ds : (*it)->sources())
                        join_sources.set(ds->id());

                    if (join_sources.is_subset(s)) { // possible join
                        join_condition = join_condition and (*it)->condition();
                        it = joins.erase(it);
                    } else {
                        ++it;
                    }
                }

                /* Construct the join. */
                if (sub_plans.size() == 2 and is_equi_join(join_condition)) {
                    auto join = new JoinOperator(join_condition, JoinOperator::J_SimpleHashJoin);
                    for (auto sub_plan : sub_plans)
                        join->add_child(sub_plan);
                    return join;
                } else {
                    auto join = new JoinOperator(join_condition, JoinOperator::J_NestedLoops);
                    for (auto sub_plan : sub_plans)
                        join->add_child(sub_plan);
                    return join;
                }
            }
        };
        return construct_plan_impl(s, construct_plan_impl);
    };

    return std::unique_ptr<Producer>(construct_recursive(Subproblem((1UL << G.sources().size()) - 1)));
}
