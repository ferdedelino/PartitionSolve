#include <iostream>
#include <string>
#include "cadical.hpp"
#include "hypergraph_split.hpp"
#include "prime_graph_split.hpp"
#include "dual_graph_split.hpp"
#include "PartitionPropagator.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cassert>

using namespace std;
namespace fs = std::filesystem;

int split_and_solve(const std::string& path, std::string& split_type);

struct ClauseCollector : CaDiCaL::ClauseIterator {
    Formula formula;
    bool clause(const vector<int>& c) override {
        formula.clauses.push_back(c);
        for (int lit : c) {
            formula.num_variables = max(formula.num_variables, abs(lit));
        }
        return true; // return false to stop early
    }
};


int main(int argc, char** argv) {
    std::cout << "Hello World!\n";
    //string path = (argc > 1) ? argv[1] : "instances/uf150-01.cnf";
    //string path = (argc > 1) ? argv[1] : "instances/uuf100-01.cnf";
    //string path = (argc > 1) ? argv[1] : "instances/hole6.cnf";
    // hole9.cnf
    // par16-2.cnf
    // bmc-ibm-12  -- sat
    // qg6-12.cnf  -- unsat
    //string path = (argc > 1) ? argv[1] : "instances/test/benchmarks/qg6-12.cnf";
    string path = (argc > 1) ? argv[1] : "instances/test/benchmarks/hole9.cnf";
    

    //string path = (argc > 1) ? argv[1] : "instances/instances_small/0ae917c2e0c1d39cbe411c29b2f5ff08-dislog_a14_x14_n24.cnf.xz";

    std::string split_type = "hypergraph"; // "dual_nodes", "prime_edges", "hypergraph"
    if (true) {
        split_and_solve(path, split_type);
        return 0;
    }
    
    fs::path root = "instances";

    try {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) {
                split_and_solve(entry.path().string(), split_type);
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
    }


}

int split_and_solve(const std::string& path, string& split_type) {
    // ==== Use Cadical to read the DIMACS file ====
    CaDiCaL::Solver solver;
    solver.set("quiet", 1);
    int vars = 0;
    const char* err = solver.read_dimacs(path.c_str(), vars);
    if (err) {
        cerr << "Parse error: " << err << "\n";
        return 0;
    }


    // ==== Use the solvers preprocessing to simplify ====
    ClauseCollector collector;
    auto start = chrono::high_resolution_clock().now();
    int simlifyResult = solver.simplify();
    auto simplify_time = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
    if (simlifyResult != 0) {
        string str = simlifyResult == 10 ? "SAT" : "UNSAT";
        std::cout << "Solved during preprocessing: " << str << "\n";
        return 1;
    }
    
    solver.traverse_clauses(collector);

    std::cout << path << ": " << "Vars: " << collector.formula.num_variables << ", Clauses: " << collector.formula.clauses.size() << "\n";

    // Compare to CaDiCaL directly:
    start = chrono::high_resolution_clock().now();
    int cad_result = 0;
    chrono::milliseconds duration;
    if (false) {
        cad_result = solver.solve();
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "CaDiCaL (" << cad_result << ") Time: " << duration.count() << "ms == \n";
    }
    
    // Dual Hypergraph. We cut by removing hyperedges (variables)
    if (split_type == "hypergraph") {
        // ==== Create hypergraph from our clauses ====
        auto hypergraph_edges = hypergraph_split::clauses_to_hypergraph(collector.formula.clauses, collector.formula.num_variables);

        // ==== Split hypergraph ====
        start = chrono::high_resolution_clock().now();
        std::vector<Formula> sub_formulas = hypergraph_split::split_hypergraph(collector.formula, hypergraph_edges, 2);
        vector<int> common_variables = hypergraph_split::extract_common_variables(sub_formulas, collector.formula.num_variables);
        std::cout << "Common variables: " << common_variables.size() << " / " << vars << " == ";
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "Partition Time: " << duration.count() << "ms == \n";

        // ==== Solve split formula ====
        start = chrono::high_resolution_clock().now();
        PartitionPropagator part_solver(sub_formulas, common_variables, collector.formula.num_variables);
        int result = part_solver.solve();
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "PartitionSolve (" << result << ") Time: " << (duration.count() + simplify_time.count()) << "ms\n";
        if (cad_result != 0) assert(result == cad_result);
        return result;
    }


    // ==== The following is experimentation. It can be ignored ====

    // Dual Graph: We cut by removing nodes (clauses)
    if (split_type == "dual_nodes") {
        // ==== Create and split dual graph ====
        start = chrono::high_resolution_clock().now();
        auto graph = dual_graph::clauses_to_dual_graph(collector.formula.clauses, collector.formula.num_variables);
        auto split = dual_graph::compute_split(graph, 2);
        auto sub_formulas = dual_graph::make_sub_formulas(collector.formula.clauses, collector.formula.num_variables, graph, 2);
        Formula shared_formula;
        shared_formula.num_variables = collector.formula.num_variables;
        for (auto clause_id : split.separator) {
            shared_formula.clauses.push_back(collector.formula.clauses[clause_id]);
        } 
        std::vector<int> common_variables;
        std::unordered_set<int> c_var;
        for (auto& clause : shared_formula.clauses) {
            for (int lit : clause) {
                std::cout << lit << " ";
                c_var.insert(std::abs(lit));
            }
            std::cout << "\n";
        }
        for (int var : c_var) {
            common_variables.push_back(var);
        }
        std::cout << "Partition Time: " << duration.count() << "ms == \n";
        std::cout << "Shared Vars:" << common_variables.size() << ", Clauses: " << shared_formula.clauses.size() << "\n";

        // ==== Solve split formula ====
        start = chrono::high_resolution_clock().now();
        PartitionPropagator part_solver(sub_formulas, common_variables, collector.formula.num_variables);
        for (auto& clause : shared_formula.clauses) {
            part_solver.add_clause(clause);
        }

        int result = part_solver.solve();
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "PartitionSolve (" << result << ") Time: " << (duration.count() + simplify_time.count()) << "ms\n";
        if (cad_result != 0) assert(result == cad_result);
        return 0;
    }

    // Prime graph where we cut by removing edges aka clauses
    if (split_type == "prime_edges") {
        // ==== Create and split primal graph ====
        start = chrono::high_resolution_clock().now();
        auto graph = prime_graph::clauses_to_primal_graph(collector.formula.clauses, collector.formula.num_variables);
        auto var_partition = prime_graph::split_primal_graph(graph, 2);
        auto sub_formulas = prime_graph::make_sub_formulas(collector.formula.clauses, collector.formula.num_variables, var_partition, 2);
        auto shared_formula = sub_formulas[2];
        sub_formulas.pop_back();
        std::vector<int> common_variables;
        std::unordered_set<int> c_var;
        for (auto& clause : shared_formula.clauses) {
            for (int lit : clause) {
                //std::cout << lit << " ";
                c_var.insert(std::abs(lit));
            }
            //std::cout << "\n";
        }
        for (int var : c_var) {
            common_variables.push_back(var);
        }
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "Partition Time: " << duration.count() << "ms == \n";
        std::cout << "Shared Vars:" << common_variables.size() << ", Clauses: " << shared_formula.clauses.size() << "\n";

        // ==== Solve split formula ====
        start = chrono::high_resolution_clock().now();
        PartitionPropagator part_solver(sub_formulas, common_variables, collector.formula.num_variables);
        for (auto& clause : shared_formula.clauses) {
            part_solver.add_clause(clause);
        }

        int result = part_solver.solve();
        duration = chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock().now() - start);
        std::cout << "PartitionSolve (" << result << ") Time: " << (duration.count() + simplify_time.count()) << "ms\n";
        if (cad_result != 0) assert(result == cad_result);
        return 0;
    }
}

