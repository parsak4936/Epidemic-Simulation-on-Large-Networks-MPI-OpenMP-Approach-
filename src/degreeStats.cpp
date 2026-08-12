// measures the shape of the contact network itself, rather than the epidemic on it.
//
// why this is worth its own little program: the inner loop of the sequential run costs
// O(degree) for each person, so how the degrees are spread out decides both how uneven
// the work is and how badly a naive partition cuts the network. a small world graph gives
// nearly every person the same number of contacts. a scale free graph does not: a handful
// of hubs hold a large share of all the contacts, and those hubs are what make the parallel
// version work harder.
//
// usage:  ./degreeStats [numberOfNodes] [networkType] [randomSeed]
//   writes results/degrees_<networkType>_<numberOfNodes>.csv  (degree, howManyPeople)
#include "graph.h"
#include "networkGenerators.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

int main(int argc, char** argv) {
    int numberOfNodes = 500000;
    std::string networkType = "scalefree";
    unsigned int randomSeed = 777;

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

    // collect every person's degree, then count how many people have each degree
    std::vector<int> degrees(numberOfNodes);
    int highest = 0;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        degrees[node] = graph.degreeOf(node);
        if (degrees[node] > highest) highest = degrees[node];
    }

    std::vector<long long> howMany(highest + 1, 0);
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        howMany[degrees[node]] += 1;
    }

    // the summary numbers that go in the report
    std::vector<int> sorted = degrees;
    std::sort(sorted.begin(), sorted.end());
    long long totalDegree = 0;
    for (int d : degrees) totalDegree += d;

    double mean = static_cast<double>(totalDegree) / numberOfNodes;
    int median = sorted[numberOfNodes / 2];
    int p99 = sorted[static_cast<size_t>(numberOfNodes * 0.99)];
    int maximum = sorted.back();

    // what share of all the contacts belongs to the busiest one percent of people?
    long long topOnePercentDegree = 0;
    for (size_t i = static_cast<size_t>(numberOfNodes * 0.99); i < sorted.size(); i = i + 1) {
        topOnePercentDegree += sorted[i];
    }
    double hubShare = 100.0 * topOnePercentDegree / totalDegree;

    std::cout << "network: " << networkType << ", people: " << numberOfNodes << "\n";
    std::cout << "  mean degree      " << mean << "\n";
    std::cout << "  median degree    " << median << "\n";
    std::cout << "  99th percentile  " << p99 << "\n";
    std::cout << "  highest degree   " << maximum << "\n";
    std::cout << "  busiest 1% hold  " << hubShare << "% of all contacts\n";
    std::cout << "  max / median     " << (median > 0 ? static_cast<double>(maximum) / median : 0.0) << "\n";

    std::filesystem::create_directories("results");
    std::string outName = "results/degrees_" + networkType + "_"
                        + std::to_string(numberOfNodes) + ".csv";
    std::ofstream out(outName);
    out << "degree,howManyPeople\n";
    for (int d = 0; d <= highest; d = d + 1) {
        if (howMany[d] > 0) out << d << "," << howMany[d] << "\n";
    }
    out.close();
    std::cout << "wrote " << outName << "\n";
    return 0;
}
