#pragma once
#include <vector>
#include "PartitionPropagator.hpp"
#include <mtkahypar.h>


namespace hypergraph_split {
    std::vector<std::vector<int>> clauses_to_hypergraph(
        const std::vector<std::vector<int>>& clauses, int num_variables);

    std::vector<Formula> split_hypergraph(
        const Formula& formula, const std::vector<std::vector<int>>& edges, mt_kahypar_partition_id_t blocks);

    std::vector<int> extract_common_variables(
        const std::vector<Formula>& formulas, int number_variables);
}