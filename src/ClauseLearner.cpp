#include <vector>
#include "cadical.hpp"
#include "ClauseLearner.hpp"

ClauseLearner::ClauseLearner(int maxClauseSize, ClauseCallback cb)
    : maxClauseSize(maxClauseSize), onClauseLearned(std::move(cb)) {}

bool ClauseLearner::learning (int size) {
    return size <= this->maxClauseSize;
}

void ClauseLearner::learn (int lit) {
    if (lit != 0) {
        this->clauseBuffer.push_back(lit);
    } else {
        this->onClauseLearned(clauseBuffer);
        clauseBuffer.clear();
    }
}
