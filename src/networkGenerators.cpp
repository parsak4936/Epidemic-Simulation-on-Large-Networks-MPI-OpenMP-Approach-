#include "networkGenerators.h"
#include <random>
#include <fstream>
#include <stdexcept>

// scale free network, Barabasi-Albert style.
// people join one at a time and prefer to connect to already popular people, so a
// few end up as hubs with a huge number of contacts.
// the trick: targetPool holds each person's id once per link they already have, so
// picking a random entry from it automatically favours popular people.
EdgeList generateScaleFreeNetwork(int numberOfNodes,
                                  int edgesPerNewNode,
                                  unsigned int randomSeed) {
    if (edgesPerNewNode < 1) edgesPerNewNode = 1;
    if (numberOfNodes <= edgesPerNewNode) {
        throw std::invalid_argument("scale free network needs more nodes than edgesPerNewNode");
    }

    // same seed always builds the same network, so runs are comparable
    std::mt19937 randomEngine(randomSeed);
    EdgeList edges;
    std::vector<int> targetPool;

    // fully connect the first few people so there is something to attach to
    for (int node = 0; node <= edgesPerNewNode; node++) {
        for (int other = node + 1; other <= edgesPerNewNode; other++) {
            edges.push_back({node, other});
            targetPool.push_back(node);
            targetPool.push_back(other);
        }
    }

    // everyone else joins and picks a few existing people to connect to
    for (int newNode = edgesPerNewNode + 1; newNode < numberOfNodes; newNode++) {
        std::vector<int> chosenTargets;
        while (static_cast<int>(chosenTargets.size()) < edgesPerNewNode) {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(targetPool.size()) - 1);
            int candidate = targetPool[pick(randomEngine)];

            // do not connect to the same person twice
            bool alreadyChosen = false;
            for (int chosen : chosenTargets) {
                if (chosen == candidate) { alreadyChosen = true; break; }
            }
            if (!alreadyChosen) chosenTargets.push_back(candidate);
        }
        for (int target : chosenTargets) {
            edges.push_back({newNode, target});
            targetPool.push_back(newNode);
            targetPool.push_back(target);
        }
    }

    return edges;
}

// small world network, Watts-Strogatz style.
// start from a ring where everyone knows their nearest neighbours, then rewire a
// few links to random people. mostly local contacts plus a few long shortcuts.
EdgeList generateSmallWorldNetwork(int numberOfNodes,
                                   int neighborsPerNode,
                                   double rewireProbability,
                                   unsigned int randomSeed) {
    // half the links go each side, so it has to be even
    if (neighborsPerNode % 2 != 0) neighborsPerNode += 1;

    std::mt19937 randomEngine(randomSeed);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_int_distribution<int> anyNode(0, numberOfNodes - 1);

    // the ring. linking forward only is enough, since the person behind links to me.
    // the modulo wraps around at the end to close the circle.
    EdgeList edges;
    for (int node = 0; node < numberOfNodes; node++) {
        for (int step = 1; step <= neighborsPerNode / 2; step++) {
            int neighbor = (node + step) % numberOfNodes;
            edges.push_back({node, neighbor});
        }
    }

    // the shortcuts. auto& takes a reference so the edge is really changed.
    for (auto& edge : edges) {
        if (chance(randomEngine) < rewireProbability) {
            int newTarget = anyNode(randomEngine);
            if (newTarget != edge.first) edge.second = newTarget;
        }
    }

    return edges;
}

// load a network from a text file, one contact per line as two ids.
// numberOfNodesOut is filled in with the highest id seen plus one.
EdgeList loadEdgeListFromFile(const std::string& fileName, int& numberOfNodesOut) {
    std::ifstream input(fileName);
    if (!input) {
        throw std::runtime_error("could not open edge list file: " + fileName);
    }

    EdgeList edges;
    int highestNodeId = -1;
    int firstNode = 0;
    int secondNode = 0;
    while (input >> firstNode >> secondNode) {
        edges.push_back({firstNode, secondNode});
        if (firstNode > highestNodeId) highestNodeId = firstNode;
        if (secondNode > highestNodeId) highestNodeId = secondNode;
    }

    numberOfNodesOut = highestNodeId + 1;
    return edges;
}
