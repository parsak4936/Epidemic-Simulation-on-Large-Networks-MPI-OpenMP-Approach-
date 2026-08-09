#!/usr/bin/env bash
# Compares the block partition with the METIS partition on the same network, showing
# what METIS buys: less communication and a more even split, especially on scale free
# networks where hubs otherwise straddle the boundaries.
# usage:  bash scripts/comparePartitioners.sh [nodes] [network] [seed]
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"

NODES="${1:-500000}"
TYPE="${2:-scalefree}"
SEED="${3:-777}"

echo "network: $TYPE, people: $NODES"
echo ""
printf "%-8s %-7s %10s %12s %12s\n" "procs" "part" "seconds" "comm%" "imbalance"

for P in 2 4 8; do
  for PART in block metis; do
    line=$(OMP_NUM_THREADS=1 mpiexec -n "$P" ./build/parallelSimulation "$NODES" "$TYPE" "$SEED" "$PART" 2>/dev/null | grep "compute:")
    secs=$(echo "$line" | sed -n 's/.*communication: \([0-9.]*\) s.*/\1/p')
    comm=$(echo "$line" | sed -n 's/.*(\([0-9.]*\)% of the work).*/\1/p')
    imb=$(echo "$line"  | sed -n 's/.*load imbalance: \([0-9.]*\).*/\1/p')
    tot=$(echo "$line"  | sed -n 's/.*compute: \([0-9.]*\) s.*/\1/p')
    printf "%-8s %-7s %10s %11s%% %12s\n" "$P" "$PART" "$tot" "$comm" "$imb"
  done
done
