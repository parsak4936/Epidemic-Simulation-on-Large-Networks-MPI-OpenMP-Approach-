# draw the task timeline from results/timeline.csv and, more importantly, measure
# the most different algorithms that ran at the same time. if that number is 2 or
# more, then different algorithms overlapped in wall clock time, which is the proof
# of task parallelism.
# usage: python scripts/plotTimeline.py [results/timeline.csv] [results/timeline.png]
import sys
import csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

inputFile = sys.argv[1] if len(sys.argv) > 1 else "results/timeline.csv"
outputFile = sys.argv[2] if len(sys.argv) > 2 else "results/timeline.png"

rows = []
with open(inputFile) as file:
    for row in csv.DictReader(file):
        rows.append((int(row["rank"]), row["task"], int(row["step"]),
                     float(row["start"]), float(row["end"])))

colors = {"A": "#4C78A8", "B": "#F58518", "C": "#54A24B", "D": "#E45756"}
taskName = {"A": "A spread", "B": "B tracing", "C": "C targeting", "D": "D clustering"}
ranks = sorted(set(row[0] for row in rows))

figure, axes = plt.subplots(figsize=(12, 1.5 + 0.7 * len(ranks)))
for rank, task, step, start, end in rows:
    width = max(end - start, 0.0005)
    axes.barh(rank, width, left=start, height=0.6, color=colors.get(task, "#999999"))

axes.set_yticks(ranks)
axes.set_yticklabels([("master (A)" if r == 0 else "worker " + str(r)) for r in ranks])
axes.set_xlabel("time (seconds since start)")
axes.set_title("task timeline: four different algorithms overlapping (task parallelism)")
legend = [Patch(facecolor=colors[k], label=taskName[k]) for k in ["A", "B", "C", "D"]]
axes.legend(handles=legend, loc="upper right", ncol=4)
plt.tight_layout()
plt.savefig(outputFile, dpi=120)
print("wrote", outputFile)

# sweep a line across time and count how many distinct task types are active
events = []
for rank, task, step, start, end in rows:
    events.append((start, 1, task))
    events.append((end, -1, task))
events.sort()

active = {}
mostDifferent = 0
momentOfMost = 0.0
for time, change, task in events:
    active[task] = active.get(task, 0) + change
    distinct = sum(1 for taskType in active if active[taskType] > 0)
    if distinct > mostDifferent:
        mostDifferent = distinct
        momentOfMost = time

print("most different algorithms running at the same time:", mostDifferent,
      "(around t =", round(momentOfMost, 4), "s)")
if mostDifferent >= 2:
    print("=> different algorithms overlapped in time: this is real task parallelism.")
