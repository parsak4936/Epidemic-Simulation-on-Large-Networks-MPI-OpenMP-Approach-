// small helpers to measure how long a run took and record it, together with
// what hardware it ran on, so we can compare sequential vs parallel runs later.
#pragma once
#include <string>

struct RunMeasurement {
    std::string label;       // e.g. "sequential" or "mpi+openmp"
    int processes;           // number of mpi processes used (1 for sequential)
    int threads;             // openmp threads per process (1 for sequential)
    int numberOfNodes;
    std::string networkType;
    double seconds;          // wall clock time of the simulation part only
    double computeSeconds;   // time spent updating people (the slowest process)
    double commSeconds;      // time spent exchanging border states (the slowest process)
    double imbalance;        // slowest process compute time / average compute time (1.0 is perfect)
};

// how many cpu cores this machine reports
int hardwareCoreCount();

// short machine name (hostname)
std::string machineName();

// append one measurement as a row to a csv file, writing the header first
// time if the file does not exist yet. lets every run pile up in one place.
void appendTimingRow(const std::string& fileName, const RunMeasurement& measurement);
