#!/usr/bin/env python3
"""Maps a MIYOO_PROF profile (miyoo/platform/miyoo_prof.c) to functions of Quake II's binaries.
After redriver2-miyoo's miyoo/tools/prof_map.py; added: the code in shared libraries
(ref_soft.so, baseq2/game.so, libc...), found through the profile's "# map" lines.

    py -3 miyoo/tools/prof_map.py <profile> [--build build/miyoo] [--top N] [--callers N]

--build: the folder with the unstripped builds the profile was taken with (quake2.unstripped,
ref_soft.so.unstripped, baseq2/game.so.unstripped, from miyoo/build/build_miyoo.sh). Code in
other libraries (libc, libm, the MI libraries) is counted per library.
Prints: samples per thread (each thread is sampled on its own CPU time: ~1 sample per 10 ms
tick on the device), the functions taking the most samples (all threads, then the game's main
thread), and for the top functions who called them (the lr: exact for leaf functions)."""

import argparse
import bisect
import collections
import os
import shutil
import subprocess
import sys

NM = "arm-none-linux-gnueabihf-nm"
TC = "G:/Emulacja/redriver2-miyoo/deps/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-linux-gnueabihf/bin"


def find_nm():
    nm = shutil.which(NM)
    if nm:
        return nm
    p = os.path.join(TC, NM + ".exe")
    if os.path.exists(p):
        return p
    sys.exit("no " + NM)


def load_symbols(path, nm):
    out = subprocess.run([nm, "-n", "--defined-only", path], capture_output=True, text=True, check=True).stdout
    addrs, names = [], []
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3 or parts[1] not in "tTwW":
            continue
        addrs.append(int(parts[0], 16))
        names.append(parts[2])
    return addrs, names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("profile")
    ap.add_argument("--build", default="build/miyoo")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--callers", type=int, default=8)
    a = ap.parse_args()
    nm = find_nm()

    maps, threads, samples = [], {}, []
    header = ""
    for line in open(a.profile):
        if line.startswith("# map "):
            _, _, s, e, off, path = line.split(" ", 5)
            maps.append((int(s, 16), int(e, 16), int(off, 16), path.strip()))
        elif line.startswith("# thread "):
            _, _, tid, name = line.rstrip("\n").split(" ", 3)
            threads[int(tid)] = name
        elif line.startswith("#"):
            header = line.strip()
        elif line.strip():
            pc, lr, tid, n = line.split()
            samples.append((int(pc, 16), int(lr, 16), int(tid), int(n)))
    print(header)

    # the unstripped builds, by the mapped file's name
    local = {"quake2": "quake2.unstripped", "ref_soft.so": "ref_soft.so.unstripped",
             "game.so": os.path.join("baseq2", "game.so.unstripped")}
    syms = {}
    for base, f in local.items():
        p = os.path.join(a.build, f)
        if os.path.exists(p):
            syms[base] = load_symbols(p, nm)

    cache = {}

    def name_of(addr):
        if addr in cache:
            return cache[addr]
        r = "?"
        for s, e, off, path in maps:
            if s <= addr < e:
                base = os.path.basename(path)
                if base in syms:
                    # the executable isn't position independent: its addresses are its own;
                    # a library's are its load address + the offset in the file
                    v = addr if base == "quake2" else addr - s + off
                    addrs, names = syms[base]
                    i = bisect.bisect_right(addrs, v) - 1
                    r = f"{names[i]} [{base}]" if i >= 0 else f"? [{base}]"
                else:
                    r = f"[{base}]"
                break
        cache[addr] = r
        return r

    total = sum(n for *_, n in samples)
    per_thread = collections.Counter()
    for pc, lr, tid, n in samples:
        per_thread[tid] += n
    print(f"{total} samples")
    for tid, n in per_thread.most_common():
        print(f"  thread {tid} {threads.get(tid, '?')}: {n} ({100.0 * n / total:.1f} %)")

    def table(title, pred):
        c = collections.Counter()
        callers = collections.defaultdict(collections.Counter)
        sub = 0
        for pc, lr, tid, n in samples:
            if not pred(tid):
                continue
            f = name_of(pc)
            c[f] += n
            callers[f][name_of(lr)] += n
            sub += n
        print(f"\n{title}: {sub} samples")
        for f, n in c.most_common(a.top):
            print(f"  {100.0 * n / max(sub, 1):5.1f} % {n:6d}  {f}")
        if a.callers:
            print(f"\n{title}, callers of the top {a.callers}:")
            for f, _ in c.most_common(a.callers):
                print(f"  {f}: " + ", ".join(f"{g} {n}" for g, n in callers[f].most_common(4)))

    table("all threads", lambda tid: True)
    main_tid = max(per_thread, key=per_thread.get)
    table(f"thread {main_tid} ({threads.get(main_tid, '?')})", lambda tid: tid == main_tid)


if __name__ == "__main__":
    main()
