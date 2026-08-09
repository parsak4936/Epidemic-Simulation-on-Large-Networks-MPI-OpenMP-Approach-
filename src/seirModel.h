// the seir epidemic model. every person (a node in the graph) is in one of four
// health states, and over time they move from susceptible, to exposed, to
// infectious, and finally to recovered.
#pragma once
#include "graph.h"
#include <vector>
#include <string>

enum class HealthState { susceptible, exposed, infectious, recovered };

// the knobs that control how the epidemic behaves
struct SeirParameters {
    double transmissionProbability;  // beta,  chance an infectious node infects a susceptible neighbor each step
    double incubationProbability;    // sigma, chance an exposed node turns infectious each step
    double recoveryProbability;      // gamma, chance an infectious node recovers each step
    int numberOfSteps;               // how many days we simulate
    int initialInfectedCount;        // how many people start off infectious
    unsigned int randomSeed;         // fixed seed so runs are reproducible
};

// how many people are in each state at one moment (one row of the output csv)
struct StateCounts {
    int susceptible;
    int exposed;
    int infectious;
    int recovered;
};

// a deterministic random number in [0,1) built from integer keys. the same
// inputs always give the same value on any process or thread. this is the trick
// that lets the parallel version reproduce the sequential run exactly.
double deterministicRandomUnit(unsigned int globalSeed, int nodeId, int step, int counter);

// place the starting infectious people into the state array. both the sequential
// and the parallel versions call this, so they always begin from the same state.
void seedInitialInfected(std::vector<HealthState>& state, const SeirParameters& parameters);

// work out the next state of one person from everyone's current state. this is a
// pure function (it only depends on the id, the day, and the current states), so
// it returns the same answer no matter which process or thread runs it. both the
// sequential and parallel versions share it, which is what keeps them identical.
HealthState nextStateForNode(int node, int step, const Graph& graph,
                             const std::vector<HealthState>& currentState,
                             const SeirParameters& parameters);

// run the whole simulation on one process and return the counts for every step
std::vector<StateCounts> runSeirSimulation(const Graph& graph, const SeirParameters& parameters);

// write the per step counts to a csv file (throws if the file cannot be opened)
void writeCountsToCsv(const std::vector<StateCounts>& history, const std::string& fileName);
