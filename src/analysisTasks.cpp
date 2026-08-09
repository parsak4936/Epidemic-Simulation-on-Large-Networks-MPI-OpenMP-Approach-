#include "analysisTasks.h"
#include <algorithm>
#include <numeric>
#include <omp.h>

// the three analyses that run alongside the epidemic. they are different
// algorithms, which is what makes this task parallelism, and each one is also
// threaded inside with OpenMP.

// task B, contact tracing. a breadth first search outward from the infected.
// the frontier is split across threads. two threads can reach the same person at
// once, so the claim is atomic: whoever writes the 1 first counts that person.
TracingResult runContactTracing(const Graph& graph,
                                const std::vector<HealthState>& currentState,
                                const std::vector<HealthState>& previousState,
                                int maxDepth) {
    int numberOfNodes = graph.numberOfNodes;

    // newly infected means susceptible yesterday but not today
    std::vector<int> frontier;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (previousState[node] == HealthState::susceptible
            && currentState[node] != HealthState::susceptible) {
            frontier.push_back(node);
        }
    }

    TracingResult result;
    result.newlyInfectedCount = static_cast<int>(frontier.size());
    result.contactsTraced = 0;

    // char not bool, since vector<bool> is packed and not thread safe
    std::vector<char> visited(numberOfNodes, 0);
    for (int node : frontier) visited[node] = 1;

    for (int depth = 0; depth < maxDepth; depth = depth + 1) {
        std::vector<int> nextFrontier;
        int tracedThisRound = 0;

        #pragma omp parallel
        {
            std::vector<int> myFound;   // private to this thread
            int myCount = 0;

            // dynamic scheduling because hubs have far more neighbours than most
            // people, so fixed chunks would leave threads idle
            #pragma omp for schedule(dynamic, 64) nowait
            for (int i = 0; i < static_cast<int>(frontier.size()); i = i + 1) {
                int node = frontier[i];
                int start = graph.neighborStartIndex[node];
                int end = graph.neighborStartIndex[node + 1];
                for (int slot = start; slot < end; slot = slot + 1) {
                    int neighbor = graph.neighborList[slot];

                    // read and write in one unbreakable step, so only one thread
                    // can ever see the 0 and count this person
                    char previouslyVisited;
                    #pragma omp atomic capture
                    { previouslyVisited = visited[neighbor]; visited[neighbor] = 1; }

                    if (previouslyVisited == 0) {
                        myFound.push_back(neighbor);
                        myCount = myCount + 1;
                    }
                }
            }

            // merge once per thread instead of fighting over one shared list
            #pragma omp critical
            {
                nextFrontier.insert(nextFrontier.end(), myFound.begin(), myFound.end());
                tracedThisRound = tracedThisRound + myCount;
            }
        }

        result.contactsTraced = result.contactsTraced + tracedThisRound;
        frontier = nextFrontier;
    }

    return result;
}

// same search but starting from everyone currently infectious. the scheduler uses
// this one, since it only sends workers the current snapshot.
TracingResult runContactTracingFromInfectious(const Graph& graph,
                                              const std::vector<HealthState>& currentState,
                                              int maxDepth) {
    int numberOfNodes = graph.numberOfNodes;
    std::vector<int> frontier;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (currentState[node] == HealthState::infectious) frontier.push_back(node);
    }

    TracingResult result;
    result.newlyInfectedCount = static_cast<int>(frontier.size());
    result.contactsTraced = 0;

    std::vector<char> visited(numberOfNodes, 0);
    for (int node : frontier) visited[node] = 1;

    for (int depth = 0; depth < maxDepth; depth = depth + 1) {
        std::vector<int> nextFrontier;
        int tracedThisRound = 0;

        #pragma omp parallel
        {
            std::vector<int> myFound;
            int myCount = 0;

            #pragma omp for schedule(dynamic, 64) nowait
            for (int i = 0; i < static_cast<int>(frontier.size()); i = i + 1) {
                int node = frontier[i];
                int start = graph.neighborStartIndex[node];
                int end = graph.neighborStartIndex[node + 1];
                for (int slot = start; slot < end; slot = slot + 1) {
                    int neighbor = graph.neighborList[slot];
                    char previouslyVisited;
                    #pragma omp atomic capture
                    { previouslyVisited = visited[neighbor]; visited[neighbor] = 1; }
                    if (previouslyVisited == 0) {
                        myFound.push_back(neighbor);
                        myCount = myCount + 1;
                    }
                }
            }

            #pragma omp critical
            {
                nextFrontier.insert(nextFrontier.end(), myFound.begin(), myFound.end());
                tracedThisRound = tracedThisRound + myCount;
            }
        }

        result.contactsTraced = result.contactsTraced + tracedThisRound;
        frontier = nextFrontier;
    }
    return result;
}

// task C, intervention targeting. a PageRank style iteration that scores how
// central everyone is, then vaccinates the most central susceptible people.
// written as a pull rather than a push: each person gathers from its neighbours,
// so every thread writes only its own slot and there is no race to protect.
TargetingResult runInterventionTargeting(const Graph& graph,
                                         const std::vector<HealthState>& currentState,
                                         int howManyTargets,
                                         int iterations) {
    int numberOfNodes = graph.numberOfNodes;
    const double damping = 0.85;                          // chance of following a contact
    const double base = (1.0 - damping) / numberOfNodes;  // keeps isolated people above zero

    std::vector<double> score(numberOfNodes, 1.0 / numberOfNodes);
    std::vector<double> nextScore(numberOfNodes, 0.0);

    for (int iter = 0; iter < iterations; iter = iter + 1) {
        // static scheduling here, since every person costs about the same
        #pragma omp parallel for schedule(static)
        for (int node = 0; node < numberOfNodes; node = node + 1) {
            double gathered = 0.0;
            int start = graph.neighborStartIndex[node];
            int end = graph.neighborStartIndex[node + 1];
            for (int slot = start; slot < end; slot = slot + 1) {
                int neighbor = graph.neighborList[slot];
                // a neighbour with many contacts splits its influence among them
                int neighborDegree = graph.degreeOf(neighbor);
                if (neighborDegree > 0) {
                    gathered = gathered + score[neighbor] / neighborDegree;
                }
            }
            nextScore[node] = base + damping * gathered;
        }
        score.swap(nextScore);   // swaps pointers, does not copy
    }

    // only susceptible people are worth vaccinating
    std::vector<int> candidates;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (currentState[node] == HealthState::susceptible) candidates.push_back(node);
    }
    std::sort(candidates.begin(), candidates.end(),
              [&score](int a, int b) { return score[a] > score[b]; });

    TargetingResult result;
    int take = std::min(howManyTargets, static_cast<int>(candidates.size()));
    for (int i = 0; i < take; i = i + 1) result.targets.push_back(candidates[i]);
    return result;
}

// walk up to the root of a group, flattening the path on the way so later
// lookups are quicker
static int findRoot(std::vector<int>& parent, int node) {
    while (parent[node] != node) {
        parent[node] = parent[parent[node]];
        node = parent[node];
    }
    return node;
}

// task D, outbreak clustering with union find. groups infected people who are
// connected to each other.
// left sequential on purpose: every merge rewrites the shared parent array, and
// the profile says this is under one percent of the runtime, so there is nothing
// to gain and a wrong parallel version would be much worse.
ClusteringResult runOutbreakClustering(const Graph& graph,
                                       const std::vector<HealthState>& currentState) {
    int numberOfNodes = graph.numberOfNodes;
    std::vector<int> parent(numberOfNodes);
    std::vector<int> groupSize(numberOfNodes, 1);
    std::iota(parent.begin(), parent.end(), 0);   // everyone starts in their own group

    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (currentState[node] != HealthState::infectious) continue;
        int start = graph.neighborStartIndex[node];
        int end = graph.neighborStartIndex[node + 1];
        for (int slot = start; slot < end; slot = slot + 1) {
            int neighbor = graph.neighborList[slot];

            // both ends have to be infectious, so a path through a healthy person
            // does not join two outbreaks
            if (currentState[neighbor] != HealthState::infectious) continue;

            int rootA = findRoot(parent, node);
            int rootB = findRoot(parent, neighbor);
            if (rootA != rootB) {
                // hang the smaller group under the bigger one to keep trees flat
                if (groupSize[rootA] < groupSize[rootB]) std::swap(rootA, rootB);
                parent[rootB] = rootA;
                groupSize[rootA] = groupSize[rootA] + groupSize[rootB];
            }
        }
    }

    // each group has one root, so counting roots counts the clusters
    ClusteringResult result;
    result.clusterCount = 0;
    result.largestClusterSize = 0;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (currentState[node] == HealthState::infectious && findRoot(parent, node) == node) {
            result.clusterCount = result.clusterCount + 1;
            if (groupSize[node] > result.largestClusterSize) {
                result.largestClusterSize = groupSize[node];
            }
        }
    }
    return result;
}
