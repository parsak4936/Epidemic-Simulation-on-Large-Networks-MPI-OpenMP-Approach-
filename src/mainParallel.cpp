// parallel  epidemic simulation.
// two levels of parallelism:
// mpi    splits the people across processes (each process updates its own slice)
// openmp threads the per person work inside each process
// every process builds the same network (same seed) and updates only the slice of
// people it owns. people on the border of a slice have neighbors owned by another
// process, so each day the processes swap the health states of those border people
// the ghost exchange using non blocking sends and receives.

// usage:  mpiexec -n <processes> ./parallelSimulation [numberOfNodes] [networkType] [seed]
#include "graph.h"
#include "networkGenerators.h"
#include "seirModel.h"
#include "measurement.h"

#include <mpi.h>
#include <omp.h>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>
#ifdef USE_METIS
#include <metis.h>
#endif

// decide which process owns each person.
// block cuts the people into equal contiguous ranges, which is fine when contacts are
// mostly local. metis instead picks the split so fewer contacts cross between
// processes, which helps most on scale free networks where hubs straddle boundaries.
// both fill the same owner array, so nothing downstream cares which was used.
static void buildPartition(const Graph& graph, int numberOfProcesses,
                           const std::string& partitioner,
                           std::vector<int>& owner) {
    int numberOfNodes = graph.numberOfNodes;
    owner.assign(numberOfNodes, 0);

    if (partitioner == "metis" && numberOfProcesses > 1) {
#ifdef USE_METIS
        // METIS wants the same CSR arrays we already keep, just in its own integer type
        std::vector<idx_t> xadj(graph.neighborStartIndex.begin(), graph.neighborStartIndex.end());
        std::vector<idx_t> adjncy(graph.neighborList.begin(), graph.neighborList.end());
        std::vector<idx_t> part(numberOfNodes, 0);
        idx_t nodeCount = numberOfNodes;
        idx_t constraints = 1;
        idx_t partCount = numberOfProcesses;
        idx_t edgeCut = 0;

        int status = METIS_PartGraphKway(&nodeCount, &constraints, xadj.data(), adjncy.data(),
                                         nullptr, nullptr, nullptr, &partCount,
                                         nullptr, nullptr, nullptr, &edgeCut, part.data());
        if (status == METIS_OK) {
            for (int node = 0; node < numberOfNodes; node = node + 1) {
                owner[node] = static_cast<int>(part[node]);
            }
            return;
        }
        std::cerr << "warning: METIS failed\n";
#else
        std::cerr << "warning: this build has no METIS\n";
#endif
    }

    // block partition 
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        long long rank = static_cast<long long>(node) * numberOfProcesses / numberOfNodes;
        owner[node] = static_cast<int>(rank);
    }
}


static void exchangeBorderStates(std::vector<HealthState>& state,
                                 const std::vector<std::vector<int>>& sendToRank,
                                 const std::vector<std::vector<int>>& recvFromRank,
                                 int numberOfProcesses) {
    // MPI sends plain blocks of memory, so copy the states into int arrays first
    std::vector<std::vector<int>> sendBuffer(numberOfProcesses);
    std::vector<std::vector<int>> recvBuffer(numberOfProcesses);

    // a receipt for each message still in flight, all waited on together at the end
    std::vector<MPI_Request> pending;

    // post the receives first, so nobody ends up sending while nobody is listening
    for (int other = 0; other < numberOfProcesses; other = other + 1) {
        if (!recvFromRank[other].empty()) {
            recvBuffer[other].resize(recvFromRank[other].size());
            MPI_Request request;
            MPI_Irecv(recvBuffer[other].data(), static_cast<int>(recvBuffer[other].size()),
                      MPI_INT, other, 0, MPI_COMM_WORLD, &request);
            pending.push_back(request);
        }
    }

    for (int other = 0; other < numberOfProcesses; other = other + 1) {
        if (!sendToRank[other].empty()) {
            sendBuffer[other].resize(sendToRank[other].size());
            for (std::size_t i = 0; i < sendToRank[other].size(); i = i + 1) {
                sendBuffer[other][i] = static_cast<int>(state[sendToRank[other][i]]);
            }
            MPI_Request request;
            MPI_Isend(sendBuffer[other].data(), static_cast<int>(sendBuffer[other].size()),
                      MPI_INT, other, 0, MPI_COMM_WORLD, &request);
            pending.push_back(request);
        }
    }

    // nothing has been waited on until now, so all the messages overlapped
    MPI_Waitall(static_cast<int>(pending.size()), pending.data(), MPI_STATUSES_IGNORE);

    // copy the arrived states into the ghosts, the only place we touch people we
    // do not own, and only ever with what their owner just sent
    for (int other = 0; other < numberOfProcesses; other = other + 1) {
        for (std::size_t i = 0; i < recvFromRank[other].size(); i = i + 1) {
            state[recvFromRank[other][i]] = static_cast<HealthState>(recvBuffer[other][i]);
        }
    }
}

// count just my own slice, no communication. we add up all the processes' counts
// once at the very end instead of every single day.
static StateCounts localBlockCounts(const std::vector<HealthState>& state,
                                    const std::vector<int>& myNodes) {
    int local[4] = {0, 0, 0, 0};
    for (int node : myNodes) {
        local[static_cast<int>(state[node])] = local[static_cast<int>(state[node])] + 1;
    }
    StateCounts counts{local[0], local[1], local[2], local[3]};
    return counts;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int myRank = 0;
    int numberOfProcesses = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &myRank);
    MPI_Comm_size(MPI_COMM_WORLD, &numberOfProcesses);

    int numberOfNodes = 100000;
    std::string networkType = "scalefree";
    unsigned int randomSeed = 12345;
    std::string partitioner = "block";   // block or metis
    if (argc >= 2) numberOfNodes = std::stoi(argv[1]);
    if (argc >= 3) networkType = argv[2];
    if (argc >= 4) randomSeed = static_cast<unsigned int>(std::stoul(argv[3]));
    for (int i = 1; i < argc; i = i + 1) {
        std::string option = argv[i];
        if (option == "metis" || option == "block") partitioner = option;
    }

   
    EdgeList edges;
    if (networkType == "smallworld") {
        edges = generateSmallWorldNetwork(numberOfNodes, 6, 0.1, randomSeed);
    } else {
        networkType = "scalefree";
        edges = generateScaleFreeNetwork(numberOfNodes, 3, randomSeed);
    }
    Graph graph = Graph::fromEdgeList(numberOfNodes, edges);

    // slice the people into contiguous blocks, one per process
    std::vector<int> owner;
    buildPartition(graph, numberOfProcesses, partitioner, owner);

    // the people this process is responsible for
    std::vector<int> myNodes;
    for (int node = 0; node < numberOfNodes; node = node + 1) {
        if (owner[node] == myRank) myNodes.push_back(node);
    }

    std::vector<std::vector<int>> sendToRank(numberOfProcesses);
    std::vector<std::vector<int>> recvFromRank(numberOfProcesses);
    for (int node : myNodes) {
        int start = graph.neighborStartIndex[node];
        int end = graph.neighborStartIndex[node + 1];
        for (int slot = start; slot < end; slot = slot + 1) {
            int neighbor = graph.neighborList[slot];
            int neighborOwner = owner[neighbor];
            if (neighborOwner != myRank) {
                recvFromRank[neighborOwner].push_back(neighbor);  // i need this neighbour's state
                sendToRank[neighborOwner].push_back(node);        // that process needs my person too
            }
        }
    }
    // remove duplicates and sort, so both sides agree on the order of the lists
    for (int other = 0; other < numberOfProcesses; other = other + 1) {
        std::sort(sendToRank[other].begin(), sendToRank[other].end());
        sendToRank[other].erase(std::unique(sendToRank[other].begin(), sendToRank[other].end()),
                                sendToRank[other].end());
        std::sort(recvFromRank[other].begin(), recvFromRank[other].end());
        recvFromRank[other].erase(std::unique(recvFromRank[other].begin(), recvFromRank[other].end()),
                                  recvFromRank[other].end());
    }

    // count the people I have to send and the ghosts I keep.
    // this is the concrete size of the communication, it is what the edge cut costs.
    int myBorderPeople = 0;
    int myGhosts = 0;
    for (int other = 0; other < numberOfProcesses; other = other + 1) {
        myBorderPeople = myBorderPeople + static_cast<int>(sendToRank[other].size());
        myGhosts = myGhosts + static_cast<int>(recvFromRank[other].size());
    }
    int totalBorder = 0;
    int totalGhosts = 0;
    MPI_Reduce(&myBorderPeople, &totalBorder, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&myGhosts, &totalGhosts, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    SeirParameters parameters;
    parameters.transmissionProbability = 0.05;
    parameters.incubationProbability = 0.20;
    parameters.recoveryProbability = 0.10;
    parameters.numberOfSteps = 120;
    parameters.initialInfectedCount = 5;
    parameters.randomSeed = randomSeed;

    // starting state, identical on every process because the seeding is deterministic
    std::vector<HealthState> currentState(numberOfNodes, HealthState::susceptible);
    seedInitialInfected(currentState, parameters);

    // each process records only its own slice counts each step (no communication).
    // we combine them into the global epidemic curve once at the end.
    std::vector<StateCounts> localHistory;
    localHistory.push_back(localBlockCounts(currentState, myNodes));

    // time the simulation loop. we use a steady (monotonic) clock, which never
    // runs backward, because the mpi wall clock can jump on this wsl setup.
    // the barrier lines everyone up so we time from the same moment.
    MPI_Barrier(MPI_COMM_WORLD);
    auto startTime = std::chrono::steady_clock::now();

    // a buffer just for my own slice's new states, allocated once. this is the key
    // to speedup: we never copy the whole network each step (that would cost the
    // same no matter how many processes there are), only my own share of it.
    std::vector<HealthState> newLocalStates(myNodes.size());
    int myNodeCount = static_cast<int>(myNodes.size());

    // we time the two parts separately: the compute (updating people) and the
    // communication (swapping border states). their split tells us the overhead.
    double computeAccum = 0.0;
    double commAccum = 0.0;

    // the main loop.
    //   1. COMPUTE      update my own people 
    //   2. COMMUNICATE  swap border states with the neighbouring processes
    // The ratio between those two is the "communication overhead" reported at the
    // end, and it is what explains why adding more processes eventually stops
    for (int step = 0; step < parameters.numberOfSteps; step = step + 1) {
        //compute part
        auto computeStart = std::chrono::steady_clock::now();
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < myNodeCount; i = i + 1) {
            newLocalStates[i] = nextStateForNode(myNodes[i], step, graph, currentState, parameters);
        }

        // Only now, once every decision for the day has been made, do we write them
        // back. Same reason as the sequential version: deciding from a frozen
        // snapshot is what stops an infection made today from spreading again today.
        for (int i = 0; i < myNodeCount; i = i + 1) {
            currentState[myNodes[i]] = newLocalStates[i];
        }
        auto computeEnd = std::chrono::steady_clock::now();
        computeAccum = computeAccum + std::chrono::duration<double>(computeEnd - computeStart).count();

        // communication 
        auto commStart = std::chrono::steady_clock::now();
        // swap border people's new states so neighbors on other processes are fresh
        exchangeBorderStates(currentState, sendToRank, recvFromRank, numberOfProcesses);
        auto commEnd = std::chrono::steady_clock::now();
        commAccum = commAccum + std::chrono::duration<double>(commEnd - commStart).count();

        localHistory.push_back(localBlockCounts(currentState, myNodes));
    }

    auto endTime = std::chrono::steady_clock::now();
    double localElapsed = std::chrono::duration<double>(endTime - startTime).count();
    // the run is only as fast as its slowest process, so take the maximum
    double slowestElapsed = 0.0;
    MPI_Reduce(&localElapsed, &slowestElapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // compute-vs-communication split, and how uneven the compute was across processes
    double maxCompute = 0.0;
    double sumCompute = 0.0;
    double maxComm = 0.0;
    MPI_Reduce(&computeAccum, &maxCompute, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&computeAccum, &sumCompute, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&commAccum, &maxComm, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Aggregation: one reduction instead of 120

    int rows = static_cast<int>(localHistory.size());
    std::vector<int> localFlat(rows * 4);
    for (int i = 0; i < rows; i = i + 1) {
        localFlat[i * 4 + 0] = localHistory[i].susceptible;
        localFlat[i * 4 + 1] = localHistory[i].exposed;
        localFlat[i * 4 + 2] = localHistory[i].infectious;
        localFlat[i * 4 + 3] = localHistory[i].recovered;
    }
    std::vector<int> globalFlat(rows * 4, 0);
    MPI_Reduce(localFlat.data(), globalFlat.data(), rows * 4, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    std::vector<StateCounts> history;
    if (myRank == 0) {
        for (int i = 0; i < rows; i = i + 1) {
            StateCounts counts{globalFlat[i * 4 + 0], globalFlat[i * 4 + 1],
                               globalFlat[i * 4 + 2], globalFlat[i * 4 + 3]};
            history.push_back(counts);
        }
    }

    
    int threadsPerProcess = 1;
    #pragma omp parallel
    {
        #pragma omp single
        threadsPerProcess = omp_get_num_threads();
    }

    // only process 0 writes the results and prints the report
    if (myRank == 0) {
        std::filesystem::create_directories("results");
        writeCountsToCsv(history, "results/epidemicCurve.csv");

        StateCounts last = history.back();
        std::cout << "network type: " << networkType
                  << " | nodes: " << numberOfNodes
                  << " | edges: " << edges.size() << "\n";
        std::cout << "finish, recovered: " << last.recovered
                  << ", still susceptible: " << last.susceptible << "\n";
        std::cout << "simulation time: " << slowestElapsed << " s"
                  << "  |  mode: parallel"
                  << "  |  processes: " << numberOfProcesses
                  << "  |  threads each: " << threadsPerProcess
                  << "  |  cores on machine: " << hardwareCoreCount()
                  << "  |  machine: " << machineName() << "\n";

        // load imbalance: the slowest process's compute divided by the average
        double averageCompute = sumCompute / numberOfProcesses;
        double imbalance = (averageCompute > 0.0) ? (maxCompute / averageCompute) : 1.0;
        // communication overhead: how much of the work was just swapping states
        double commOverhead = (maxCompute + maxComm > 0.0) ? (maxComm / (maxCompute + maxComm)) : 0.0;

        std::cout << "halo: " << totalGhosts << " ghost copies across all processes ("
                  << (100.0 * totalGhosts / numberOfNodes) << "% of the people), "
                  << (totalGhosts * sizeof(int) / 1024) << " KB exchanged per day\n";
        std::cout << "compute: " << maxCompute << " s  |  communication: " << maxComm
                  << " s (" << (commOverhead * 100.0) << "% of the work)"
                  << "  |  load imbalance: " << imbalance << " (1.0 is perfect)\n";

        RunMeasurement measurement;
        measurement.label = (partitioner == "metis") ? "mpi+openmp+metis" : "mpi+openmp";
        measurement.processes = numberOfProcesses;
        measurement.threads = threadsPerProcess;
        measurement.numberOfNodes = numberOfNodes;
        measurement.networkType = networkType;
        measurement.seconds = slowestElapsed;
        measurement.computeSeconds = maxCompute;
        measurement.commSeconds = maxComm;
        measurement.imbalance = imbalance;
        appendTimingRow("results/timings.csv", measurement);
        std::cout << "recorded this run in results/timings.csv\n";
    }

    MPI_Finalize();
    return 0;
}
