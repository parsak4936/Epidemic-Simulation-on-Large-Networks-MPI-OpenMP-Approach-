#!/usr/bin/env bash
# profile the sequential baseline with gprof to find the hottest functions.
# gprof needs a special build: compile and link with -pg. then you run the program
# (it writes a file called gmon.out), then gprof reads that file.
# run from the project root:  bash scripts/profile.sh
export PATH="$HOME/opt/mpich/bin:$PATH"

echo "[1/3] compiling a profiling build (with -pg) ..."
g++ -std=c++17 -O2 -pg src/mainSequential.cpp src/graph.cpp \
    src/networkGenerators.cpp src/seirModel.cpp src/measurement.cpp \
    -o build/sequentialProfiled

echo "[2/3] running it on a big-ish network so gprof gets enough samples ..."
./build/sequentialProfiled 500000 smallworld 777 > /dev/null

echo "[3/3] reading the profile with gprof ..."
gprof ./build/sequentialProfiled gmon.out > results/gprofReport.txt
rm -f gmon.out
echo "wrote results/gprofReport.txt"
echo ""
echo "=== flat profile: where the time went (hottest first) ==="
sed -n '1,12p' results/gprofReport.txt
