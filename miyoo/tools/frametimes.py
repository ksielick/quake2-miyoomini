"""Frame time statistics for stutter checks: the files from bench.sh FRAMES=1
(MIYOO_FRAMES, one frame time in microseconds per line).

    py -3 miyoo/tools/frametimes.py [--skip N] file...

Per file: frames, mean, median, p90, p99, p99.9, the longest, and the frames over 50 and
100 ms. The first N frames (default 50: the level loads) are left out.
"""
import argparse
import os


def stats(path, skip):
    with open(path) as f:
        t = [int(x) / 1000.0 for x in f.read().split()][skip:]
    if not t:
        return None
    s = sorted(t)

    def pct(p):
        return s[min(len(s) - 1, int(p / 100.0 * len(s)))]

    return (len(t), sum(t) / len(t), pct(50), pct(90), pct(99), pct(99.9), s[-1],
            sum(1 for x in t if x > 50), sum(1 for x in t if x > 100))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--skip', type=int, default=50)
    ap.add_argument('files', nargs='+')
    a = ap.parse_args()
    print('%-28s %6s %6s %6s %6s %6s %6s %7s %5s %5s' % (
        'file', 'frames', 'mean', 'p50', 'p90', 'p99', 'p99.9', 'max', '>50', '>100'))
    for p in a.files:
        r = stats(p, a.skip)
        if r is None:
            print('%-28s (empty)' % os.path.basename(p))
            continue
        print('%-28s %6d %6.1f %6.1f %6.1f %6.1f %6.1f %7.1f %5d %5d' % (
            (os.path.basename(p),) + r))


if __name__ == '__main__':
    main()
