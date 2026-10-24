#include "dual_graph_split.hpp"
#include <metis.h>
#include <algorithm>
#include <queue>
#include <stdexcept>
#include <cstdlib>

namespace dual_graph {

DualGraph clauses_to_dual_graph(const std::vector<std::vector<int>>& clauses, int num_variables) {
    DualGraph g;
    g.num_vertices = static_cast<int>(clauses.size());

    // occurrences[v] = clauses containing variable v (positive or negative literal)
    std::vector<std::vector<int>> occurrences(num_variables + 1);
    for (int c = 0; c < g.num_vertices; ++c)
        for (int lit : clauses[c])
            occurrences[std::abs(lit)].push_back(c);

    for (auto& occ : occurrences) {
        std::sort(occ.begin(), occ.end());
        occ.erase(std::unique(occ.begin(), occ.end()), occ.end());
        for (size_t i = 0; i < occ.size(); ++i)
            for (size_t j = i + 1; j < occ.size(); ++j)
                g.edges.emplace_back(occ[i], occ[j]);
    }

    // Two clauses can share several variables -> deduplicate
    std::sort(g.edges.begin(), g.edges.end());
    g.edges.erase(std::unique(g.edges.begin(), g.edges.end()), g.edges.end());
    return g;
}

Split compute_split(const DualGraph& graph, int blocks) {
    idx_t n = graph.num_vertices;
    Split result;
    result.part.assign(n, 0);
    if (blocks <= 1 || n == 0) return result;

    // Build CSR adjacency (both directions)
    std::vector<idx_t> xadj(n + 1, 0);
    for (auto [a, b] : graph.edges) { ++xadj[a + 1]; ++xadj[b + 1]; }
    for (idx_t i = 0; i < n; ++i) xadj[i + 1] += xadj[i];

    std::vector<idx_t> adjncy(xadj[n]);
    std::vector<idx_t> fill(xadj.begin(), xadj.end() - 1);
    for (auto [a, b] : graph.edges) {
        adjncy[fill[a]++] = b;
        adjncy[fill[b]++] = a;
    }

    // Partition clauses into `blocks` parts, minimizing cut edges
    idx_t ncon = 1, nparts = blocks, objval = 0;
    std::vector<idx_t> part(n);
    idx_t options[METIS_NOPTIONS];
    METIS_SetDefaultOptions(options);

    int rc = METIS_PartGraphKway(&n, &ncon, xadj.data(), adjncy.data(),
                                 nullptr, nullptr, nullptr, &nparts,
                                 nullptr, nullptr, options, &objval, part.data());
    if (rc != METIS_OK) throw std::runtime_error("METIS_PartGraphKway failed");
    for (idx_t i = 0; i < n; ++i) result.part[i] = part[i];

    // Cut edges = edges between different blocks
    std::vector<std::vector<int>> cut_adj(n);
    for (auto [a, b] : graph.edges) {
        if (part[a] != part[b]) {
            cut_adj[a].push_back(b);
            cut_adj[b].push_back(a);
        }
    }

    // Greedy vertex cover of the cut edges: repeatedly remove the clause
    // that touches the most still-uncovered cut edges.
    std::vector<int> deg(n);
    std::vector<char> removed(n, 0);
    std::priority_queue<std::pair<int,int>> pq;  // (degree, vertex), lazy updates
    for (int v = 0; v < n; ++v) {
        deg[v] = static_cast<int>(cut_adj[v].size());
        if (deg[v] > 0) pq.emplace(deg[v], v);
    }
    while (!pq.empty()) {
        auto [d, v] = pq.top(); pq.pop();
        if (removed[v] || d != deg[v] || d == 0) continue;  // stale entry
        removed[v] = 1;
        result.separator.push_back(v);
        for (int u : cut_adj[v]) {
            if (!removed[u] && deg[u] > 0) {
                --deg[u];
                if (deg[u] > 0) pq.emplace(deg[u], u);
            }
        }
        deg[v] = 0;
    }
    std::sort(result.separator.begin(), result.separator.end());
    return result;
}

std::vector<Formula> make_sub_formulas(const std::vector<std::vector<int>>& clauses,
                                       int num_variables, const DualGraph& graph, int blocks) {
    Split split = compute_split(graph, blocks);

    std::vector<char> in_sep(clauses.size(), 0);
    for (int c : split.separator) in_sep[c] = 1;

    // ADAPT: build whatever your Formula type needs; here I assume it can be
    // built from a vector of clauses.
    std::vector<std::vector<std::vector<int>>> block_clauses(blocks);
    for (size_t c = 0; c < clauses.size(); ++c)
        if (!in_sep[c]) block_clauses[split.part[c]].push_back(clauses[c]);

    std::vector<Formula> result;
    for (auto& bc : block_clauses) {
        Formula f;
        f.clauses = std::move(bc);
        f.num_variables = num_variables;
        result.push_back(std::move(f));
    }
    return result;
}

} // namespace dual_graph