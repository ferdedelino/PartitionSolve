# PartitionSolve
PartitionSolve is a proof of concept SAT solver. It works by splitting a CNF SAT formula into sub-formulas. The solver searches over the shared variables between the sub-formulas. Given a full assignment of the shared variables, the solver checks if the sub-formulas are satisfiable under these assignments.

The solver is based upon the incremental SAT solver [CaDiCaL](https://github.com/arminbiere/cadical).

## Results
PartitionSolve is able to improve solve times for some instances up to 25x. Especially the pigeonhole encodings from the [SATLIB benchmarks](https://www.cs.ubc.ca/~hoos/SATLIB/benchm.html) see big improvements.

Unfortunately the solver struggles with most other instances, especially satsifiable ones.

Example: The pigeonhole problem with 10 holes (taken from [here](https://www.cs.ubc.ca/~hoos/SATLIB/Benchmarks/SAT/DIMACS/PHOLE/descr.html)) takes **27.0 seconds** to solve on CaDiCal 3.0.1 with standard settings. PartitionSolve solves it in **1.1 seconds**.

## Requirements
- Linux or macOS
- A C++20 compiler (GCC 10+ or Clang 12+)
- CMake 3.16 or newer
- Git, Make
- [METIS](https://github.com/KarypisLab/METIS) (library and headers), installed system-wide
- Internet access during the first configure/build

## Functionalities
The solver is a proof of concept. In its current form it does not support model extraction, unsat proofs or any kind of incremental functionalities.

## Core Idea
1) split a CNF SAT formula into two (or more) sub-formulas.
2) Extract all variables shared between the sub-formulas. If all shared variables are assigned, we can solve the sub-formulas independently.
3) Initiate a CaDiCaL instance that runs over the shared variables. Connect an external propagator that checks
    - **full models** against the sub-formulas: On each sub-formula run a CaDiCaL instance with the shared variables as assumptions. If one sub-formula is unsat, extract the unsatisfiable core and provide it as an external reason to the propagator.
    - **partial assignments** of shared variables: On each sub-formula run a CaDiCaL instance. Take the partial assignment as assumptions and call the propagate() function of CaDiCaL. Extract all shared variables that got implied by the propagate() on one of the sub-formulas.