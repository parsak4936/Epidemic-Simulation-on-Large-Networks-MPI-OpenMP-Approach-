// the contact network, stored in CSR format.
// instead of a list of lists, all neighbours go in one flat array and a second
// array says where each person's slice starts. compact and fast to read.
#pragma once
#include <vector>
#include <utility>

struct Graph {
    int numberOfNodes = 0;

    // neighbours of person i are neighborList[ neighborStartIndex[i] .. neighborStartIndex[i+1] )
    std::vector<int> neighborStartIndex;
    std::vector<int> neighborList;

    // how many neighbours person "node" has
    int degreeOf(int node) const {
        return neighborStartIndex[node + 1] - neighborStartIndex[node];
    }

    // build the graph from a list of edges, each one added in both directions
    static Graph fromEdgeList(int numberOfNodes,
                              const std::vector<std::pair<int, int>>& edgeList);
};
