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

This produces four programs in `build/`:

| Program | What it is |
|---------|-----------|
| `sequentialSimulation` | the plain baseline, one process, one thread |
| `parallelSimulation` | the same epidemic split across processes and threads |
| `taskScheduler` | the four tasks running at the same time |
| `timingReport` | prints the speedup table from the recorded runs |

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
1 million people, best of three runs. Full tables are in `results/`.

**Strong scaling on the cloud machine**

| mode | processes | threads | time (s) | speedup | comm% |
|------|----------:|--------:|---------:|--------:|------:|
| sequential | 1 | 1 | 1.373 | 1.00 | 0% |
| OpenMP | 1 | 4 | 0.978 | 1.40 | 0% |
| OpenMP | 1 | 8 | 0.797 | 1.72 | 0% |
| MPI | 4 | 1 | 0.499 | 2.75 | 19% |
| MPI | 8 | 1 | 0.485 | 2.83 | 28% |
| MPI | 16 | 1 | 0.660 | 2.08 | 70% |

Communication overhead is the share of the time spent exchanging border states instead of
computing. It grows with the process count, and is the main reason the speedup eventually
stops improving.

**The same workers, split differently** (1 million people)

| workers | split | laptop | cloud |
|--------:|-------|-------:|------:|
| 4 | 1 process x 4 threads | **2.33** | 1.30 |
| 4 | 2 x 2 | 1.76 | 1.60 |
| 4 | 4 x 1 | 1.47 | **2.63** |
| 8 | 1 x 8 | **2.29** | 1.59 |
| 8 | 4 x 2 | 1.23 | **2.93** |
| 8 | 8 x 1 | 0.92 | 2.57 |

The best split is not the same on both machines. On the laptop, with only two real cores,
threads win everywhere. On the cloud the fastest configuration of all is four processes
with two threads each: a genuinely mixed setup that beats both pure MPI and pure OpenMP at
the same worker count.

**Each task on its own threads** (cloud, scale free, 1 million people)

| task | 1 thread | 8 threads | speedup |
|------|---------:|----------:|--------:|
| A spread | 0.023 s | 0.006 s | 4.09 |
| B tracing | 0.120 s | 0.029 s | 4.17 |
| C targeting | 0.847 s | 0.506 s | 1.67 |
| D clustering | 0.013 s | 0.012 s | sequential by design |

Clustering is left sequential on purpose: union find rewrites one shared structure, and the
profile says it is under one percent of the runtime, so there is nothing to gain.

**Block partition against METIS** (cloud, scale free, 500k people)

| processes | partition | comm% | load imbalance |
|----------:|-----------|------:|---------------:|
| 2 | block | 25.6% | 1.09 |
| 2 | METIS | 13.1% | 1.02 |
| 4 | block | 39.8% | 1.11 |
| 4 | METIS | 26.7% | 1.03 |

METIS chooses the split so fewer contacts cross between processes, which cuts the
communication and evens out the load. Changing nothing but the partitioner moves these
numbers, which shows the cost really is driven by the number of crossing edges.

**Profiling.** gprof on the sequential build puts about 90% of the runtime in one function,
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
scripts/    plotting and benchmark helpers
results/    measurements from both machines, and the figures
```
