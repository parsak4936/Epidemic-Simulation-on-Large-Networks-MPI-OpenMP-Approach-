// phase 3a driver: run the WHOLE epidemic response system in one process, so we
// can see all four operations working together before we make them run at the
// same time on different processes (that is phase 3b).
//
// each day we run:
//   task A  spread dynamics      (who gets infected next)
//   task B  contact tracing      (trace back from the newly infected)
//   task D  outbreak clustering  (how many active clusters, and the biggest)
// and every few days:
//   task C  intervention target  (pick central people and vaccinate them)
//
// usage:  ./responseSimulation [numberOfNodes] [networkType] [seed]
#include "graph.h"
#include "networkGenerators.h"
#include "seirModel.h"
#include "analysisTasks.h"

#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>

int main(int argc, char** argv) {
    int numberOfNodes = 20000;
    std::string networkType = "scalefree";
    unsigned int randomSeed = 12345;
    if (argc >= 2) numberOfNodes = std::stoi(argv[1]);
    if (argc >= 3) networkType = argv[2];
    if (argc >= 4) randomSeed = static_cast<unsigned int>(std::stoul(argv[3]));

    EdgeList edges;
    if (networkType == "smallworld") {
        edges = generateSmallWorldNetwork(numberOfNodes, 6, 0.1, randomSeed);
    } else {
        networkType = "scalefree";
        edges = generateScaleFreeNetwork(numberOfNodes, 3, randomSeed);
    }
    Graph graph = Graph::fromEdgeList(numberOfNodes, edges);

    SeirParameters parameters;
    parameters.transmissionProbability = 0.05;
    parameters.incubationProbability = 0.20;
    parameters.recoveryProbability = 0.10;
    parameters.numberOfSteps = 120;
    parameters.initialInfectedCount = 5;
    parameters.randomSeed = randomSeed;

   
    const int tracingDepth = 2;               // task B: trace 2 hops back
    const int centralityIterations = 20;      // task C: pagerank rounds
    const int interventionInterval = 20;      // task C runs every 20 days
    const int vaccinesPerRound = numberOfNodes / 100;   // 1% vaccinated each time

    std::vector<HealthState> currentState(numberOfNodes, HealthState::susceptible);
    seedInitialInfected(currentState, parameters);

    std::filesystem::create_directories("results");
    std::ofstream log("results/responseLog.csv");
    log << "step,susceptible,exposed,infectious,recovered,newlyInfected,contactsTraced,clusters,largestCluster,vaccinatedThisStep\n";

    int totalVaccinated = 0;

    for (int step = 0; step < parameters.numberOfSteps; step = step + 1) {
        std::vector<HealthState> previousState = currentState;

        // task A: advance the spread
        std::vector<HealthState> nextState = currentState;
        for (int node = 0; node < numberOfNodes; node = node + 1) {
            nextState[node] = nextStateForNode(node, step, graph, currentState, parameters);
        }
        currentState = nextState;

        // task B: contact tracing from the people infected this step
        TracingResult tracing = runContactTracing(graph, currentState, previousState, tracingDepth);

        // task D: cluster the currently infectious people
        ClusteringResult clustering = runOutbreakClustering(graph, currentState);

        // task C: every so often, pick central people and vaccinate them
        int vaccinatedThisStep = 0;
        if (step % interventionInterval == 0) {
            TargetingResult targeting = runInterventionTargeting(graph, currentState,
                                                                 vaccinesPerRound, centralityIterations);
            for (int node : targeting.targets) {
                if (currentState[node] == HealthState::susceptible) {
                    currentState[node] = HealthState::recovered;   // vaccinated = immune
                    vaccinatedThisStep = vaccinatedThisStep + 1;
                }
            }
            totalVaccinated = totalVaccinated + vaccinatedThisStep;
        }

        // count the states for the log
        int counts[4] = {0, 0, 0, 0};
        for (int node = 0; node < numberOfNodes; node = node + 1) {
            counts[static_cast<int>(currentState[node])] = counts[static_cast<int>(currentState[node])] + 1;
        }

        log << (step + 1) << ","
            << counts[0] << "," << counts[1] << "," << counts[2] << "," << counts[3] << ","
            << tracing.newlyInfectedCount << "," << tracing.contactsTraced << ","
            << clustering.clusterCount << "," << clustering.largestClusterSize << ","
            << vaccinatedThisStep << "\n";
    }

    int finalInfectedEver = 0;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (currentState[node] == HealthState::recovered) finalInfectedEver = finalInfectedEver + 1;
    }

    std::cout << "network: " << networkType << " , nodes: " << numberOfNodes
              << " , edges: " << edges.size() << "\n";
    std::cout << "ran all four tasks for " << parameters.numberOfSteps << " days\n";
    std::cout << "total vaccinated by task C: " << totalVaccinated << "\n";
    std::cout << "recovered or vaccinated at the end: " << finalInfectedEver << "\n";
    std::cout << "wrote results/responseLog.csv\n";
    return 0;
}
