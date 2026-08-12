#!/usr/bin/env bash
# the main experiment: every combination of MPI processes and OpenMP threads, on one machine.
#   processes x 1 threads  = pure MPI
#   1 process x threads    = pure OpenMP
#   anything else          = the hybrid
#
# every configuration is repeated and every run is kept, so the spread can be reported next to
# the number. the repeat is the outer loop, so a busy minute on the machine is shared out over
# all the configurations instead of landing on whichever one happened to be running.
#
#   bash scripts/runGrid.sh [people] [network] [seed] [label] [repeats]
#   bash scripts/runGrid.sh confirm [people] [network] [seed] [label] [repeats]
#
# the second form re-measures only the two closest configurations with more repeats, to settle
# whether the hybrid split is really ahead of the best pure one. the repeat count is fixed
# before the run and every run is kept, so the answer is whatever comes out.
export PATH="$HOME/opt/mpich/bin:$PATH"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")" || exit 1
source .venv/bin/activate 2>/dev/null || true
if command -v python >/dev/null 2>&1; then PY=python; else PY=python3; fi

MODE=full
if [ "$1" = "confirm" ]; then MODE=confirm; shift; fi

NODES="${1:-1000000}"
TYPE="${2:-smallworld}"
SEED="${3:-777}"
LABEL="${4:-local}"
REPEATS="${5:-7}"
OUT="results/$LABEL"

if [ "$MODE" = "confirm" ]; then
  CONFIGS="1x1 4x2 8x1"
  RAW="$OUT/confirmRaw.csv"
  REPEATS="${5:-15}"
else
  CONFIGS="1x1 \
           1x2 2x1 \
           1x4 2x2 4x1 \
           1x8 2x4 4x2 8x1 \
           1x16 2x8 4x4 8x2 16x1"
  RAW="$OUT/combinedModesRaw.csv"
fi

# refuse to start on nonsense arguments, so a typo cannot wipe an existing sweep
case "$NODES" in ''|*[!0-9]*) echo "stopping: people must be a whole number, got '$NODES'" >&2; exit 1 ;; esac
case "$REPEATS" in ''|*[!0-9]*) echo "stopping: repeats must be a whole number, got '$REPEATS'" >&2; exit 1 ;; esac
if [ ! -x ./build/parallelSimulation ] || [ ! -x ./build/sequentialSimulation ]; then
  echo "stopping: build first with  cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

mkdir -p "$OUT"
if [ -s "$RAW" ]; then
  mv "$RAW" "${RAW%.csv}.previous.csv"
  echo "previous raw data moved aside to ${RAW%.csv}.previous.csv"
fi

echo "network: $TYPE, people: $NODES, machine: $LABEL, repeats: $REPEATS"
echo "cores reported by the system: $(nproc)"
echo ""

runOne() {   # $1 processes, $2 threads -> prints "seconds comm"
  if [ "$1" = "1" ] && [ "$2" = "1" ]; then
    out=$(./build/sequentialSimulation "$NODES" "$TYPE" "$SEED" 2>/dev/null)
    c=0
  else
    out=$(OMP_NUM_THREADS="$2" mpiexec -n "$1" ./build/parallelSimulation \
          "$NODES" "$TYPE" "$SEED" 2>/dev/null)
    c=$(echo "$out" | sed -n 's/.*(\([0-9.]*\)% of the work).*/\1/p')
  fi
  t=$(echo "$out" | sed -n 's/.*simulation time: \([0-9.]*\).*/\1/p')
  [ -z "$c" ] && c=0
  echo "$t $c"
}

# one run of each, thrown away, so the first kept measurement is not the one paying to
# load the binary and fill the caches
echo "warming up..."
for cfg in $CONFIGS; do runOne "${cfg%x*}" "${cfg#*x}" > /dev/null; done

echo "procs,threads,repeat,seconds,comm" > "$RAW"
for r in $(seq 1 "$REPEATS"); do
  echo "round $r of $REPEATS"
  for cfg in $CONFIGS; do
    P="${cfg%x*}"; T="${cfg#*x}"
    read -r t c <<< "$(runOne "$P" "$T")"
    if [ -n "$t" ]; then
      echo "$P,$T,$r,$t,$c" >> "$RAW"
      printf "    %2sx%-3s %s\n" "$P" "$T" "$t"
    fi
  done
done

echo ""
if [ "$MODE" = "confirm" ]; then
  $PY scripts/analyse.py confirm "$RAW" | tee "$OUT/confirmBest.txt"
else
  $PY scripts/analyse.py modes "$RAW" "$LABEL" | tee "$OUT/combinedModes.txt"
  $PY scripts/plots.py grid "$OUT/combinedModes.csv" "$OUT/combinedModes.png" "the $LABEL machine" \
    || echo "(no chart: matplotlib is not installed here, the numbers above are still good)"
fi
