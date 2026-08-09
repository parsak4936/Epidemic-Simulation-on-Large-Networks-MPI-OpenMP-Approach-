// two ways to build a synthetic contact network, plus a loader for real data.
#pragma once
#include <vector>
#include <utility>
#include <string>

// an edge list is just a bunch of (personA, personB) contact pairs
using EdgeList = std::vector<std::pair<int, int>>;

// scale free network (barabasi-albert model): a few people end up with a huge
// number of contacts while most have only a few. new people prefer to connect
// to already popular people. this is what real social contact often looks like.
EdgeList generateScaleFreeNetwork(int numberOfNodes,
                                  int edgesPerNewNode,
                                  unsigned int randomSeed);

// small world network (watts-strogatz model): everyone is linked to their close
// neighbors on a ring, then a few links are rewired to random far away people.
// gives short paths between anyone (the "six degrees" effect).
EdgeList generateSmallWorldNetwork(int numberOfNodes,
                                   int neighborsPerNode,
                                   double rewireProbability,
                                   unsigned int randomSeed);

// load an undirected edge list from a text file whose lines are "personA personB".
// numberOfNodesOut is filled in with (highest id + 1).
EdgeList loadEdgeListFromFile(const std::string& fileName, int& numberOfNodesOut);
