#include "graph.h"

// three passes so nothing has to grow while we fill it:
// count how many neighbours each person has, turn those counts into start
// positions, then drop the neighbours into place.
Graph Graph::fromEdgeList(int numberOfNodes,
                          const std::vector<std::pair<int, int>>& edgeList) {
    Graph graph;
    graph.numberOfNodes = numberOfNodes;

    //count. an edge (a,b) gives a neighbour to both a and b.
    std::vector<int> degreeCount(numberOfNodes, 0);
    for (const auto& edge : edgeList) {
        degreeCount[edge.first] += 1;
        degreeCount[edge.second] += 1;
    }

    // running total of the counts gives each person's start position
    graph.neighborStartIndex.assign(numberOfNodes + 1, 0);
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        graph.neighborStartIndex[node + 1] =
            graph.neighborStartIndex[node] + degreeCount[node];
    }

    //  fill in the neighbours. writeCursor tracks the next free slot per person.
    int totalSlots = graph.neighborStartIndex[numberOfNodes];
    graph.neighborList.assign(totalSlots, 0);

    std::vector<int> writeCursor = graph.neighborStartIndex;
    for (const auto& edge : edgeList) {
        int firstNode = edge.first;
        int secondNode = edge.second;

        graph.neighborList[writeCursor[firstNode]] = secondNode;
        writeCursor[firstNode] += 1;

        graph.neighborList[writeCursor[secondNode]] = firstNode;
        writeCursor[secondNode] += 1;
    }

    return graph;
}
