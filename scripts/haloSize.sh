#!/usr/bin/env bash
# how many ghost copies each partitioning produces, which is the concrete size of
# the communication. run from the project root.
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"

NODES="${1:-500000}"
SEED="${2:-777}"

for TYPE in smallworld scalefree; do
  for PART in block metis; do
    for P in 2 4 8; do
      line=$(OMP_NUM_THREADS=1 mpiexec -n "$P" ./build/parallelSimulation "$NODES" "$TYPE" "$SEED" "$PART" 2>/dev/null | grep -o "halo:.*day")
      printf "%-11s %-6s %2s procs : %s\n" "$TYPE" "$PART" "$P" "$line"
    done
  done
done
