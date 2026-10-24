#include <map>
#include <set>
#include <algorithm>
#include "prime_graph_split.hpp"
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
#include "PartitionPropagator.hpp"
#include <metis.h>
using namespace std;

namespace prime_graph {

    PrimalGraph clauses_to_primal_graph(const vector<vector<int>>& clauses, const int num_variables) {
        map<pair<int,int>, int> edge_map;

        for (const vector<int>& clause : clauses) {
            // distinct variables of this clause (handles x and ¬x, and duplicate literals)
            set<int> vars;
            for (int lit : clause) {
                int var = abs(lit);
                assert(var >= 1 && var <= num_variables);
                vars.insert(var - 1);
            }
            // clique over all variables of the clause
            for (auto it = vars.begin(); it != vars.end(); ++it) {
                for (auto jt = next(it); jt != vars.end(); ++jt) {
                    edge_map[{*it, *jt}]++;   // *it < *jt because std::set is sorted
                }
            }
        }

        PrimalGraph g;
        g.num_vertices = num_variables;
        g.edges.reserve(edge_map.size());
        g.edge_weights.reserve(edge_map.size());
        for (const auto& [edge, weight] : edge_map) {
            g.edges.push_back(edge);
            g.edge_weights.push_back(weight);
        }
        return g;
    }

    vector<int> split_primal_graph(const PrimalGraph& graph, int blocks) {
        const idx_t n = static_cast<idx_t>(graph.num_vertices);

        // === Build symmetric CSR adjacency (METIS needs both directions of every edge) ===
        vector<idx_t> xadj(n + 1, 0);
        for (const auto& [u, v] : graph.edges) {
            xadj[u + 1]++;
            xadj[v + 1]++;
        }
        for (idx_t i = 0; i < n; ++i) {
            xadj[i + 1] += xadj[i];
        }

        vector<idx_t> adjncy(xadj[n]);
        vector<idx_t> adjwgt(xadj[n]);
        vector<idx_t> fill(xadj.begin(), xadj.end() - 1);   // next free slot per vertex

        for (size_t i = 0; i < graph.edges.size(); ++i) {
            const auto [u, v] = graph.edges[i];
            const idx_t w = static_cast<idx_t>(graph.edge_weights[i]);
            adjncy[fill[u]] = v;  adjwgt[fill[u]++] = w;
            adjncy[fill[v]] = u;  adjwgt[fill[v]++] = w;
        }

        // === Options ===
        idx_t options[METIS_NOPTIONS];
        METIS_SetDefaultOptions(options);
        options[METIS_OPTION_OBJTYPE] = METIS_OBJTYPE_CUT;
        options[METIS_OPTION_SEED]    = 42;
        options[METIS_OPTION_UFACTOR] = 30;   // allowed imbalance in 1/1000 -> 3%

        // === Partition ===
        idx_t ncon   = 1;                     // one balance constraint (vertex count)
        idx_t nparts = blocks;
        idx_t objval = 0;
        vector<idx_t> part(n);

        int ret = METIS_PartGraphKway(
            const_cast<idx_t*>(&n), &ncon,
            xadj.data(), adjncy.data(),
            nullptr,            // vertex weights (uniform)
            nullptr,            // vertex sizes
            adjwgt.data(),      // edge weights = number of shared clauses
            &nparts,
            nullptr, nullptr,   // default target weights / imbalance tolerance
            options, &objval, part.data()
        );
        if (ret != METIS_OK) {
            std::cerr << "METIS failed with code " << ret << "\n";
            std::exit(1);
        }

        return vector<int>(part.begin(), part.end());
    }

    vector<Formula> make_sub_formulas(const vector<vector<int>>& clauses, int num_variables, const vector<int>& var_partition, int blocks) {
        vector<Formula> formulas(blocks + 1);
        for (const auto& clause : clauses) {
            if (clause.empty()) continue;
            int first_block = var_partition[abs(clause[0]) - 1];
            bool is_cut = false;
            for (int lit : clause) {
                if (var_partition[abs(lit) - 1] != first_block) {
                    is_cut = true;
                    break;
                }
            }
            if (is_cut) {
                formulas[blocks].clauses.push_back(clause);
            } else {
                formulas[first_block].clauses.push_back(clause);
            }
        }

        for (auto& formula : formulas) {
            formula.num_variables = num_variables;
        }
        return formulas;
    }
}
