#include "seirModel.h"
#include <fstream>
#include <stdexcept>
#include <cstdint>

// a random number in [0,1) computed from its inputs instead of from hidden state.
// the same (seed, person, day, counter) always gives the same number, on any
// process or thread, which is what lets the parallel run match the sequential one
// exactly. the constants are the usual splitmix64 mixing values.
double deterministicRandomUnit(unsigned int globalSeed, int nodeId, int step, int counter) {
    std::uint64_t mixed = static_cast<std::uint64_t>(globalSeed) * 0x9E3779B97F4A7C15ULL
                        + static_cast<std::uint64_t>(nodeId)     * 0xD1B54A32D192ED03ULL
                        + static_cast<std::uint64_t>(step)       * 0xA0761D6478BD642FULL
                        + static_cast<std::uint64_t>(counter)    * 0x8EBC6AF09C88C6E3ULL;
    mixed = mixed ^ (mixed >> 30);
    mixed = mixed * 0xBF58476D1CE4E5B9ULL;
    mixed = mixed ^ (mixed >> 27);
    mixed = mixed * 0x94D049BB133111EBULL;
    mixed = mixed ^ (mixed >> 31);
    // top 53 bits scaled into 0..1
    return (mixed >> 11) * (1.0 / 9007199254740992.0);
}

// pick the first infectious people. uses the same deterministic dice, so every
// process agrees on who they are without sending any messages.
void seedInitialInfected(std::vector<HealthState>& state, const SeirParameters& parameters) {
    int numberOfNodes = static_cast<int>(state.size());
    int seededSoFar = 0;
    int attempt = 0;
    while (seededSoFar < parameters.initialInfectedCount && seededSoFar < numberOfNodes) {
        double roll = deterministicRandomUnit(parameters.randomSeed, -1, 0, attempt);
        int node = static_cast<int>(roll * numberOfNodes);
        attempt = attempt + 1;
        if (node >= 0 && node < numberOfNodes && state[node] == HealthState::susceptible) {
            state[node] = HealthState::infectious;
            seededSoFar = seededSoFar + 1;
        }
    }
}

// work out one person's next state from everyone's current state.
// this is where about 90 percent of the runtime goes, so it is the function we
// parallelise. it only reads the shared state and its dice depend on the person
// and the day, so any number of threads can run it at once safely.
HealthState nextStateForNode(int node, int step, const Graph& graph,
                             const std::vector<HealthState>& currentState,
                             const SeirParameters& parameters) {
    HealthState state = currentState[node];

    if (state == HealthState::susceptible) {
        // pull model: I check my own infectious neighbours rather than infectious
        // people reaching out to me. that way I only ever write my own state.
        int start = graph.neighborStartIndex[node];
        int end = graph.neighborStartIndex[node + 1];
        int counter = 0;
        for (int slot = start; slot < end; slot = slot + 1) {
            int neighbor = graph.neighborList[slot];
            if (currentState[neighbor] == HealthState::infectious) {
                double roll = deterministicRandomUnit(parameters.randomSeed, node, step, counter);
                counter = counter + 1;
                if (roll < parameters.transmissionProbability) {
                    return HealthState::exposed;   // one is enough
                }
            }
        }
        return HealthState::susceptible;
    }

    if (state == HealthState::exposed) {
        double roll = deterministicRandomUnit(parameters.randomSeed, node, step, 0);
        if (roll < parameters.incubationProbability) return HealthState::infectious;
        return HealthState::exposed;
    }

    if (state == HealthState::infectious) {
        double roll = deterministicRandomUnit(parameters.randomSeed, node, step, 0);
        if (roll < parameters.recoveryProbability) return HealthState::recovered;
        return HealthState::infectious;
    }

    return state;   // recovered stays recovered
}

// how many people are in each state right now
static StateCounts countStates(const std::vector<HealthState>& states) {
    StateCounts counts{0, 0, 0, 0};
    for (HealthState state : states) {
        if (state == HealthState::susceptible)      counts.susceptible = counts.susceptible + 1;
        else if (state == HealthState::exposed)     counts.exposed = counts.exposed + 1;
        else if (state == HealthState::infectious)  counts.infectious = counts.infectious + 1;
        else                                        counts.recovered = counts.recovered + 1;
    }
    return counts;
}

// the plain sequential simulation, used as the baseline for every measurement
std::vector<StateCounts> runSeirSimulation(const Graph& graph,
                                           const SeirParameters& parameters) {
    std::vector<HealthState> currentState(graph.numberOfNodes, HealthState::susceptible);
    seedInitialInfected(currentState, parameters);

    std::vector<StateCounts> history;
    history.push_back(countStates(currentState));

    for (int step = 0; step < parameters.numberOfSteps; step = step + 1) {
        // decide everything from the current states, then apply it all at once.
        // otherwise someone infected early in the loop could spread the disease
        // again on the same day.
        std::vector<HealthState> nextState = currentState;
        for (int node = 0; node < graph.numberOfNodes; node = node + 1) {
            nextState[node] = nextStateForNode(node, step, graph, currentState, parameters);
        }
        currentState = nextState;
        history.push_back(countStates(currentState));
    }

    return history;
}

// write the daily counts so the epidemic curve can be plotted
void writeCountsToCsv(const std::vector<StateCounts>& history, const std::string& fileName) {
    std::ofstream output(fileName);
    if (!output) {
        throw std::runtime_error("could not open output file: " + fileName);
    }

    output << "step,susceptible,exposed,infectious,recovered\n";
    for (std::size_t step = 0; step < history.size(); step = step + 1) {
        const StateCounts& counts = history[step];
        output << step << ","
               << counts.susceptible << ","
               << counts.exposed << ","
               << counts.infectious << ","
               << counts.recovered << "\n";
    }
}
