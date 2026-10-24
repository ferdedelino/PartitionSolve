#include <iostream>
#include <vector>
#include <cassert>
#include <memory>
#include <vector>
#include <iostream>
#include <thread>
#include <cmath>
#include <mtkahypar.h>
#include <assert.h>
#include "hypergraph_split.hpp"
#include "PartitionPropagator.hpp"
using namespace std;


namespace hypergraph_split {

    vector<vector<int>> clauses_to_hypergraph(const vector<vector<int>>& clauses, const int num_variables) {
        vector<vector<int>> edges(num_variables);
        for (int clauseId = 0; clauseId < static_cast<int>(clauses.size()); ++clauseId) {
            vector<int> clause = clauses[clauseId];
            for (int lit : clause) {
                int var = abs(lit);
                assert(var >= 1 && var <= num_variables);
                edges.at(var - 1).push_back(clauseId);
            }
        }
        vector<vector<int>> edges_cleaned;
        for (const vector<int>& edge : edges) {
            if (edge.size() > 1) {
                edges_cleaned.push_back(edge);
            }
        }

        return edges_cleaned;
    }

    vector<Formula> split_hypergraph(const Formula& formula, const vector<vector<int>>& hypergraph_edges, mt_kahypar_partition_id_t blocks) {
        mt_kahypar_error_t error{};

        // === Initialize KaHyPar context ===
        mt_kahypar_initialize(
            std::thread::hardware_concurrency(), // use all available cores
            true // activate interleaved NUMA allocation policy 
        );
        mt_kahypar_context_t* context = mt_kahypar_context_from_preset(DETERMINISTIC); // TODO: Mybe DEFAULT instead of DETERMINISTIC. Latter is better for testing
        //mt_kahypar_context_t* context = mt_kahypar_context_from_preset(DEFAULT); // TODO: Mybe DEFAULT instead of DETERMINISTIC. Latter is better for testing
        mt_kahypar_set_partitioning_parameters(context,
            blocks, // number of blocks
            0.03, // imbalance parameter
            KM1 // objective function
        );
        mt_kahypar_set_seed(42);
        mt_kahypar_status_t status = mt_kahypar_set_context_parameter(context, VERBOSE, "0", &error);
        if (status != SUCCESS) {
            std::cerr << "Failed to set parameter: " << error.msg << "\n";
            std::exit(1);
        }

        // === Create the hypergraph ===
        std::vector<size_t> indices;
        std::vector<mt_kahypar_hypernode_id_t> pins;

        indices.reserve(hypergraph_edges.size() + 1);
        for (const auto& edge : hypergraph_edges) {
            indices.push_back(pins.size());
            for (int clause_id : edge) {
                pins.push_back(static_cast<mt_kahypar_hypernode_id_t>(clause_id));
            }
        }
        indices.push_back(pins.size());

        mt_kahypar_hypergraph_t hypergraph = mt_kahypar_create_hypergraph(
            context,
            static_cast<mt_kahypar_hypernode_id_t>(formula.clauses.size()),
            static_cast<mt_kahypar_hyperedge_id_t>(hypergraph_edges.size()),
            indices.data(),
            pins.data(),
            nullptr,
            nullptr,
            &error
        );
        if (hypergraph.hypergraph == nullptr) {
            std::cerr << "Failed to create hypergraph: " << error.msg << "\n";
            // free the error content if the header provides a function for it
            std::exit(1);
        }
        

        // === Partition Hypergraph ===
        mt_kahypar_partitioned_hypergraph_t partitioned_hg = mt_kahypar_partition(hypergraph, context, &error);
        if (partitioned_hg.partitioned_hg == nullptr) {
            std::cout << error.msg << std::endl;
            std::exit(1);
        }

        // === Extract Partition ===
        auto partition = std::make_unique<mt_kahypar_partition_id_t[]>(mt_kahypar_num_hypernodes(hypergraph));
        mt_kahypar_get_partition(partitioned_hg, partition.get());
        vector<Formula> formulas(blocks);
        for (size_t clause_id = 0; clause_id < formula.clauses.size(); ++clause_id) {
            int block_id = partition[clause_id];
            formulas[block_id].clauses.push_back(formula.clauses[clause_id]);
        }
        // All sub-formulas should have the original number of variables
        for (Formula& f : formulas) {
            f.num_variables = formula.num_variables;
        }


        // === Compute Metrics ===
        /*const double imbalance = mt_kahypar_imbalance(partitioned_hg, context);
        const int km1 = mt_kahypar_km1(partitioned_hg);
        std::cout << "Partitioning Results:" << std::endl;
        std::cout << "Imbalance         = " << imbalance << std::endl;
        std::cout << "Km1               = " << km1 << std::endl;
        for (Formula& f : formulas) {
            std::cout << "Sub-Formula: " << f.clauses.size() << " clauses\n";
        } 
        */

        mt_kahypar_free_context(context);
        mt_kahypar_free_hypergraph(hypergraph);
        mt_kahypar_free_partitioned_hypergraph(partitioned_hg);

        return formulas;
    }

    vector<int> extract_common_variables(const vector<Formula>& formulas, int number_variables) {
        vector<int> count(number_variables, 0);
        for (const Formula& formula : formulas) {
            vector<int> seen(number_variables, 0);
            for (auto& clause : formula.clauses) {
                for (int lit : clause) {
                    int var_id = abs(lit) - 1;
                    if (seen[var_id] == 0) {
                        seen[var_id] = 1;
                        count[var_id]++;
                    }
                }
            }
        }
        vector<int> common;
        for (int var_id = 0; var_id < number_variables; ++var_id) {
            if (count[var_id] > 1) {
                common.push_back(var_id + 1);
            }
        }
        return common;
    }

}