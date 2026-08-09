#include "measurement.h"

#include <thread>
#include <fstream>
#include <filesystem>
#include <unistd.h>   // gethostname

int hardwareCoreCount() {
    unsigned int cores = std::thread::hardware_concurrency();
    return cores == 0 ? 1 : static_cast<int>(cores);
}

std::string machineName() {
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    return "unknown";
}

void appendTimingRow(const std::string& fileName, const RunMeasurement& measurement) {
    // did the file already exist (and have content)? if not we add a header row.
    bool needHeader = true;
    std::error_code errorCode;
    if (std::filesystem::exists(fileName, errorCode) &&
        std::filesystem::file_size(fileName, errorCode) > 0) {
        needHeader = false;
    }

    std::ofstream output(fileName, std::ios::app);   // app = append, do not overwrite
    if (!output) return;                             // if we cannot write, just skip

    if (needHeader) {
        output << "label,processes,threads,numberOfNodes,networkType,cores,machine,"
               << "seconds,computeSeconds,commSeconds,imbalance\n";
    }
    output << measurement.label << ","
           << measurement.processes << ","
           << measurement.threads << ","
           << measurement.numberOfNodes << ","
           << measurement.networkType << ","
           << hardwareCoreCount() << ","
           << machineName() << ","
           << measurement.seconds << ","
           << measurement.computeSeconds << ","
           << measurement.commSeconds << ","
           << measurement.imbalance << "\n";
}
