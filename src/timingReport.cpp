// reads results/timings.csv and prints a comparison table in the console.
// if a configuration was run several times, we keep its best (smallest) time, so
// the table is not thrown off by a single noisy run.
//   speedup    = sequential time divided by parallel time (higher is better)
//   efficiency = speedup divided by the number of workers (1.0 would be perfect)
// usage:  ./timingReport [results/timings.csv]
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

struct Config {
    std::string label;
    int processes;
    int threads;
    int numberOfNodes;
    std::string networkType;
    double bestSeconds;
    double commSeconds;
    double imbalance;
};

int main(int argc, char** argv) {
    std::string fileName = (argc >= 2) ? argv[1] : "results/timings.csv";
    std::ifstream input(fileName);
    if (!input) {
        std::cout << "could not open " << fileName << ". run a simulation first.\n";
        return 1;
    }

    // read every row, and keep only the best (smallest) time per configuration
    std::vector<Config> configs;
    std::string line;
    std::getline(input, line);   // skip the header
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::stringstream parts(line);
        std::string field, label, networkType, machine;
        int processes, threads, numberOfNodes, cores;
        double seconds;
        std::getline(parts, label, ',');
        std::getline(parts, field, ','); processes = std::stoi(field);
        std::getline(parts, field, ','); threads = std::stoi(field);
        std::getline(parts, field, ','); numberOfNodes = std::stoi(field);
        std::getline(parts, networkType, ',');
        std::getline(parts, field, ','); cores = std::stoi(field);
        std::getline(parts, machine, ',');
        std::getline(parts, field, ','); seconds = std::stod(field);
        double commSeconds = 0.0;
        double imbalance = 1.0;
        if (std::getline(parts, field, ',')) { /* computeSeconds, not needed here */ }
        if (std::getline(parts, field, ',')) commSeconds = std::stod(field);
        if (std::getline(parts, field, ',')) imbalance = std::stod(field);
        (void)cores; (void)machine;

        bool found = false;
        for (Config& config : configs) {
            if (config.label == label && config.processes == processes
                && config.threads == threads && config.numberOfNodes == numberOfNodes
                && config.networkType == networkType) {
                if (seconds < config.bestSeconds) {
                    config.bestSeconds = seconds;
                    config.commSeconds = commSeconds;
                    config.imbalance = imbalance;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            configs.push_back(Config{label, processes, threads, numberOfNodes,
                                     networkType, seconds, commSeconds, imbalance});
        }
    }

    // the distinct network sizes, in the order they first appear
    std::vector<std::pair<int, std::string>> groups;
    for (const Config& config : configs) {
        std::pair<int, std::string> key(config.numberOfNodes, config.networkType);
        if (std::find(groups.begin(), groups.end(), key) == groups.end()) {
            groups.push_back(key);
        }
    }

    std::cout << "\n=== timing comparison (best of the runs in " << fileName << ") ===\n";
    for (const auto& group : groups) {
        int nodes = group.first;
        std::string type = group.second;

        // sequential baseline for this size (one process, one thread)
        double baseline = -1.0;
        for (const Config& config : configs) {
            if (config.numberOfNodes == nodes && config.networkType == type
                && config.processes == 1 && config.threads == 1) {
                if (baseline < 0.0 || config.bestSeconds < baseline) baseline = config.bestSeconds;
            }
        }

        std::cout << "\nnetwork: " << type << ", people: " << nodes << "\n";
        std::printf("  %-12s %9s %8s %11s %8s %11s %9s %10s\n",
                    "mode", "procs", "threads", "seconds", "speedup", "efficiency",
                    "comm%", "imbalance");
        for (const Config& config : configs) {
            if (config.numberOfNodes != nodes || config.networkType != type) continue;
            int workers = config.processes * config.threads;
            double commPercent = (config.bestSeconds > 0.0) ? (config.commSeconds / config.bestSeconds * 100.0) : 0.0;
            if (baseline > 0.0 && config.bestSeconds > 0.0) {
                double speedup = baseline / config.bestSeconds;
                double efficiency = speedup / workers;
                std::printf("  %-12s %9d %8d %11.5f %7.2fx %10.0f%% %8.0f%% %10.2f\n",
                            config.label.c_str(), config.processes, config.threads,
                            config.bestSeconds, speedup, efficiency * 100.0,
                            commPercent, config.imbalance);
            } else {
                std::printf("  %-12s %9d %8d %11.5f %8s %11s %8.0f%% %10.2f\n",
                            config.label.c_str(), config.processes, config.threads,
                            config.bestSeconds, "-", "-", commPercent, config.imbalance);
            }
        }
    }
    std::cout << "\n";
    return 0;
}
