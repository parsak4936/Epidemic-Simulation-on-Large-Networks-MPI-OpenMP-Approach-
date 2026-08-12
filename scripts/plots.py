# draws every figure in the report. one script, pick the chart with the first argument.
#
#   python scripts/plots.py grid     results/laptop/combinedModes.csv   out.png "the laptop"
#   python scripts/plots.py scaling  results/laptop/combinedModesRaw.csv out.png "the laptop" "small world, 1M"
#   python scripts/plots.py compare  laptopRaw.csv cloudRaw.csv          out.png "small world, 1M"
#   python scripts/plots.py weak     results/laptop/weakTimings.csv     out.png "the laptop"
#   python scripts/plots.py timeline results/timeline.csv               out.png
#   python scripts/plots.py degrees  degrees_scalefree.csv degrees_smallworld.csv out.png
#   python scripts/plots.py curve    results/epidemicCurve.csv          out.png
import sys
import csv
import statistics
from collections import defaultdict
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

TASK_COLOURS = {"A": "#4C78A8", "B": "#F58518", "C": "#54A24B", "D": "#E45756"}


def readRuns(path):
    """(processes, threads) -> median seconds, from either csv layout we produce"""
    runs = defaultdict(list)
    label = ""
    with open(path) as file:
        for row in csv.DictReader(file):
            p = int(row["procs"] if "procs" in row else row["processes"])
            runs[(p, int(row["threads"]))].append(float(row["seconds"]))
            if "networkType" in row:
                label = row["networkType"] + ", " + row["numberOfNodes"] + " people"
    return {k: statistics.median(v) for k, v in runs.items()}, label


def grid(args):
    """speedup and communication for every process/thread split, grouped by total workers"""
    rows = []
    with open(args[0]) as file:
        for line in csv.reader(file):
            if len(line) < 6 or line[0] in ("sequential", "mode"):
                continue
            p, t = int(line[1]), int(line[2])
            rows.append((p * t, p, t, float(line[4]), float(line[5])))

    groups = sorted(set(r[0] for r in rows))
    figure, (axSpeed, axComm) = plt.subplots(1, 2, figsize=(12, 5))
    palette = {"threads only": "#4C78A8", "mixed": "#F58518", "processes only": "#E45756"}
    positions, labels, speeds, comms, colours = [], [], [], [], []
    pos = 0
    for workers in groups:
        for total, p, t, speedup, comm in sorted([r for r in rows if r[0] == workers],
                                                 key=lambda r: r[1]):
            kind = "threads only" if p == 1 else ("processes only" if t == 1 else "mixed")
            positions.append(pos)
            labels.append(f"{p}x{t}")
            speeds.append(speedup)
            comms.append(comm)
            colours.append(palette[kind])
            pos += 1
        pos += 1

    axSpeed.bar(positions, speeds, color=colours)
    axSpeed.set_xticks(positions)
    axSpeed.set_xticklabels(labels, rotation=45)
    axSpeed.set_ylabel("speedup")
    axSpeed.set_title("speedup by split (processes x threads)")
    axSpeed.legend(handles=[Patch(facecolor=c, label=k) for k, c in palette.items()])
    axSpeed.grid(True, alpha=0.3, axis="y")

    axComm.bar(positions, comms, color=colours)
    axComm.set_xticks(positions)
    axComm.set_xticklabels(labels, rotation=45)
    axComm.set_ylabel("communication overhead (%)")
    axComm.set_title("communication cost by split")
    axComm.grid(True, alpha=0.3, axis="y")

    machine = args[2] if len(args) > 2 else "this machine"
    figure.suptitle("Same workers, different split, on " + machine
                    + " (bars grouped by total workers)")
    save(figure, args[1])


def scaling(args):
    """speedup and efficiency as one mechanism is turned up, against ideal and Amdahl"""
    best, label = readRuns(args[0])
    if len(args) > 4:
        label = args[4]
    baseline = best[(1, 1)]
    threads = sorted(t for (p, t) in best if p == 1)
    procs = sorted(p for (p, t) in best if t == 1)
    ompSpeed = [baseline / best[(1, t)] for t in threads]
    mpiSpeed = [baseline / best[(p, 1)] for p in procs]

    # estimate Amdahl's serial fraction from the thread series
    estimates = []
    for i, p in enumerate(threads):
        if p > 1 and ompSpeed[i] > 0:
            f = (1.0 / ompSpeed[i] - 1.0 / p) / (1.0 - 1.0 / p)
            estimates.append(min(max(f, 0.0), 1.0))
    serial = sum(estimates) / len(estimates) if estimates else 0.0

    figure, (axSpeed, axEff) = plt.subplots(1, 2, figsize=(12, 5))
    ideal = list(range(1, max(max(threads), max(procs)) + 1))
    axSpeed.plot(ideal, ideal, "k--", label="perfect scaling")
    axSpeed.plot(threads, ompSpeed, "o-", label="OpenMP threads")
    axSpeed.plot(procs, mpiSpeed, "s-", label="MPI processes")
    axSpeed.plot(ideal, [1.0 / (serial + (1.0 - serial) / p) for p in ideal], ":",
                 color="gray", label=f"Amdahl (f={round(serial, 2)})")
    axSpeed.set_xlabel("number of workers")
    axSpeed.set_ylabel("speedup (sequential time / parallel time)")
    axSpeed.set_title("strong scaling: speedup")
    axSpeed.legend()
    axSpeed.grid(True, alpha=0.3)

    axEff.axhline(1.0, color="k", linestyle="--", label="perfect efficiency")
    axEff.plot(threads, [ompSpeed[i] / threads[i] for i in range(len(threads))], "o-",
               label="OpenMP threads")
    axEff.plot(procs, [mpiSpeed[i] / procs[i] for i in range(len(procs))], "s-",
               label="MPI processes")
    axEff.set_xlabel("number of workers")
    axEff.set_ylabel("efficiency (speedup / workers)")
    axEff.set_title("strong scaling: efficiency")
    axEff.set_ylim(0, 1.1)
    axEff.legend()
    axEff.grid(True, alpha=0.3)

    machine = args[2] if len(args) > 2 else "this machine"
    figure.suptitle(f"Strong scaling on {machine} ({label})")
    save(figure, args[1])
    print("Amdahl serial fraction f =", round(serial, 3))


def compare(args):
    """the same experiment on both machines, side by side"""
    def series(path):
        best, label = readRuns(path)
        base = best[(1, 1)]
        threads = sorted(t for (p, t) in best if p == 1)
        procs = sorted(p for (p, t) in best if t == 1)
        return (threads, [base / best[(1, t)] for t in threads],
                procs, [base / best[(p, 1)] for p in procs], label)

    lT, lOmp, lP, lMpi, label = series(args[0])
    cT, cOmp, cP, cMpi, _ = series(args[1])
    if len(args) > 3:
        label = args[3]

    figure, (axMpi, axOmp) = plt.subplots(1, 2, figsize=(12, 5))
    ideal = list(range(1, max(lP + cP + lT + cT) + 1))
    for axes, (lx, ly, cx, cy, title) in (
            (axMpi, (lP, lMpi, cP, cMpi, "MPI processes")),
            (axOmp, (lT, lOmp, cT, cOmp, "OpenMP threads"))):
        axes.plot(ideal, ideal, "--", color="gray", label="perfect")
        axes.plot(lx, ly, "s-", label="laptop: 2 cores (4 vCPU)")
        axes.plot(cx, cy, "o-", label="cloud: 4 cores (8 vCPU)")
        axes.set_xlabel(title.split()[1])
        axes.set_ylabel("speedup")
        axes.set_title(title)
        axes.legend()
        axes.grid(True, alpha=0.3)
    figure.suptitle("Laptop vs cloud scaling: " + label)
    save(figure, args[2])


def weak(args):
    """work per worker held fixed, so the time should ideally stay flat"""
    workers, nodes, seconds = [], [], []
    with open(args[0]) as file:
        for row in csv.DictReader(file):
            workers.append(int(row["workers"]))
            nodes.append(int(row["nodes"]))
            seconds.append(float(row["seconds"]))
    base = seconds[0]
    efficiency = [base / t for t in seconds]

    figure, (axTime, axEff) = plt.subplots(1, 2, figsize=(12, 5))
    axTime.plot(workers, seconds, "o-", label="measured time")
    axTime.axhline(base, color="k", linestyle="--", label="ideal (flat)")
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

    machine = args[2] if len(args) > 2 else "this machine"
    figure.suptitle(f"Weak scaling on {machine}: {nodes[0] // workers[0]} people per worker")
    save(figure, args[1])
    for i in range(len(workers)):
        print(f"  workers {workers[i]} | people {nodes[i]} | {seconds[i]:.4f}s "
              f"| weak efficiency {efficiency[i]:.2f}")


def timeline(args):
    """when every job ran, and how many different algorithms overlapped"""
    rows = []
    with open(args[0]) as file:
        for row in csv.DictReader(file):
            rows.append((int(row["rank"]), row["task"], float(row["start"]), float(row["end"])))

    names = {"A": "A spread", "B": "B tracing", "C": "C targeting", "D": "D clustering"}
    ranks = sorted(set(r[0] for r in rows))
    figure, axes = plt.subplots(figsize=(12, 1.5 + 0.7 * len(ranks)))
    for rank, task, start, end in rows:
        axes.barh(rank, max(end - start, 0.0005), left=start, height=0.6,
                  color=TASK_COLOURS.get(task, "#999999"))
    axes.set_yticks(ranks)
    axes.set_yticklabels([("master (A)" if r == 0 else f"worker {r}") for r in ranks])
    axes.set_xlabel("time (seconds since start)")
    axes.set_title("task timeline: four different algorithms overlapping (task parallelism)")
    axes.legend(handles=[Patch(facecolor=TASK_COLOURS[k], label=names[k]) for k in "ABCD"],
                loc="upper right", ncol=4)
    save(figure, args[1])

    # sweep a line across time and count how many distinct algorithms are live
    events = []
    for rank, task, start, end in rows:
        events.append((start, 1, task))
        events.append((end, -1, task))
    events.sort()
    active, most, when = {}, 0, 0.0
    for time, change, task in events:
        active[task] = active.get(task, 0) + change
        distinct = sum(1 for k in active if active[k] > 0)
        if distinct > most:
            most, when = distinct, time
    print(f"most different algorithms running at the same time: {most} "
          f"(around t = {round(when, 4)} s)")
    if most >= 2:
        print("=> different algorithms overlapped in time: this is real task parallelism.")


def degrees(args):
    """how many contacts people actually have, in both network shapes"""
    def load(path):
        d, c = [], []
        with open(path) as file:
            for row in csv.DictReader(file):
                d.append(int(row["degree"]))
                c.append(int(row["howManyPeople"]))
        return d, c

    sfD, sfC = load(args[0])
    swD, swC = load(args[1])
    figure, (axLog, axZoom) = plt.subplots(1, 2, figsize=(12, 4.4))
    axLog.loglog(sfD, sfC, "o", markersize=3, color="#E45756", label="scale free")
    axLog.loglog(swD, swC, "s", markersize=5, color="#4C78A8", label="small world")
    axLog.set_xlabel("number of contacts (degree)")
    axLog.set_ylabel("how many people have it")
    axLog.set_title("degree distribution, 500k people (log scales)")
    axLog.grid(True, alpha=0.3, which="both")
    axLog.legend()

    axZoom.bar([d - 0.2 for d in sfD], sfC, width=0.4, color="#E45756", label="scale free")
    axZoom.bar([d + 0.2 for d in swD], swC, width=0.4, color="#4C78A8", label="small world")
    axZoom.set_xlim(0, 20)
    axZoom.set_xlabel("number of contacts (degree)")
    axZoom.set_ylabel("how many people have it")
    axZoom.set_title("the same data, zoomed to 0-20 contacts")
    axZoom.grid(True, alpha=0.3, axis="y")
    axZoom.legend()
    figure.suptitle("Both networks average 6 contacts per person; only the spread differs")
    save(figure, args[2])


def curve(args):
    """the epidemic itself: how many people are in each state, day by day"""
    steps, series = [], {"susceptible": [], "exposed": [], "infectious": [], "recovered": []}
    with open(args[0]) as file:
        for row in csv.DictReader(file):
            steps.append(int(row["step"]))
            for key in series:
                series[key].append(int(row[key]))
    figure = plt.figure(figsize=(9, 5))
    for key in series:
        plt.plot(steps, series[key], label=key)
    plt.xlabel("day")
    plt.ylabel("number of people")
    plt.title("seir epidemic curve (sequential baseline)")
    plt.legend()
    plt.grid(True, alpha=0.3)
    save(figure, args[1])


def save(figure, path):
    figure.tight_layout()
    figure.savefig(path, dpi=130)
    print("wrote", path)


charts = {"grid": grid, "scaling": scaling, "compare": compare, "weak": weak,
          "timeline": timeline, "degrees": degrees, "curve": curve}

if len(sys.argv) < 2 or sys.argv[1] not in charts:
    print("pick one of:", ", ".join(charts))
    print("see the comment at the top of this file for the arguments each one takes")
    sys.exit(1)
charts[sys.argv[1]](sys.argv[2:])
