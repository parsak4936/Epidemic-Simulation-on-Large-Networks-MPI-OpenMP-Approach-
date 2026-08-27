# turns the raw timings into the numbers the report quotes. two analyses, pick with the
# first argument:
#
#   python scripts/analyse.py modes   results/<machine>/combinedModesRaw.csv [label]
#       medians, spreads and speedups for every process/thread split, plus a warning when
#       the best hybrid split is not separated from the best pure one. also writes the
#       csv the charts read and two latex tables.
#
#   python scripts/analyse.py confirm results/<machine>/confirmRaw.csv
#       the closer look at that one comparison: how often the hybrid split won head to
#       head, and a permutation test for whether a gap that size could be chance.
import sys, csv, os, random, itertools, collections, statistics as st
from collections import defaultdict


def modes(args):
    path = args[0] if args else 'results/local/combinedModesRaw.csv'
    label = args[1] if len(args) > 1 else path.split('/')[-2]

    runs, comms = defaultdict(list), defaultdict(list)
    for row in csv.DictReader(open(path)):
        key = (int(row['procs']), int(row['threads']))
        runs[key].append(float(row['seconds']))
        comms[key].append(float(row['comm']))

    base = st.median(runs[(1, 1)])
    nrep = len(runs[(1, 1)])


    def approach(p, t):
        if p == 1 and t == 1: return 'sequential'
        if p == 1:            return 'pure OpenMP'
        if t == 1:            return 'pure MPI'
        return 'hybrid'


    print(f'\n{label}: median of {nrep} runs per configuration, sequential baseline {base:.3f} s\n')
    print(f"{'workers':>7} {'split':>7} {'approach':<14} {'median':>8} {'min':>7} {'max':>7} "
          f"{'spread':>7} {'speedup':>8} {'comm%':>6}")
    print('-' * 80)

    rows = []
    last = 1
    for (p, t) in sorted(runs, key=lambda k: (k[0] * k[1], k[0])):
        v = runs[(p, t)]
        w = p * t
        if w != last:
            print()
            last = w
        med = st.median(v)
        spread = (max(v) - min(v)) / min(v) * 100
        sp = base / med
        cm = st.median(comms[(p, t)])
        rows.append((w, p, t, approach(p, t), med, min(v), max(v), spread, sp, cm))
        print(f'{w:>7} {p:>3}x{t:<3} {approach(p,t):<14} {med:>8.3f} {min(v):>7.3f} {max(v):>7.3f} '
              f'{spread:>6.0f}% {sp:>7.2f}x {cm:>5.0f}%')

    # is the best mixed configuration genuinely ahead of the best pure one?
    print('\n' + '=' * 80)
    best_of = {}
    for w, p, t, ap, med, lo, hi, spread, sp, cm in rows:
        if ap == 'sequential':
            continue
        if ap not in best_of or sp > best_of[ap][0]:
            best_of[ap] = (sp, f'{p}x{t}', med, lo, hi)

    for ap in ('pure MPI', 'pure OpenMP', 'hybrid'):
        if ap in best_of:
            sp, cfg, med, lo, hi = best_of[ap]
            print(f'  best {ap:<14} {sp:>5.2f}x  ({cfg}, median {med:.3f} s, range {lo:.3f}-{hi:.3f})')

    if 'hybrid' in best_of:
        csp, ccfg, cmed, clo, chi = best_of['hybrid']
        rival = max((best_of[a] for a in ('pure MPI', 'pure OpenMP') if a in best_of),
                    key=lambda x: x[0])
        rsp, rcfg, rmed, rlo, rhi = rival
        margin = (rmed - cmed) / cmed * 100
        print(f'\n  the hybrid split {ccfg} is {margin:.0f}% faster than the best '
              f'pure one ({rcfg})')
        # the test that actually decides it: do the two ranges overlap?
        if chi < rlo:
            print('  every run of the hybrid split beat every run of the other: the '
                  'difference is larger than the noise')
        else:
            print(f'  WARNING: the ranges overlap ({clo:.3f}-{chi:.3f} against '
                  f'{rlo:.3f}-{rhi:.3f}), so this margin is not separated from run to run '
                  f'variation. Report it with the spread, or repeat more.')
    print()

    # the plotting script wants a small csv of one line per configuration, so write that too
    outdir = os.path.dirname(path) or '.'
    with open(os.path.join(outdir, 'combinedModes.csv'), 'w', newline='') as f:
        f.write('mode,processes,threads,seconds,speedup,comm\n')
        for w, p, t, ap, med, lo, hi, spread, sp, cm in rows:
            mode = 'sequential' if ap == 'sequential' else 'hybrid'
            f.write(f'{mode},{p},{t},{med:.5f},{sp:.2f},{cm:.0f}\n')

    # and a latex ready table so the report and the csv can never drift apart
    with open(os.path.join(outdir, 'combinedModesTable.tex'), 'w') as f:
        for w, p, t, ap, med, lo, hi, spread, sp, cm in rows:
            if ap == 'sequential':
                continue
            f.write(f'{w} & ${p} \\times {t}$ & {ap} & {med:.3f} & {lo:.3f}--{hi:.3f} & '
                    f'{spread:.0f}\\% & {sp:.2f} & {cm:.0f}\\% \\\\\n')

    # The strong scaling table is the subset of this grid where only one mechanism is turned up,
    # taken from the same runs rather than a separate sweep, so the two tables cannot disagree.
    with open(os.path.join(outdir, 'strongScalingTable.tex'), 'w') as f:
        f.write(f'sequential & 1 & 1 & {base:.3f} & 1.00 & 0\\% \\\\\n')
        for w, p, t, ap, med, lo, hi, spread, sp, cm in rows:
            if ap == 'OpenMP alone':
                f.write(f'OpenMP & {p} & {t} & {med:.3f} & {sp:.2f} & {cm:.0f}\\% \\\\\n')
        for w, p, t, ap, med, lo, hi, spread, sp, cm in rows:
            if ap == 'MPI alone':
                f.write(f'MPI & {p} & {t} & {med:.3f} & {sp:.2f} & {cm:.0f}\\% \\\\\n')

    print(f'wrote {outdir}/combinedModes.csv, combinedModesTable.tex, strongScalingTable.tex')



def confirm(args):
    path = args[0] if args else 'results/cloud/confirmRaw.csv'

    runs = collections.defaultdict(list)
    for row in csv.DictReader(open(path)):
        runs[(int(row['procs']), int(row['threads']))].append(float(row['seconds']))

    base = st.median(runs[(1, 1)])
    mixed, procs = (4, 2), (8, 1)
    a, b = runs[mixed], runs[procs]
    na = f'{mixed[0]}x{mixed[1]}'
    nb = f'{procs[0]}x{procs[1]}'

    print(f'\nrepeats kept: {len(a)} per configuration')
    print(f'sequential baseline: {base:.4f} s '
          f'(range {min(runs[(1,1)]):.4f}-{max(runs[(1,1)]):.4f})\n')

    for name, v in ((na, a), (nb, b)):
        print(f'  {name:<5} median {st.median(v):.4f}  mean {st.mean(v):.4f}  '
              f'range {min(v):.4f}-{max(v):.4f}  speedup {base/st.median(v):.2f}x')

    wins = sum((x < y) + 0.5 * (x == y) for x, y in itertools.product(a, b))
    total = len(a) * len(b)
    gap = (st.median(b) - st.median(a)) / st.median(a) * 100
    print(f'\n  {na} is {gap:.0f}% faster than {nb} on the medians')
    print(f'  {na} won {wins:.0f} of {total} head to head pairings ({wins/total*100:.0f}%)')

    # permutation test: shuffle the labels many times and see how often chance alone produces a
    # gap at least this big
    observed = st.median(b) - st.median(a)
    pool = a + b
    hits, trials = 0, 200000
    rng = random.Random(12345)
    for _ in range(trials):
        rng.shuffle(pool)
        if st.median(pool[len(a):]) - st.median(pool[:len(a)]) >= observed:
            hits += 1
    p = (hits + 1) / (trials + 1)

    print(f'\n  permutation test over {trials:,} shuffles: p = {p:.4f}')
    if p < 0.01:
        print(f'   the gap is far larger than chance would produce. {na} is genuinely faster.')
    elif p < 0.05:
        print(f'   the gap is larger than chance would usually produce. {na} is faster.')
    else:
        print(f'   a gap this size could still come from chance at this number of repeats.')
        print(f'     Report {na} as at least matching {nb}, not as beating it.')
    print()


what = {'modes': modes, 'confirm': confirm}
if len(sys.argv) < 2 or sys.argv[1] not in what:
    print('usage: python scripts/analyse.py [modes|confirm] <rawCsv> [label]')
    sys.exit(1)
what[sys.argv[1]](sys.argv[2:])
