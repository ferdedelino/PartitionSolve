#pragma once
#include <functional>
#include "cadical.hpp"
#include <vector>


class ClauseLearner : public  CaDiCaL::Learner 
{
public:
    using ClauseCallback = std::function<void(const std::vector<int>&)>;

    ClauseLearner(int maxClauseSize, ClauseCallback cb);
    ~ClauseLearner() override = default;

    bool learning (int size) override;
    void learn (int lit) override;


private:
    int maxClauseSize;
    std::vector<int> clauseBuffer;
    ClauseCallback onClauseLearned;
};