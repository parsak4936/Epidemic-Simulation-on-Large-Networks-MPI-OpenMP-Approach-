// phase 1 driver: build a contact network, run the sequential seir simulation,
// and save the epidemic curve to a csv file we can plot later.
//
// usage:  ./sequentialSimulation [numberOfNodes] [networkType] [randomSeed]
//   networkType is either "scalefree" (default) or "smallworld"
#include "graph.h"
#include "networkGenerators.h"
#include "seirModel.h"
#include "measurement.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>

int main(int argc, char** argv) {
    // defaults, overridable from the command line
    int numberOfNodes = 10000;
    std::string networkType = "scalefree";
    unsigned int randomSeed = 12345;

    if (argc >= 2) numberOfNodes = std::stoi(argv[1]);
    if (argc >= 3) networkType = argv[2];
    if (argc >= 4) randomSeed = static_cast<unsigned int>(std::stoul(argv[3]));

    // 1. build the contact network
    EdgeList edges;
    if (networkType == "smallworld") {
        edges = generateSmallWorldNetwork(numberOfNodes, 6, 0.1, randomSeed);
    } else {
        networkType = "scalefree";
        edges = generateScaleFreeNetwork(numberOfNodes, 3, randomSeed);
    }
    Graph graph = Graph::fromEdgeList(numberOfNodes, edges);

    std::cout << "network type: " << networkType
              << " | nodes: " << graph.numberOfNodes
              << " | edges: " << edges.size() << "\n";

    // 2. pick the epidemic parameters
    SeirParameters parameters;
    parameters.transmissionProbability = 0.05;
    parameters.incubationProbability = 0.20;
    parameters.recoveryProbability = 0.10;
    parameters.numberOfSteps = 120;
    parameters.initialInfectedCount = 5;
    parameters.randomSeed = randomSeed;

    // 3. run the simulation, timing just the simulation part with a wall clock
    auto startTime = std::chrono::steady_clock::now();
    std::vector<StateCounts> history = runSeirSimulation(graph, parameters);
    auto endTime = std::chrono::steady_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    // 4. save the curve and print a short summary
    std::filesystem::create_directories("results");
    std::string outputFile = "results/epidemicCurve.csv";
    writeCountsToCsv(history, outputFile);

    StateCounts firstStep = history.front();
    StateCounts lastStep = history.back();
    std::cout << "start  -> infectious: " << firstStep.infectious
              << ", susceptible: " << firstStep.susceptible << "\n";
    std::cout << "finish -> recovered: " << lastStep.recovered
              << ", still susceptible: " << lastStep.susceptible << "\n";
    std::cout << "wrote " << outputFile << " (" << history.size() << " rows)\n";

    // 5. report and record the timing + hardware, so we can compare runs later
    std::cout << "simulation time: " << elapsedSeconds << " s"
              << "  |  mode: sequential (1 process, 1 thread)"
              << "  |  cores on machine: " << hardwareCoreCount()
              << "  |  machine: " << machineName() << "\n";

    RunMeasurement measurement;
    measurement.label = "sequential";
    measurement.processes = 1;
    measurement.threads = 1;
    measurement.numberOfNodes = graph.numberOfNodes;
    measurement.networkType = networkType;
    measurement.seconds = elapsedSeconds;
    measurement.computeSeconds = elapsedSeconds;  // all of it is compute
    measurement.commSeconds = 0.0;                // one process, nothing to exchange
    measurement.imbalance = 1.0;                  // one process, perfectly balanced
    appendTimingRow("results/timings.csv", measurement);
    std::cout << "recorded this run in results/timings.csv\n";
    return 0;
}
