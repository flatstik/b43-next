#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The prior hypotheses of every field, from fields.tsv and a priors module.

    hypotheses.py --fields fields.tsv --source build/*.i --trace BOARD=FILE ... \\
                  [--priors priors_g] > hypotheses.tsv

One row per (field, block, role) with its normalized weight and the sources
that gave it, strongest first within a field. The rules and their weights
are the priors module's; see its docstring.
"""

import argparse
import collections
import csv
import importlib
import re
import sys

import fields as F


def split(pairs, weight):
    return [(b, r, weight / len(pairs)) for b, r in pairs]


def strip(e):
    e = e.strip()
    while e.startswith("(") and e.endswith(")"):
        depth = 0
        for i, ch in enumerate(e):
            depth += ch == "("
            depth -= ch == ")"
            if depth == 0 and i < len(e) - 1:
                return e
        e = e[1:-1].strip()
    return e


def terms(expr):
    """The terms of a value OR-ed together, each with its mask if it has one."""
    out, depth, cur = [], 0, ""
    for ch in strip(expr):
        depth += ch == "("
        depth -= ch == ")"
        if ch == "|" and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    result = []
    for t in map(strip, out):
        m = re.fullmatch(r"(.*)&\s*(0[xX][0-9a-fA-F]+|\d+)", t, re.S)
        result.append((t, int(m.group(2), 0) if m and F.const(m.group(2)) is not None else None))
    return result


def placements(calls, patterns):
    """(space, reg, pattern) -> the bits a source pattern is seen written to."""
    seen = collections.defaultdict(int)
    for c in calls:
        if c["op"] == "rd" or c["reg"] is None:
            continue
        site = F.bits_of(c) or 0
        whole = terms(c["valexpr"])
        for t, mask in whole:
            bits = mask if mask is not None else (site if len(whole) == 1 and c["op"] == "maskset" else 0)
            for pat in patterns:
                if bits and re.search(pat, t):
                    seen[(c["space"], c["reg"], pat)] |= bits
    return seen


def phase_of(func, phases):
    for name, pat in phases.items():
        if re.search(pat, func):
            return name
    return None


def range_shape(trace_accs, regs):
    """'monotone', 'symmetric' or None for the last values of a range."""
    last = {}
    for a in trace_accs:
        if a["dir"] == "WR" and a["space"] == "phy" and a["reg"] in regs:
            last[a["reg"]] = a["val"]
    vals = [last[r] for r in sorted(last)]
    if len(vals) < 4 or len(set(vals)) < 3:
        return None
    steps = [b - a for a, b in zip(vals, vals[1:])]
    if all(s >= 0 for s in steps) or all(s <= 0 for s in steps):
        return "monotone"
    if vals == vals[::-1]:
        return "symmetric"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fields", required=True)
    ap.add_argument("--source", nargs="+", required=True)
    ap.add_argument("--trace", nargs="+", default=[], metavar="BOARD=FILE")
    ap.add_argument("--priors", default="priors_g")
    args = ap.parse_args()
    P = importlib.import_module(args.priors)

    sites = {(c["func"], c["line"]): c for i in args.source for c in F.static_calls(i)}
    reads = collections.defaultdict(list)
    for c in sites.values():
        if c["op"] == "rd" and c["reg"] is not None:
            reads[(c["space"], c["reg"])].append(c)
    patterns = [pat for pat, _ in P.ARITHMETIC]
    placed = placements(sites.values(), patterns)
    traces = [F.read_trace(t.split("=", 1)[1]) for t in args.trace]
    rows = list(csv.DictReader(open(args.fields), delimiter="\t"))

    # The registers of each range, grouped by the function that writes them.
    ranges = collections.defaultdict(set)
    for r in rows:
        if r["origin"] == "range":
            ranges[r["writers"]].add(int(r["reg"], 16))
    shapes = {}
    for writer, regs in ranges.items():
        for accs in traces:
            shape = range_shape(accs, regs)
            if shape:
                shapes[writer] = shape
                break

    w = sys.stdout.write
    w("space\treg\tbits\torigin\tblock\trole\tweight\tsources\n")
    counts = collections.Counter()
    for r in rows:
        hyp = collections.defaultdict(float)
        why = collections.defaultdict(set)

        def add(pairs, source, weight):
            for b, role, x in split(pairs, weight):
                hyp[(b, role)] += x
                why[(b, role)].add(source)

        calls = [sites[(t.split(":")[0], int(t.split(":")[1]))]
                 for t in r["static"].split() if (t.split(":")[0], int(t.split(":")[1])) in sites]
        writes = [c for c in calls if c["op"] != "rd"]
        if r["origin"] == "range":
            writes = [c for c in sites.values() if c["reg"] is None and c["op"] != "rd"
                      and c["func"] in r["writers"].split()]

        # A term reaches the field through its mask, else through where the
        # same quantity is seen written in this register, else through the
        # bits the call can change.
        bits = int(r["bits"], 16)
        s1 = []
        for c in writes:
            for t, mask in terms(c["valexpr"]):
                for pat, pairs in P.ARITHMETIC:
                    if not re.search(pat, t):
                        continue
                    reach = mask if mask is not None else \
                        placed.get((c["space"], c["reg"], pat)) or F.bits_of(c) or 0
                    if reach & bits and pairs not in s1:
                        s1.append(pairs)
        if not writes:
            for c in reads.get((r["space"], int(r["reg"], 16)), []):
                for pat, pairs in P.READS:
                    if c["lhs"] and re.search(pat, c["lhs"]) and pairs not in s1:
                        s1.append(pairs)
        for pairs in s1:
            add(pairs, "S1", P.WEIGHT["S1"] / len(s1))

        names = {c["regname"] for c in calls if c["regname"]}
        s2 = []
        for name in sorted(names):
            for pat, pairs in P.NAMES:
                if re.search(pat, name) and pairs not in s2:
                    s2.append(pairs)
                    break
        for pairs in s2:
            add(pairs, "S2", P.WEIGHT["S2"] / len(s2))

        writers = r["writers"].split() or sorted({c["func"] for c in writes})
        phases = {phase_of(f, P.PHASES) for f in writers}
        if len(phases) == 1 and None not in phases:
            phase = phases.pop()
            constant = all(c["val"] is not None for c in writes)
            if phase != "init" or constant:
                add(P.PHASE_PAIRS[phase], "S3", P.WEIGHT["S3"])

        if r["origin"] == "range":
            if shapes.get(r["writers"]) == "monotone":
                add(P.MONOTONE_PAIRS, "S4", P.WEIGHT["S4"])
        else:
            nvals = len(r["values"].split(",")) if r["values"] else 0
            m = re.search(r"\.\.\.\((\d+)\)", r["values"])
            if m:
                nvals = int(m.group(1))
            if nvals >= P.SWEEP_VALUES and len(writers) == 1:
                add(P.SWEEP_PAIRS, "S4", P.WEIGHT["S4"])

        add([("unknown", "unknown")], "S0", P.WEIGHT["S0"])

        total = sum(hyp.values())
        best = sorted(hyp.items(), key=lambda kv: -kv[1])
        for (b, role), x in best:
            w(f"{r['space']}\t{r['reg']}\t{r['bits']}\t{r['origin'].split()[0]}\t{b}\t{role}\t"
              f"{x / total:.3f}\t{','.join(sorted(why[(b, role)]))}\n")
        counts["fields"] += 1
        counts["only base" if len(hyp) == 1 else "with sources"] += 1
        for src in {s for v in why.values() for s in v}:
            counts[src] += 1
    sys.stderr.write("hypotheses.py: " + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())) + "\n")


if __name__ == "__main__":
    main()
