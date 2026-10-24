#pragma once
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "cadical.hpp"
#include "ClauseLearner.hpp"
#include <random>

struct Formula {
    std::vector<std::vector<int>> clauses;
    int num_variables = 0;
};

struct Propagation {
    int lit;
    std::shared_ptr<const std::vector<int>> assumptions;
    size_t oracle_id;

    bool is_propagated = false;
    bool antecedent_is_computed = false;
    size_t clause_index = 0;
    std::vector<int> antecedent;
    
    Propagation(int lit, std::shared_ptr<const std::vector<int>> assumptions, size_t oracle_id)
        : lit(lit), assumptions(std::move(assumptions)), oracle_id(oracle_id) {}
};


class PartitionPropagator : public CaDiCaL::ExternalPropagator
{

public:
    PartitionPropagator(
        const std::vector<Formula>& partial_formulas, 
        const std::vector<int>& common_variables, 
        int num_variables);
    ~PartitionPropagator() override;

    int solve();
    const std::vector<int>& get_model() const { return model; }
    void add_clause(const std::vector<int> clause);

    // --- ExternalPropagator callbacks ---
    void notify_assignment (const std::vector<int> &lits) override;
    void notify_new_decision_level() override;
    void notify_backtrack(size_t new_level) override;
    bool cb_check_found_model(const std::vector<int>& model) override;
    int  cb_decide() override;
    int  cb_propagate() override;
    int  cb_add_reason_clause_lit(int propagated_lit) override;
    bool cb_has_external_clause (bool &is_forgettable) override;
    int  cb_add_external_clause_lit() override;

    // --- Lerner callbacks ---
    void onMainSolverLearned(const std::vector<int>& clause);

private:
    // Constructor
    std::vector<Formula> partial_formulas;
    std::vector<int> common_variables;
    std::unordered_set<int> common_variables_set; // TODO: this can probably be optimized
    int num_variables;

    // Solvers
    std::unique_ptr<CaDiCaL::Solver> solver;
    int universal_false_var;
    std::vector<std::unique_ptr<CaDiCaL::Solver>> oracles;
    std::vector<int> model;

    // State
    int forced_variable;
    std::vector<std::vector<int>> level_trail{};
    std::unordered_map<int, int> assignment;  // Variable to literal
    bool dirty = false;

    // Fixed literals listener
    std::vector<int> fixed_literals;
    size_t fixed_literals_index = 0;
    
    // Clause sharing
    std::unique_ptr<ClauseLearner> main_learner;
    std::vector<std::vector<int>> clauses_from_main_solver;

    // IPASIR-UP interactions
    std::vector<int> unsat_core;
    std::vector<int> reason_clause;
    std::vector<int> model_reject_clause;
    bool has_model_reject_clause = false;
    size_t reason_pos = 0;
    std::vector<int> propagation_queue;
    std::unordered_map<int, Propagation> pending_propagations;
    
    // Statistics
    double test_time = 0.0;
    int num_checks = 0;
    int num_partial_checks = 0;
    int num_model_checks = 0;
    int added_reasons = 0;

    // Config
    const int DEBUG_LEVEL = 0;
    bool use_clause_sharing = true;
    bool do_oracle_solves_on_partial = false;
    int main_solver_clause_learn_size = 3;

    // Random stuff
    std::mt19937 rng_randomizer{42};

    // Internal functions
    bool test(const std::vector<int>& assumptions, bool partial);
    std::vector<int> extract_reason(size_t oracle_id, const std::vector<int>& assumptions, int lit);
    void minimize_core(std::vector<int>& core, size_t oracle_id);
    void minimize_reason(std::vector<int>& reason, size_t oracle_id, int lit);
    void share_fixed_literals();
    void share_clauses();
    void print_infos();
};