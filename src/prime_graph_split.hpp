#pragma once
#include <vector>
#include "PartitionPropagator.hpp"

namespace prime_graph {
    struct PrimalGraph {
        int num_vertices = 0;                       // = number of variables
        std::vector<std::pair<int,int>> edges;      // 0-based variable ids, u < v
        std::vector<int> edge_weights;              // #clauses containing both u and v
    };

    PrimalGraph clauses_to_primal_graph(const std::vector<std::vector<int>>& clauses, int num_variables);

    // Returns for every variable (index = var-1) its block id
    std::vector<int> split_primal_graph(const PrimalGraph& graph, int blocks);

    // Returns ids of all clauses whose variables lie in more than one block
    std::vector<Formula> make_sub_formulas(const std::vector<std::vector<int>>& clauses, int num_variables, const std::vector<int>& var_partition, int blocks);
}
