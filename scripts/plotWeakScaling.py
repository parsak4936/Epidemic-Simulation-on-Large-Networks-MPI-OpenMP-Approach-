# draw the weak-scaling charts from results/weakTimings.csv.
# weak scaling keeps the work per worker constant, so ideally the time stays flat.
# weak efficiency = time(1 worker) / time(W workers); 1.0 would be perfect.
# usage: python scripts/plotWeakScaling.py [results/weakTimings.csv] [results/weakScaling.png]
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

inputFile = sys.argv[1] if len(sys.argv) > 1 else "results/weakTimings.csv"
outputFile = sys.argv[2] if len(sys.argv) > 2 else "results/weakScaling.png"

workers = []
nodes = []
seconds = []
with open(inputFile) as file:
    for row in csv.DictReader(file):
        workers.append(int(row["workers"]))
        nodes.append(int(row["nodes"]))
        seconds.append(float(row["seconds"]))

baseTime = seconds[0]
efficiency = [baseTime / t for t in seconds]

figure, (axTime, axEff) = plt.subplots(1, 2, figsize=(12, 5))

axTime.plot(workers, seconds, "o-", label="measured time")
axTime.axhline(baseTime, color="k", linestyle="--", label="ideal (flat)")
axTime.set_xlabel("number of workers (people grow with them)")
axTime.set_ylabel("time (seconds)")
axTime.set_title("weak scaling: time per run")
axTime.set_ylim(0, max(seconds) * 1.2)
axTime.legend()
axTime.grid(True, alpha=0.3)

axEff.plot(workers, efficiency, "o-", label="weak efficiency")
axEff.axhline(1.0, color="k", linestyle="--", label="perfect efficiency")
axEff.set_xlabel("number of workers")
axEff.set_ylabel("weak efficiency (time on 1 worker / time on W)")
axEff.set_title("weak scaling: efficiency")
axEff.set_ylim(0, 1.1)
axEff.legend()
axEff.grid(True, alpha=0.3)

machine = sys.argv[3] if len(sys.argv) > 3 else "this machine"
figure.suptitle("Weak scaling on " + machine + ": "
                + str(nodes[0] // workers[0]) + " people per worker")
plt.tight_layout()
plt.savefig(outputFile, dpi=120)
print("wrote", outputFile)
for i in range(len(workers)):
    print("workers", workers[i], "| people", nodes[i], "| time", round(seconds[i], 4),
          "s | weak efficiency", round(efficiency[i], 2))
