// Measures each of the four tasks on its own, at several thread counts, so we can
// show that every task is data parallel inside (not just the spread). It also
// checks that the answer does not change with the thread count, which is what makes
// the parallel versions trustworthy.
//
// usage:  ./taskBenchmark [numberOfNodes] [networkType] [seed]
//   set the thread counts with the loop below; run it once and it sweeps them.
#include "graph.h"
#include "networkGenerators.h"
#include "seirModel.h"
#include "analysisTasks.h"
#include "measurement.h"

#include <omp.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    int numberOfNodes = 500000;
    std::string networkType = "smallworld";
    unsigned int randomSeed = 777;
    if (argc >= 2) numberOfNodes = std::stoi(argv[1]);
    if (argc >= 3) networkType = argv[2];
    if (argc >= 4) randomSeed = static_cast<unsigned int>(std::stoul(argv[3]));

    EdgeList edges;
    if (networkType == "scalefree") {
        edges = generateScaleFreeNetwork(numberOfNodes, 3, randomSeed);
    } else {
        networkType = "smallworld";
        edges = generateSmallWorldNetwork(numberOfNodes, 6, 0.1, randomSeed);
    }
    Graph graph = Graph::fromEdgeList(numberOfNodes, edges);

    SeirParameters parameters;
    parameters.transmissionProbability = 0.05;
    parameters.incubationProbability = 0.20;
    parameters.recoveryProbability = 0.10;
    parameters.numberOfSteps = 120;
    parameters.initialInfectedCount = 5;
    parameters.randomSeed = randomSeed;

    // run the epidemic forward a while so there is a realistic mix of states to analyse
    std::vector<HealthState> state(numberOfNodes, HealthState::susceptible);
    seedInitialInfected(state, parameters);
    for (int step = 0; step < 60; step = step + 1) {
        std::vector<HealthState> nextState = state;
        for (int node = 0; node < numberOfNodes; node = node + 1) {
            nextState[node] = nextStateForNode(node, step, graph, state, parameters);
        }
        state = nextState;
    }

    std::filesystem::create_directories("results");
    std::ofstream out("results/taskThreads.csv");
    out << "task,threads,seconds,check\n";

    std::cout << "network: " << networkType << ", people: " << numberOfNodes << "\n";
    std::cout << "cores available: " << hardwareCoreCount() << "\n\n";
    std::cout << "task              threads   seconds   speedup   answer check\n";

    const int threadCounts[] = {1, 2, 4, 8};
    const char* taskNames[] = {"A spread", "B tracing", "C targeting", "D clustering"};

    for (int taskIndex = 0; taskIndex < 4; taskIndex = taskIndex + 1) {
        double baseSeconds = 0.0;
        for (int t = 0; t < 4; t = t + 1) {
            int threads = threadCounts[t];
            omp_set_num_threads(threads);

            long long check = 0;
            auto start = std::chrono::steady_clock::now();

            if (taskIndex == 0) {
                // task A: one full day of the spread, threaded over the people
                std::vector<HealthState> nextState = state;
                #pragma omp parallel for schedule(static)
                for (int node = 0; node < numberOfNodes; node = node + 1) {
                    nextState[node] = nextStateForNode(node, 61, graph, state, parameters);
                }
                for (int node = 0; node < numberOfNodes; node = node + 1) {
                    check = check + static_cast<int>(nextState[node]);
                }
            } else if (taskIndex == 1) {
                TracingResult r = runContactTracingFromInfectious(graph, state, 2);
                check = r.contactsTraced;
            } else if (taskIndex == 2) {
                TargetingResult r = runInterventionTargeting(graph, state, numberOfNodes / 100, 20);
                check = static_cast<long long>(r.targets.size());
                if (!r.targets.empty()) check = check * 1000 + r.targets[0];
            } else {
                ClusteringResult r = runOutbreakClustering(graph, state);
                check = r.clusterCount * 1000000LL + r.largestClusterSize;
            }

            auto end = std::chrono::steady_clock::now();
            double seconds = std::chrono::duration<double>(end - start).count();
            if (threads == 1) baseSeconds = seconds;
            double speedup = (seconds > 0.0) ? baseSeconds / seconds : 0.0;

            std::printf("%-16s %7d %9.4f %8.2fx   %lld\n",
                        taskNames[taskIndex], threads, seconds, speedup, check);
            out << taskNames[taskIndex] << "," << threads << "," << seconds << "," << check << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "wrote results/taskThreads.csv\n";
    std::cout << "the answer check column must be identical down each task's rows,\n";
    std::cout << "which shows the threaded versions give the same result.\n";
    return 0;
}
