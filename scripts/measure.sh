#!/usr/bin/env bash
# every measurement in the report except the main process/thread grid, which is runGrid.sh.
# pick one with the first argument:
#
#   bash scripts/measure.sh halo         [people] [seed]              ghost copies per partitioner
#   bash scripts/measure.sh metis        [people] [network] [seed]    block split against METIS
#   bash scripts/measure.sh weak                                      weak scaling
#   bash scripts/measure.sh intervention [people] [network] [seed]    what task C achieves
#   bash scripts/measure.sh memory                                    memory and time as N grows
#   bash scripts/measure.sh profile                                   gprof on the sequential build
#   bash scripts/measure.sh degrees      [people] [seed]              degree distribution, both shapes
#
# everything writes into results/ and prints a summary.
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")" || exit 1
mkdir -p results

WHAT="${1:-}"
shift 2>/dev/null

case "$WHAT" in

halo)
  NODES="${1:-500000}"; SEED="${2:-777}"
  echo "ghost copies produced by each partitioning, $NODES people, seed $SEED."
  echo "this depends only on the graph and the split, not on the machine."
  echo ""
  for TYPE in smallworld scalefree; do
    for PART in block metis; do
      for P in 2 4 8; do
        line=$(OMP_NUM_THREADS=1 mpiexec -n "$P" ./build/parallelSimulation \
               "$NODES" "$TYPE" "$SEED" "$PART" 2>/dev/null | grep -o "halo:.*day")
        printf "%-11s %-6s %2s procs : %s\n" "$TYPE" "$PART" "$P" "$line"
      done
    done
  done
  ;;

metis)
  NODES="${1:-500000}"; TYPE="${2:-scalefree}"; SEED="${3:-777}"
  echo "network: $TYPE, people: $NODES"
  echo ""
  printf "%-8s %-7s %10s %12s %12s\n" "procs" "part" "seconds" "comm%" "imbalance"
  for P in 2 4 8; do
    for PART in block metis; do
      line=$(OMP_NUM_THREADS=1 mpiexec -n "$P" ./build/parallelSimulation \
             "$NODES" "$TYPE" "$SEED" "$PART" 2>/dev/null | grep "compute:")
      comm=$(echo "$line" | sed -n 's/.*(\([0-9.]*\)% of the work).*/\1/p')
      imb=$(echo "$line"  | sed -n 's/.*load imbalance: \([0-9.]*\).*/\1/p')
      tot=$(echo "$line"  | sed -n 's/.*compute: \([0-9.]*\) s.*/\1/p')
      printf "%-8s %-7s %10s %11s%% %12s\n" "$P" "$PART" "$tot" "$comm" "$imb"
    done
  done
  ;;

weak)
  BASE=200000; TYPE=smallworld; SEED=777; REPEATS=3
  cp results/timings.csv /tmp/strongTimings.csv 2>/dev/null
  echo "workers,nodes,seconds" > results/weakTimings.csv
  for W in 1 2 4; do
    N=$((W * BASE))
    bestT=999999
    for r in $(seq 1 $REPEATS); do
      if [ "$W" -eq 1 ]; then
        out=$(./build/sequentialSimulation $N $TYPE $SEED)
      else
        out=$(OMP_NUM_THREADS=1 mpiexec -n $W ./build/parallelSimulation $N $TYPE $SEED)
      fi
      t=$(echo "$out" | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
      if [ "$(awk -v a="$t" -v b="$bestT" 'BEGIN{print (a<b)?1:0}')" -eq 1 ]; then bestT=$t; fi
    done
    echo "$W,$N,$bestT" >> results/weakTimings.csv
    echo "  weak: $W workers, $N people, best ${bestT}s"
  done
  cp /tmp/strongTimings.csv results/timings.csv 2>/dev/null
  cat results/weakTimings.csv
  ;;

intervention)
  NODES="${1:-200000}"; TYPE="${2:-scalefree}"; SEED="${3:-777}"
  echo "network: $TYPE, people: $NODES, seed: $SEED"
  echo ""
  ./build/sequentialSimulation "$NODES" "$TYPE" "$SEED" > /dev/null 2>&1
  caughtWithout=$(tail -1 results/epidemicCurve.csv | cut -d, -f5)
  withOut=$(./build/responseSimulation "$NODES" "$TYPE" "$SEED" 2>/dev/null)
  vaccinated=$(echo "$withOut" | sed -n 's/.*total vaccinated by task C: \([0-9]*\).*/\1/p')
  endR=$(tail -1 results/responseLog.csv | cut -d, -f5)
  caughtWith=$((endR - vaccinated))
  echo "without interventions: $caughtWithout people caught the disease"
  echo "with interventions:    $caughtWith caught it, and $vaccinated were vaccinated"
  echo "$caughtWithout,$caughtWith,$vaccinated" > results/interventionEffect.csv
  ;;

memory)
  echo "people,peakMemoryKB,seconds" > results/memoryProfile.csv
  for N in 125000 250000 500000 1000000 2000000; do
    line=$(/usr/bin/time -v ./build/sequentialSimulation "$N" smallworld 777 2>&1)
    kb=$(echo "$line"  | sed -n 's/.*Maximum resident set size (kbytes): \([0-9]*\).*/\1/p')
    t=$(echo "$line"   | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
    echo "$N,$kb,$t" >> results/memoryProfile.csv
    awk -v n="$N" -v k="$kb" -v s="$t" 'BEGIN{
      printf "  %8d people : %7.1f MB, %6s s, %.1f bytes per person\n", n, k/1024, s, k*1024/n }'
  done
  ;;

profile)
  echo "[1/3] compiling a profiling build (with -pg) ..."
  g++ -std=c++17 -O2 -pg src/mainSequential.cpp src/graph.cpp \
      src/networkGenerators.cpp src/seirModel.cpp src/measurement.cpp \
      -o build/sequentialProfiled
  echo "[2/3] running it big enough that gprof gets enough samples ..."
  ./build/sequentialProfiled 500000 smallworld 777 > /dev/null
  echo "[3/3] reading the profile ..."
  gprof ./build/sequentialProfiled gmon.out > results/gprofReport.txt
  rm -f gmon.out
  echo ""
  echo "=== flat profile, hottest first ==="
  sed -n '1,12p' results/gprofReport.txt
  ;;

degrees)
  NODES="${1:-500000}"; SEED="${2:-777}"
  ./build/degreeStats "$NODES" scalefree "$SEED"
  echo ""
  ./build/degreeStats "$NODES" smallworld "$SEED"
  ;;

*)
  sed -n '2,15p' "$0"
  exit 1
  ;;
esac
