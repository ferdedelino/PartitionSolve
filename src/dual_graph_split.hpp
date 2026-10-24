#pragma once
#include <vector>
#include <utility>
#include "PartitionPropagator.hpp"

namespace dual_graph {

struct DualGraph {
    int num_vertices = 0;                    // one vertex per clause
    std::vector<std::pair<int,int>> edges;   // (i < j), clauses i and j share a variable
};

struct Split {
    std::vector<int> part;       // part[c] = block of clause c (METIS result)
    std::vector<int> separator;  // clause ids whose removal disconnects the blocks
};

DualGraph clauses_to_dual_graph(const std::vector<std::vector<int>>& clauses, int num_variables);

Split compute_split(const DualGraph& graph, int blocks);

std::vector<Formula> make_sub_formulas(const std::vector<std::vector<int>>& clauses,
                                       int num_variables, const DualGraph& graph, int blocks);
}