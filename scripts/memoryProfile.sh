#!/usr/bin/env bash
# measures peak memory and time as the problem grows, so the cost of the sequential
# version can be stated with numbers instead of adjectives.
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"

TYPE="${1:-smallworld}"
SEED="${2:-777}"

echo "network: $TYPE"
printf "%10s %12s %12s %14s %12s\n" "people" "peak KB" "peak MB" "bytes/person" "time (s)"

for N in 125000 250000 500000 1000000 2000000; do
  out=$(/usr/bin/time -v ./build/sequentialSimulation "$N" "$TYPE" "$SEED" 2>&1)
  kb=$(echo "$out" | sed -n 's/.*Maximum resident set size (kbytes): \([0-9]*\).*/\1/p')
  secs=$(echo "$out" | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
  awk -v n="$N" -v k="$kb" -v s="$secs" 'BEGIN{
    printf "%10d %12d %12.1f %14.1f %12s\n", n, k, k/1024.0, k*1024.0/n, s }'
done
