# draw the strong-scaling charts (speedup and efficiency) from results/timings.csv.
# it makes two panels: one for adding OpenMP threads (one process), one for adding
# MPI processes (one thread each). the dashed line is perfect scaling.
# usage: python scripts/plotScaling.py [results/timings.csv] [results/scaling.png]
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

inputFile = sys.argv[1] if len(sys.argv) > 1 else "results/timings.csv"
outputFile = sys.argv[2] if len(sys.argv) > 2 else "results/scaling.png"

# keep the best (smallest) time for each (processes, threads)
best = {}
label = ""
with open(inputFile) as file:
    for row in csv.DictReader(file):
        processes = int(row["processes"])
        threads = int(row["threads"])
        seconds = float(row["seconds"])
        label = row["networkType"] + ", " + row["numberOfNodes"] + " people"
        key = (processes, threads)
        if key not in best or seconds < best[key]:
            best[key] = seconds

baseline = best[(1, 1)]

# openmp series: one process, more threads. mpi series: more processes, one thread.
threadCounts = sorted(set(t for (p, t) in best if p == 1))
procCounts = sorted(set(p for (p, t) in best if t == 1))
openmpSpeedup = [baseline / best[(1, t)] for t in threadCounts]
mpiSpeedup = [baseline / best[(p, 1)] for p in procCounts]
openmpEff = [openmpSpeedup[i] / threadCounts[i] for i in range(len(threadCounts))]
mpiEff = [mpiSpeedup[i] / procCounts[i] for i in range(len(procCounts))]

# Amdahl's law: estimate the serial fraction f from the OpenMP points (threads,
# no message passing, so this reflects the algorithm's parallel fraction rather
# than communication cost). for p workers and measured speedup S:
#   f = (1/S - 1/p) / (1 - 1/p),  and the model predicts S(p) = 1 / (f + (1-f)/p).
estimates = []
for i in range(len(threadCounts)):
    p = threadCounts[i]
    speed = openmpSpeedup[i]
    if p > 1 and speed > 0:
        f = (1.0 / speed - 1.0 / p) / (1.0 - 1.0 / p)
        estimates.append(min(max(f, 0.0), 1.0))
serialFraction = sum(estimates) / len(estimates) if estimates else 0.0
maxSpeedup = (1.0 / serialFraction) if serialFraction > 0 else float("inf")

figure, (axSpeed, axEff) = plt.subplots(1, 2, figsize=(12, 5))

# speedup panel
ideal = list(range(1, max(max(threadCounts), max(procCounts)) + 1))
axSpeed.plot(ideal, ideal, "k--", label="perfect scaling")
axSpeed.plot(threadCounts, openmpSpeedup, "o-", label="OpenMP threads")
axSpeed.plot(procCounts, mpiSpeedup, "s-", label="MPI processes")
# Amdahl prediction curve from the estimated serial fraction
amdahlY = [1.0 / (serialFraction + (1.0 - serialFraction) / p) for p in ideal]
axSpeed.plot(ideal, amdahlY, ":", color="gray",
             label="Amdahl (f=" + str(round(serialFraction, 2)) + ")")
axSpeed.set_xlabel("number of workers")
axSpeed.set_ylabel("speedup (sequential time / parallel time)")
axSpeed.set_title("strong scaling: speedup")
axSpeed.legend()
axSpeed.grid(True, alpha=0.3)

# efficiency panel
axEff.axhline(1.0, color="k", linestyle="--", label="perfect efficiency")
axEff.plot(threadCounts, openmpEff, "o-", label="OpenMP threads")
axEff.plot(procCounts, mpiEff, "s-", label="MPI processes")
axEff.set_xlabel("number of workers")
axEff.set_ylabel("efficiency (speedup / workers)")
axEff.set_title("strong scaling: efficiency")
axEff.set_ylim(0, 1.1)
axEff.legend()
axEff.grid(True, alpha=0.3)

machine = sys.argv[3] if len(sys.argv) > 3 else "this machine"
figure.suptitle("Strong scaling on " + machine + " (" + label + ")")
plt.tight_layout()
plt.savefig(outputFile, dpi=120)
print("wrote", outputFile)
print("Amdahl serial fraction f =", round(serialFraction, 3))
if serialFraction > 0:
    print("Amdahl maximum speedup (workers -> infinity) =", round(maxSpeedup, 2), "x")

