# Epidemic Simulation, Contact Tracing and Intervention on Large Networks

A hybrid MPI + OpenMP simulation of a disease spreading through a contact network of up
to a million people, with three more analyses running on the same network at the same
time.

High Performance Computing course project, University of Messina.

## What it does

Four different algorithms run concurrently on the same evolving network:

| Task | What it computes | Algorithm |
|------|------------------|-----------|
| A | how the infection spreads | stochastic SEIR state update |
| B | who the infected have been in contact with | breadth first search |
| C | who to vaccinate | PageRank style centrality |
| D | how many separate outbreaks are active | union find |

The point is to combine two kinds of parallelism in one program:

- **data parallelism**, where the same work is split over different parts of the network:
  MPI gives each process a share of the people, and OpenMP threads the work inside each
  process. Border people are exchanged between processes once per simulated day.
- **task parallelism**, where the four different algorithms run at the same time on
  different processes behind a master and worker scheduler. The master never waits: while
  the workers analyse day *t* it is already computing day *t+1*.

## Requirements

Linux, with:

```
build-essential  cmake  mpich  libmetis-dev  python3-venv
```

METIS is optional. Without it the program still builds and uses the simple block
partition.

## Build

```bash
cmake -S . -B build && cmake --build build
```

This produces seven programs in `build/`:

| Program | What it is |
|---------|-----------|
| `sequentialSimulation` | the plain baseline, one process, one thread |
| `parallelSimulation` | the same epidemic split across processes and threads |
| `taskScheduler` | the four tasks running at the same time |
| `responseSimulation` | the same epidemic with task C vaccinating the most central people |
| `taskBenchmark` | each task on its own, at several thread counts |
| `degreeStats` | the degree distribution of a generated network |
| `timingReport` | prints the speedup table from the recorded runs |

The measurements need nothing beyond these. The chart scripts additionally need `matplotlib`;
without it every measurement still runs and prints its numbers, only the pictures are skipped.

```bash
python3 -m venv .venv && source .venv/bin/activate && pip install matplotlib
```

## Run

Every command has the same shape: threads on the left, processes in `-n`, then the
number of people, the network type (`smallworld` or `scalefree`) and a seed.

```bash
# the baseline
./build/sequentialSimulation 500000 smallworld 777

# threads only
OMP_NUM_THREADS=4 mpiexec -n 1 ./build/parallelSimulation 500000 smallworld 777

# processes only
OMP_NUM_THREADS=1 mpiexec -n 4 ./build/parallelSimulation 500000 smallworld 777

# both together, the hybrid configuration
OMP_NUM_THREADS=2 mpiexec -n 4 ./build/parallelSimulation 500000 smallworld 777

# METIS instead of the block partition
OMP_NUM_THREADS=1 mpiexec -n 4 ./build/parallelSimulation 500000 scalefree 777 metis

# the four tasks running at the same time
OMP_NUM_THREADS=2 mpiexec -n 4 ./build/taskScheduler 500000 smallworld 777

# the same, with one worker deliberately failing, to show the recovery
OMP_NUM_THREADS=1 mpiexec -n 4 ./build/taskScheduler 50000 smallworld 777 failure timeout=3

# the speedup table from everything recorded so far
./build/timingReport
```

Every run appends a line to `results/timings.csv`, so the table builds up as you go.

## Correctness

The parallel version produces **exactly** the same epidemic as the sequential one, byte for
byte, on any number of processes and threads and with either partitioner. Check it
yourself:

```bash
./build/sequentialSimulation 200000 smallworld 777 && cp results/epidemicCurve.csv /tmp/seq.csv
OMP_NUM_THREADS=2 mpiexec -n 4 ./build/parallelSimulation 200000 smallworld 777
diff /tmp/seq.csv results/epidemicCurve.csv && echo identical
```

This works because every random decision is computed from the person's id, the day and the
seed rather than drawn from a shared stream, so it does not depend on execution order.

## Results

Measured on two machines: a laptop with 2 physical cores (Intel i5-8265U) and a Google
Compute Engine `e2-standard-8` with 4 physical cores (AMD EPYC 7B12). Small world network,
1 million people. Full tables are in `results/`.

**How the timings are taken.** Every configuration is run seven times and the **median** is
reported, not the best run. The repeats are interleaved, so one run of every configuration is
taken before the second run of any of them, and a busy minute on the machine is shared out
rather than charged to whichever configuration happened to be measured during it. One run of
each is discarded first as a warm up. Taking the best of three instead, which is the common
habit, moved individual speedups by up to 30% between sweeps.

**MPI alone, OpenMP alone, and the two combined.** Every combination of processes and
threads, 1 million people, small world, median of seven runs each.

| workers | split | approach | laptop | cloud |
|--------:|-------|----------|-------:|------:|
| 2  | 1 x 2  | OpenMP alone | **1.75** | 1.15 |
| 2  | 2 x 1  | MPI alone    | 1.46 | **1.45** |
| 4  | 1 x 4  | OpenMP alone | **2.55** | 1.58 |
| 4  | 2 x 2  | combined     | 2.00 | 1.55 |
| 4  | 4 x 1  | MPI alone    | 1.73 | **2.64** |
| 8  | 1 x 8  | OpenMP alone | **2.40** | 1.66 |
| 8  | 2 x 4  | combined     | 1.60 | 1.66 |
| 8  | 4 x 2  | combined     | 1.35 | **3.12** |
| 8  | 8 x 1  | MPI alone    | 1.02 | 2.64 |
| 16 | 1 x 16 | OpenMP alone | **2.16** | **1.85** |
| 16 | 2 x 8  | combined     | 1.97 | 0.73 |
| 16 | 4 x 4  | combined     | 1.13 | 0.86 |
| 16 | 8 x 2  | combined     | 1.02 | 0.85 |
| 16 | 16 x 1 | MPI alone    | 0.84 | 1.52 |

The interesting row is 4 x 2 on the cloud, at **3.12** — the fastest result anywhere in the
study. At eight workers MPI alone is stuck at 2.64 because it has taken all four physical
cores, and OpenMP alone is stuck at 1.66 because eight threads sharing one copy of the network
run out of memory bandwidth. The mixed split pays neither price in full: four processes take
the four physical cores and each keeps its own copy of the data, and the second thread in each
then works while the first waits on memory. That configuration is only available to a program
that implements both mechanisms.

How much better it is, stated carefully: 4 x 2 beat the best thread only configuration in
**every one of 49** run-against-run pairings (permutation test p = 0.006). Against the best
process only configuration it is 18% ahead on the median and won 39 of 49 pairings, but
p = 0.07, which is short of the usual threshold. So it is decisively better than threads alone,
and ahead of processes alone without quite clearing the bar for statistical separation.

The best split is also not the same on both machines: on the laptop, with only two real
cores, threads win at every worker count and processes are worst everywhere. A study on one
machine would have drawn the wrong general conclusion. The laptop numbers are noisy — the same
sweep run three times moved values by up to 30% while giving the same ranking every time — so
they are quoted for the ordering rather than the exact figures.

Reproduce it with:

```bash
bash scripts/runGrid.sh 1000000 smallworld 777 <label> 7
```

**The same grid on a scale free network**, where the hubs make the boundaries expensive. Best of
each approach, 1 million people, block partition, median of seven runs:

| approach | small world | scale free |
|----------|------------:|-----------:|
| pure OpenMP | 1.85x | **2.50x** |
| pure MPI | 2.64x | 2.08x |
| hybrid | **3.12x** | 2.41x |

The ranking moves a second time, and now on the same machine. Threads barely notice the change
because they share one copy of the network and send nothing; everything holding more than one
process pays, because a scale free network of the same size needs 3.38x as many ghost copies.
At eight workers the hybrid split is still ahead of both pure ones at that worker count (2.41x
against 2.34x and 1.99x), so the case for both mechanisms holds; what it loses is the outright
win, because 1x16 keeps improving while every configuration with processes is already past 40%
communication. The best split depends on the machine and on the network together.

```bash
bash scripts/runGrid.sh 1000000 scalefree 777 <label> 7
```

**Strong scaling on the cloud machine** (varying one mechanism at a time). These rows come
from the same seven runs as the table above, so the two cannot disagree.

| mode | processes | threads | time (s) | speedup | comm% |
|------|----------:|--------:|---------:|--------:|------:|
| sequential | 1 | 1 | 1.301 | 1.00 | 0% |
| OpenMP | 1 | 4 | 0.822 | 1.58 | 0% |
| OpenMP | 1 | 8 | 0.784 | 1.66 | 0% |
| OpenMP | 1 | 16 | 0.702 | 1.85 | 0% |
| MPI | 4 | 1 | 0.493 | 2.64 | 18% |
| MPI | 8 | 1 | 0.492 | 2.64 | 29% |
| MPI | 16 | 1 | 0.854 | 1.52 | 69% |

Communication overhead is the share of the time spent exchanging border states instead of
computing. It grows with the process count, and is the main reason the speedup eventually
stops improving.

**How much is actually exchanged.** The halo is the set of people a process must keep a
copy of because they belong to another process. Its size is what the partitioner controls
(500k people):

| network | partition | 2 procs | 4 procs | 8 procs |
|---------|-----------|--------:|--------:|--------:|
| small world | block | 130k (26%) | 210k (42%) | 253k (51%) |
| scale free  | block | 441k (88%) | 982k (196%) | 1,511k (302%) |
| scale free  | METIS | 354k (71%) | 704k (141%) | 1,016k (203%) |

Scale free needs 3.4 times as many ghost copies as small world for the same population,
because its hubs have contacts everywhere. Above 100% means one person is copied into
several processes at once. Run `bash scripts/measure.sh halo`.

**Each task on its own threads** (cloud, scale free, 1 million people)

| task | 1 thread | 8 threads | speedup |
|------|---------:|----------:|--------:|
| A spread | 0.023 s | 0.006 s | 4.19 |
| B tracing | 0.098 s | 0.025 s | 3.93 |
| C targeting | 0.647 s | 0.413 s | 1.57 |
| D clustering | 0.012 s | 0.012 s | sequential by design |

Clustering is left sequential on purpose: union find rewrites one shared structure, and the
profile says it is under one percent of the runtime, so there is nothing to gain.

**Block partition against METIS** (cloud, scale free, 500k people)

| processes | partition | comm% | load imbalance |
|----------:|-----------|------:|---------------:|
| 2 | block | 24.2% | 1.10 |
| 2 | METIS | 12.7% | 1.01 |
| 4 | block | 39.8% | 1.10 |
| 4 | METIS | 29.0% | 1.06 |
| 8 | block | 50.7% | 1.18 |
| 8 | METIS | 39.1% | 1.07 |

METIS chooses the split so fewer contacts cross between processes, which cuts the
communication and evens out the load. Changing nothing but the partitioner moves these
numbers, which shows the cost really is driven by the number of crossing edges. Raw output in
`results/cloud/partitioners_scalefree.txt`.

**Network shape.** Both networks are built with the same average, six contacts per person, so
they give the sequential run the same amount of work. Everything else about them differs:

| | small world | scale free |
|---|---:|---:|
| median contacts | 6 | 4 |
| the most connected person | 11 | 2,379 |
| busiest 1% hold | 1.4% of all contacts | 11.3% |
| most connected ÷ median | 1.8x | 595x |

Since the inner loop costs O(degree) per person, that last row is the spread in per-person
cost. It is why tracing needs dynamic scheduling, why a block split of a scale free graph
gives an imbalance of 1.41, and why the same population needs 3.4x as many ghost copies.
Reproduce with `./build/degreeStats 500000 scalefree 777`.

**Profiling.** gprof on the sequential build puts about 88% of the runtime in one function,
the per person daily update, which is exactly the loop that was parallelised.

**Task parallelism.** Every task records when it starts and finishes. Reading that log back
shows four different algorithms active at the same instant, which is what makes this task
parallelism rather than four phases in sequence. See `results/figures/taskTimeline.png`.

**Fault handling.** Each dispatched job has a deadline; a worker that misses it loses the
job back to the queue and another worker finishes it. With the `failure` option one worker
deliberately stops answering, and all 252 jobs still complete.

**Interventions.** Vaccinating the most central people, chosen by task C, cuts a scale free
outbreak from about 145,000 infected to 18, by vaccinating 6% of the population. On a small
world network the same strategy barely helps, because there are no hubs to remove.

## Layout

```
src/        the code
scripts/    four scripts, described below
results/    the raw output behind every table in the report
```

The result folders, since there are several:

| folder | machine | network |
|--------|---------|---------|
| `cloud/`     | cloud VM, e2-standard-8 | small world |
| `laptop/`    | laptop, 2 cores         | small world |
| `laptopSF/`  | laptop, 2 cores         | scale free |
| `cloud2/`    | a second cloud VM of the same type | small world, the reproducibility check |
| `cloud2SF/`  | that second cloud VM    | scale free |

## Reproducing the numbers

There are four scripts. Two run experiments, two turn the output into numbers and charts.

```bash
bash scripts/runGrid.sh 1000000 smallworld 777 laptop 7   # the main experiment
bash scripts/runGrid.sh confirm 1000000 smallworld 777 laptop 15
bash scripts/measure.sh halo | metis | weak | intervention | memory | profile | degrees
python scripts/analyse.py  modes | confirm   <rawCsv>
python scripts/plots.py    grid | scaling | compare | weak | timeline | degrees | curve
```

`runGrid.sh` calls `analyse.py` and `plots.py` itself, so the main experiment is one command.
Each script explains its arguments in the comment at the top.

Every table in the report has a file behind it, so the numbers can be checked against the runs
that produced them rather than taken on trust:

| what the report shows | produced by | raw output |
|---|---|---|
| the process/thread grid, small world | `runGrid.sh 1000000 smallworld 777 <label> 7` | `results/cloud/`, `results/laptop/combinedModesRaw.csv` |
| the same grid on a scale free network | `runGrid.sh 1000000 scalefree 777 <label> 7` | `results/cloud2SF/`, `results/laptopSF/combinedModesRaw.csv` |
| the reproducibility check on a second cloud instance | `runGrid.sh 1000000 smallworld 777 cloud2 7` | `results/cloud2/combinedModesRaw.csv` |
| the head to head test and its p values | `analyse.py confirm <rawCsv>` | recomputed from `results/cloud/combinedModesRaw.csv`, the same runs as the row above |
| ghost copies per partitioner | `measure.sh halo` | `results/haloSize.txt` |
| block against METIS, share and balance | `measure.sh metis` | `results/<machine>/partitioners_scalefree.txt` |
| block against METIS, timings over five repeats | `parallelSimulation ... block\|metis` | `results/cloud2SF/metisRepeats.txt` |
| scale free strong scaling | `measure.sh` (earlier sweep) | `results/<machine>/table_scalefree_1000000.txt` |
| weak scaling | `measure.sh weak` | `results/<machine>/weakTimings.csv` |
| each task on its own threads | `build/taskBenchmark` | `results/<machine>/taskThreads.csv` |
| the gprof profile | `measure.sh profile` | `results/<machine>/gprofReport.txt` |
| effect of the interventions | `measure.sh intervention` | `results/interventionEffect.csv` |
| degree distribution | `measure.sh degrees` | `results/degrees_*.csv` |
| the scheduler and its fault recovery | `build/taskScheduler` | `results/<machine>/scheduler_*.txt` |
| the two machines | — | `results/<machine>/machine.txt` |

Note on the commands: thread count comes from `OMP_NUM_THREADS`, because that is the variable
the OpenMP standard defines, and process count from `mpiexec -n`. So a hybrid run names both,
one on each side of the command.
