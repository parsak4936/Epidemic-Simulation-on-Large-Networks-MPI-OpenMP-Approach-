# Shows how the SAME number of workers performs when split differently between MPI
# processes and OpenMP threads. Bars are grouped by total workers, so within a group
# you are comparing shapes of the same amount of parallelism.
# usage: python scripts/plotCombinedModes.py results/local/combinedModes.csv out.png "the laptop"
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

inputFile = sys.argv[1] if len(sys.argv) > 1 else "results/local/combinedModes.csv"
outputFile = sys.argv[2] if len(sys.argv) > 2 else "results/combinedModes.png"
machine = sys.argv[3] if len(sys.argv) > 3 else "this machine"

rows = []
with open(inputFile) as file:
    for line in csv.reader(file):
        if len(line) < 6 or line[0] == "sequential":
            continue
        procs, threads = int(line[1]), int(line[2])
        rows.append((procs * threads, procs, threads, float(line[4]), float(line[5])))

groups = sorted(set(r[0] for r in rows))
figure, (axSpeed, axComm) = plt.subplots(1, 2, figsize=(12, 5))

positions, labels, speeds, comms, colors = [], [], [], [], []
palette = {"threads only": "#4C78A8", "mixed": "#F58518", "processes only": "#E45756"}
pos = 0
for workers in groups:
    entries = sorted([r for r in rows if r[0] == workers], key=lambda r: r[1])
    for total, procs, threads, speed, comm in entries:
        kind = "threads only" if procs == 1 else ("processes only" if threads == 1 else "mixed")
        positions.append(pos)
        labels.append(str(procs) + "x" + str(threads))
        speeds.append(speed)
        comms.append(comm)
        colors.append(palette[kind])
        pos += 1
    pos += 1   # gap between groups

axSpeed.bar(positions, speeds, color=colors)
axSpeed.set_xticks(positions)
axSpeed.set_xticklabels(labels, rotation=45)
axSpeed.set_ylabel("speedup")
axSpeed.set_title("speedup by split (processes x threads)")
axSpeed.grid(True, axis="y", alpha=0.3)

axComm.bar(positions, comms, color=colors)
axComm.set_xticks(positions)
axComm.set_xticklabels(labels, rotation=45)
axComm.set_ylabel("communication overhead (%)")
axComm.set_title("communication cost by split")
axComm.grid(True, axis="y", alpha=0.3)

handles = [plt.Rectangle((0, 0), 1, 1, color=palette[k]) for k in palette]
axSpeed.legend(handles, list(palette.keys()), loc="upper left")

figure.suptitle("Same workers, different split, on " + machine
                + " (bars grouped by total workers)")
plt.tight_layout()
plt.savefig(outputFile, dpi=120)
print("wrote", outputFile)
