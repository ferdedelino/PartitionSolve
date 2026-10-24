#include "PartitionPropagator.hpp"
#include <vector>
#include <cmath>
#include <cassert>
#include <iostream>
#include "ClauseLearner.hpp"
#include <cmath>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <random>



/*
High-Level functionality of this SAT solver:
 - We run an incremental solver over the common variables of the partial formulas.
 - This solver starts with no clauses.
 - Whenever the solver decides (or propagates) a variable, we take all currently set variables as "assumptions" and
   call an oracle on the partial formulas - given the assumptions. 
 - If one oracle is UNSAT under these assumptions, we signal our incremental solver to backtrack. The solver gets a
   conflict clause (unsatisfiable core from the oracle) to perform non-chronological backtracking. It internally
   learns this new clause.

The main (inremental) solver runs the base CDCL loop. When it decides (+ propagates) a varaible, we do the following:
 - Run a propagate() call with all currently set variables as assumptions on the oracles. This generates propagated literals.
 - If no propagations have been found, run a full solve() on the oracles.
 - Else: Propagate the literals to the main solver.
*/
PartitionPropagator::PartitionPropagator(const std::vector<Formula>& partial_formulas, const std::vector<int>& common_variables, int num_variables)
    : partial_formulas(partial_formulas), common_variables(common_variables), num_variables(num_variables)
{
    for (int var : this->common_variables) {
        this->common_variables_set.insert(var);
    }

    // Initialize the incremental solver
    this->solver = std::make_unique<CaDiCaL::Solver>();
    this->universal_false_var = num_variables + 1;
    this->solver->clause(-this->universal_false_var);
    this->solver->connect_external_propagator(this);
    for (int var = 1; var <= this->num_variables; ++var) {
        if (this->common_variables_set.count(var) > 0) this->solver->add_observed_var(var);
        else this->solver->clause(var);  // Fix value of this irrelevant variables, so the solver does not waste time on them.
    }
    this->solver->add_observed_var(this->universal_false_var); // We propagate universal_false_var. Only possible if it's observed
    this->are_reasons_forgettable = true;

    // Clause sharing
    this->main_learner = std::make_unique<ClauseLearner>(
        this->main_solver_clause_learn_size,
        [this](const std::vector<int>& clause) {
            this->onMainSolverLearned(clause);
        });
    if (this->use_clause_sharing) {
        this->solver->connect_learner(this->main_learner.get());
    }

    // Oracles
    for (Formula& partial_formula : this->partial_formulas) {
        auto oracle = std::make_unique<CaDiCaL::Solver>();
        for (const auto& clause : partial_formula.clauses) {
            oracle->clause(clause);
        }
        for (int var : this->common_variables) oracle->freeze(var);
        this->oracles.push_back(std::move(oracle));
    }

    this->level_trail.emplace_back();  // level 0
    this->fixed_literals.reserve(this->common_variables.size());
}

PartitionPropagator::~PartitionPropagator() {
    if (solver) {
        solver->disconnect_external_propagator();
        solver->disconnect_learner();
    }
}

int PartitionPropagator::solve() {
    // Maybe later: Make reusable

    int result = this->solver->solve();
    return result;
}

void PartitionPropagator::add_clause(const std::vector<int> clause) {
    this->solver->clause(clause);
}




// ==== ExternalPropagator callbacks // IPASIR-UP ====
void PartitionPropagator::notify_assignment(const std::vector<int> &lits) {
    if (this->DEBUG_LEVEL > 0) std::cout << "Notify assigned: "; 
    for (int lit : lits) {
        if (this->DEBUG_LEVEL > 0) std::cout << lit << " ";
        int var = std::abs(lit);
        if (var == universal_false_var) continue;   // keep U out of the assumptions
        this->assignment[var] = lit;
        this->level_trail.back().push_back(var);
        if (level_trail.size() == 1) {
            this->fixed_literals.push_back(lit);
        }
    }
    this->dirty = true;
    if (this->DEBUG_LEVEL > 0) std::cout << "\n";
}

void PartitionPropagator::notify_new_decision_level() {
    this->level_trail.push_back(std::vector<int>());
    if (this->DEBUG_LEVEL > 0) std::cout << " New Level(" << this->assignment.size() << ") ";
}

void PartitionPropagator::notify_backtrack(size_t new_level) {
    if (this->DEBUG_LEVEL > 0) std::cout << "Backtrack: " << (this->level_trail.size() - new_level) << "\n";
    while (this->level_trail.size() > new_level + 1) {
        for (int v : level_trail.back()) {
            auto itA = this->assignment.find(v);
            if (itA != this->assignment.end()) {
                this->pending_propagations.erase(itA->second);
                this->assignment.erase(itA);
            }
        }
        this->level_trail.pop_back();
    }
    if (this->level_trail.empty()) {
        this->level_trail.emplace_back();
    }
    this->dirty = true;
    for (int lit : this->propagation_queue) {
        this->pending_propagations.erase(lit);
    }
    this->propagation_queue.clear();
}


// Handle full models
bool PartitionPropagator::cb_check_found_model(const std::vector<int>& model) {
    // Assumtions = model
    assert(this->assignment.size() == this->common_variables.size());
    // TODO: check pending propagations for inconsistencies
    bool sat = this->test(model, false);
    // if this->propagation_queue.size()... Did not happen in testing
    if (sat) {
        this->has_model_reject_clause = false;
        this->model = model; // For later: this includes the universally false variable
        return true;
    }
    this->model_reject_clause.clear();
    for (int lit : this->unsat_core) {
        this->model_reject_clause.push_back(-lit);
    }
    this->has_model_reject_clause = true;
    //std::cout << "model size: " << model.size() << "\n";
    return false;
}

bool PartitionPropagator::cb_has_external_clause (bool &is_forgettable) {
    if (this->has_model_reject_clause) {
        is_forgettable = true;
        return true;
    }
    return false;
}

int PartitionPropagator::cb_add_external_clause_lit() {
    if (this->model_reject_clause.empty()) {
        this->has_model_reject_clause = false;
        return 0;
    }
    int last = this->model_reject_clause.back();
    this->model_reject_clause.pop_back();
    return last;
}


// Handle partial assignments
int PartitionPropagator::cb_propagate() {
    if (!this->propagation_queue.empty()) {
        int lit = this->propagation_queue.back();
        this->propagation_queue.pop_back();
        //this->pending_propagations.at(lit).is_propagated = true;
        return lit;
    }
    if (!this->dirty) return 0;
    this->dirty = false;

    std::vector<int> partial_assignment;
    for (const auto& pair : this->assignment) {
        partial_assignment.push_back(pair.second);
    }
    bool sat = this->test(partial_assignment, true);
    if (sat) {
        if (this->propagation_queue.empty()) return 0;
        int lit = this->propagation_queue.back();
        this->propagation_queue.pop_back();
        //this->pending_propagations.at(lit).is_propagated = true;
        return lit;
    }

    this->reason_clause.clear();
    for (int lit : this->unsat_core) {
        this->reason_clause.push_back(-lit);
    }
    this->reason_clause.push_back(this->universal_false_var);
    return this->universal_false_var;
}

int PartitionPropagator::cb_add_reason_clause_lit(int propagated_lit) {
    if (propagated_lit == this->universal_false_var) {        
        if (this->reason_pos >= this->reason_clause.size()) {
            this->reason_pos = 0;
            return 0;
        }
        return this->reason_clause[this->reason_pos++];
    }

    auto it = this->pending_propagations.find(propagated_lit);
    if (it == this->pending_propagations.end()) {
        std::cerr << "Cannot propagate literal: Not recorded\n";
        std::abort();
    }
    auto& prop = it->second;    
    assert(prop.lit == propagated_lit);
    if (!prop.antecedent_is_computed) {
        prop.antecedent = this->extract_reason(prop.oracle_id, *prop.assumptions, prop.lit);
        prop.antecedent_is_computed = true;
    }
    if (prop.clause_index >= prop.antecedent.size()) {
        prop.clause_index = 0;
        return 0;
    }
    return prop.antecedent[prop.clause_index++];
}


// Keep default for now. Do not suggest next variables
int PartitionPropagator::cb_decide() { return 0; }



// ==== Clause Sharing Functions ====
void PartitionPropagator::onMainSolverLearned(const std::vector<int>& clause) {
    this->clauses_from_main_solver.push_back(clause);   // Filter on flush
}


// ==== Custom functions ====
bool PartitionPropagator::test(const std::vector<int>& assumptions, bool partial) {
    this->share_fixed_literals();
    if (this->use_clause_sharing) this->share_clauses();
    if (partial) assert(this->propagation_queue.size() == 0); // TODO: is the partial necessary?
    std::shared_ptr<const std::vector<int>> shared_assumptions;
    auto get_shared = [&]() {
        if (!shared_assumptions)
            shared_assumptions = std::make_shared<const std::vector<int>>(assumptions);
        return shared_assumptions;
    };

    // Propagate assumptions on all oracles (fast)
    int res = 0;
    std::unordered_map<int, size_t> implied;

    for (size_t i = 0; i < this->oracles.size(); ++i) {
        auto& oracle = this->oracles[i];
        for (int lit : assumptions) {
            if (std::abs(lit) == this->universal_false_var) {
                continue;
            }
            oracle->assume(lit);
        }
        res = oracle->propagate();

        if (res == CaDiCaL::UNSATISFIABLE) {
            this->unsat_core.clear();
            for (int lit : assumptions) {
                if (std::abs(lit) == this->universal_false_var) continue;
                if (oracle->failed(lit)) this->unsat_core.push_back(lit);
            }
            this->minimize_core(this->unsat_core, i);
            break;
        } else if (partial) {  // Handle full assignments differently - full assignments should also not have any implications...
            std::vector<int> impl;
            oracle->implied(impl);
            for (int lit : impl) {
                if (!this->common_variables_set.count(std::abs(lit))) continue;
                if (implied.count(-lit) > 0) {
                    // We have a conflict, other propagations do no longer matter. Queue is empty here
                    this->propagation_queue.push_back(lit);
                    this->propagation_queue.push_back(-lit);
                    this->pending_propagations.insert_or_assign(lit, Propagation(lit, get_shared(), i));
                    this->pending_propagations.insert_or_assign(-lit, Propagation(-lit, get_shared(), implied[-lit]));
                    return true; // Ignore everything else, imply these propagations
                }
                implied[lit] = i;
            }
        }
    }

    // TODO: compare implications against assignment (not needed I guess - did test it and it made the whole thin 5% slower)
    if (res == CaDiCaL::UNSATISFIABLE) {
        return false;
    }

    if (partial) {
        for (int lit : assumptions) {
            implied.erase(lit);
            assert(implied.count(-lit) == 0); // This case should not happen. If -lit is implied, propagate() should return UNSAT
        }

        for (auto itImp = implied.begin(); itImp != implied.end(); ) {
            int lit = itImp->first;
            size_t oracle_id = itImp->second;

            auto it = assignment.find(std::abs(lit));
            if (it != assignment.end()) {
                int current_lit = it->second;
                assert(std::abs(lit) == std::abs(current_lit));
                if (lit == current_lit) {
                    itImp = implied.erase(itImp);
                    continue;
                } else {
                    this->propagation_queue.push_back(lit);
                    pending_propagations.insert_or_assign(lit, Propagation(lit, get_shared(), oracle_id));
                    return true;
                }
            }
            ++itImp;
        }
        if (this->DEBUG_LEVEL == 0) std::cout << "Implied: ";
        for (auto const& [lit, oracle_id] : implied) {
            if (this->DEBUG_LEVEL == 0) std::cout << lit << " ";
            this->propagation_queue.push_back(lit);
            this->pending_propagations.insert_or_assign(lit, Propagation(lit, get_shared(), oracle_id));
        }
        if (this->DEBUG_LEVEL == 0) std::cout << "\n";
        if (!this->do_oracle_solves_on_partial || !implied.empty()) {
            return true; // Propagate the lits before doing full solve() calls.
        }
    }


    // Full solve over the assumptions (slow)
    bool sat = true;
    for (size_t i = 0; i < this->oracles.size(); ++i) {
        auto& oracle = this->oracles[i];
        for (int lit : assumptions) {
            if (std::abs(lit) == this->universal_false_var) {
                continue;
            }
            oracle->assume(lit);
        }
        int result = oracle->solve();
        if (result == CaDiCaL::UNSATISFIABLE) {
            this->unsat_core.clear();
            sat = false;
            for (int lit : assumptions) {
                if (std::abs(lit) == this->universal_false_var) continue;
                if (oracle->failed(lit)) this->unsat_core.push_back(lit);
            }
            this->minimize_core(this->unsat_core, i);
            break;
        }

    }
    return sat;
}

std::vector<int> PartitionPropagator::extract_reason(size_t oracle_id, const std::vector<int>& assumptions, int lit) {
    // Assumptions imply lit --> assumptions and -lit => conflict => extract core
    // We assume that assumptions do not contain lit or -lit
    auto& oracle = this->oracles[oracle_id];
    for (int a : assumptions) {
        if (std::abs(a) == this->universal_false_var) {
            continue;
        }
        oracle->assume(a);
    }
    oracle->assume(-lit);
    int res = oracle->propagate();
    auto reason_core = std::vector<int>();
    if (res == CaDiCaL::UNSATISFIABLE) {
        for (int a : assumptions) {
            if (std::abs(a) == this->universal_false_var) continue;
            if (oracle->failed(a)) {
                reason_core.push_back(a);
            }
        }
        this->minimize_reason(reason_core, oracle_id, lit);
        for (size_t i = 0; i < reason_core.size(); ++i) {
            reason_core[i] *= -1;
        }
        reason_core.push_back(lit);

        // Note: failed(-lit) may be false if the assumptions alone already conflict (we calculate the reasons lazily).
        // The clause (lit ∨ ¬core) is still a valid reason in that case.
        return reason_core;
    }

    
    // Rare case where learned clauses that lead to a propagation no longer exist
    for (int a : assumptions) {
        if (std::abs(a) == this->universal_false_var) {
            continue;
        }
        reason_core.push_back(-a);
    }
    std::cout << "Fallback reason\n";
    reason_core.push_back(lit);
    return reason_core;
}

void PartitionPropagator::share_fixed_literals() {
    assert(this->fixed_literals_index <= this->fixed_literals.size());
    if (this->fixed_literals_index >= fixed_literals.size()) {
        return;
    }

    for (size_t index = this->fixed_literals_index; index < this->fixed_literals.size(); ++index) {
        int lit = this->fixed_literals[index];
        for (auto& oracle : this->oracles) {
            oracle->clause(lit);
        }
    }
   fixed_literals_index = fixed_literals.size();
}

void PartitionPropagator::share_clauses() {
    if (this->clauses_from_main_solver.empty()) return;

    std::vector<int> filtered;
    for (const auto& clause : this->clauses_from_main_solver) {
        filtered.clear();
        bool satisfied = false;
        for (int lit : clause) {
            if (lit == -this->universal_false_var) {
                std::cout << "Tried to learn a clause with the universally false literal being true\n";
                satisfied = true; 
                break; 
            }
            if (lit == this->universal_false_var) {
                std::cout << "Tried to learn a clause with the universally false literal being false\n";
                continue;
            }
            filtered.push_back(lit);
        }
        if (satisfied || filtered.empty()) continue;

        /*std::cout << "Learned clause: ";
        for (int lit : filtered) {
            std::cout << lit << " ";
        }
        std::cout << "\n";
        */


        for (auto& oracle : this->oracles) {
            oracle->clause(filtered);
        }
    }
    this->clauses_from_main_solver.clear();
}

void PartitionPropagator::minimize_core(std::vector<int>& core, size_t oracle_id) {
    auto& oracle = this->oracles[oracle_id];
    size_t old_size = core.size();
    // Reverse order. Important!
    for (auto it = core.rbegin(); it != core.rend(); ++it) oracle->assume(*it);
    int res = oracle->solve();
    assert(res == CaDiCaL::UNSATISFIABLE);
    (void)res;   // avoids an unused warning if asserts are compiled out

    size_t keep = 0;
    for (int a : core) {
        if (oracle->failed(a)) core[keep++] = a;
    }
    core.resize(keep);
    //if (core.size() < old_size) std::cout << "Minimize: From " << old_size << " to " << core.size() << "\n";
    //else std::cout << "Could not minimize!\n";
}

void PartitionPropagator::minimize_reason(std::vector<int>& reason_core, size_t oracle_id, int lit) {
    auto& oracle = this->oracles[oracle_id];
    size_t old_size = reason_core.size();
    oracle->assume(-lit);

    for (auto it = reason_core.rbegin(); it != reason_core.rend(); ++it) oracle->assume(*it);
    int res = oracle->propagate();
    if (res != CaDiCaL::UNSATISFIABLE) {
        return;
    }

    size_t keep = 0;
    for (int a : reason_core) {
        if (oracle->failed(a)) reason_core[keep++] = a;
    }
    reason_core.resize(keep);
    if (reason_core.size() < old_size) {
        std::shuffle(reason_core.begin(), reason_core.end(), this->rng_randomizer);
        minimize_reason(reason_core, oracle_id, lit);
    }
    //if (reason_core.size() < old_size) std::cout << "Minimize: From " << old_size << " to " << reason_core.size() << "\n";
    //else std::cout << "Could not minimize!\n";
}

void PartitionPropagator::print_infos() {
    
}

