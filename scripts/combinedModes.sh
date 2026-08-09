#!/usr/bin/env bash
# The hybrid question: with a FIXED total number of workers, does it matter how you
# split them between MPI processes and OpenMP threads? On a cluster you would normally
# put one process per node and use threads inside the node, so this is the split that
# actually gets deployed. Here we hold processes x threads constant and vary the shape.
# usage:  bash scripts/combinedModes.sh [nodes] [network] [seed] [label]
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"

NODES="${1:-1000000}"
TYPE="${2:-smallworld}"
SEED="${3:-777}"
LABEL="${4:-local}"
OUT="results/$LABEL"
mkdir -p "$OUT"

# each line is "processes threads"; the pairs are grouped by total workers
CONFIGS="1x2 2x1 1x4 2x2 4x1 1x8 2x4 4x2 8x1"

echo "network: $TYPE, people: $NODES, machine: $LABEL"
echo ""
printf "%8s %8s %9s %11s %10s %9s\n" "workers" "procs" "threads" "seconds" "speedup" "comm%"

# sequential baseline first
seqLine=$(./build/sequentialSimulation "$NODES" "$TYPE" "$SEED" 2>/dev/null | grep "simulation time")
baseT=$(echo "$seqLine" | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
printf "%8s %8s %9s %11s %10s %9s\n" "1" "1" "1" "$baseT" "1.00" "0"
echo "sequential,1,1,$baseT,1.00,0" > "$OUT/combinedModes.csv"

for cfg in $CONFIGS; do
  P="${cfg%x*}"
  T="${cfg#*x}"
  W=$((P * T))
  best=999999
  bestComm=0
  for r in 1 2 3; do
    line=$(OMP_NUM_THREADS="$T" mpiexec -n "$P" ./build/parallelSimulation "$NODES" "$TYPE" "$SEED" 2>/dev/null | grep "simulation time")
    t=$(echo "$line" | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
    c=$(OMP_NUM_THREADS="$T" mpiexec -n "$P" ./build/parallelSimulation "$NODES" "$TYPE" "$SEED" 2>/dev/null | grep "compute:" | sed -n 's/.*(\([0-9.]*\)% of the work).*/\1/p')
    if [ -n "$t" ]; then
      lower=$(awk -v a="$t" -v b="$best" 'BEGIN{print (a<b)?1:0}')
      if [ "$lower" = "1" ]; then best="$t"; bestComm="$c"; fi
    fi
  done
  sp=$(awk -v b="$baseT" -v p="$best" 'BEGIN{printf "%.2f", b/p}')
  cm=$(awk -v c="$bestComm" 'BEGIN{printf "%.0f", c}')
  printf "%8s %8s %9s %11s %10s %8s%%\n" "$W" "$P" "$T" "$best" "$sp" "$cm"
  echo "hybrid,$P,$T,$best,$sp,$cm" >> "$OUT/combinedModes.csv"
done

echo ""
echo "wrote $OUT/combinedModes.csv"
