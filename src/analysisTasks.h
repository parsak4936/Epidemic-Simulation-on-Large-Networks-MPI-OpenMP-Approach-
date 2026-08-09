// the three analysis tasks that run alongside the spread dynamics (task A).
// each is a DIFFERENT algorithm, which is the whole point of the task parallelism:
//   task B, contact tracing      = a graph traversal (breadth first search)
//   task C, intervention target  = an iterative centrality score (pagerank style)
//   task D, outbreak clustering  = union find connected components
#pragma once
#include "graph.h"
#include "seirModel.h"
#include <vector>

// task B result: how many people were newly infected, and how many contacts we
// reached by walking back through the graph from them.
struct TracingResult {
    int newlyInfectedCount;
    int contactsTraced;
};

// walk backwards from the people infected this step, up to maxDepth hops, to find
// the contacts who might have exposed them.
TracingResult runContactTracing(const Graph& graph,
                                const std::vector<HealthState>& currentState,
                                const std::vector<HealthState>& previousState,
                                int maxDepth);

// same idea, but starting from everyone currently infectious. the scheduler uses
// this one, because it only sends workers the current snapshot (not the previous).
TracingResult runContactTracingFromInfectious(const Graph& graph,
                                              const std::vector<HealthState>& currentState,
                                              int maxDepth);

// task C result: the people chosen to vaccinate or quarantine.
struct TargetingResult {
    std::vector<int> targets;
};

// score everyone by how central they are in the contact graph (a pagerank style
// power iteration) and return the most central people who are still susceptible,
// as the best candidates to vaccinate.
TargetingResult runInterventionTargeting(const Graph& graph,
                                         const std::vector<HealthState>& currentState,
                                         int howManyTargets,
                                         int iterations);

// task D result: how many separate clusters of infection there are, and how big
// the largest one is.
struct ClusteringResult {
    int clusterCount;
    int largestClusterSize;
};

// group the currently infectious people into connected clusters using union find.
ClusteringResult runOutbreakClustering(const Graph& graph,
                                       const std::vector<HealthState>& currentState);
