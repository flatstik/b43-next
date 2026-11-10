#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The bit fields of the PHY and radio registers, from b43's code and traces.

    fields.py --source build/phy_g.i ... --trace BOARD=FILE ... > fields.tsv

Static: every call to the PHY and radio accessors in the preprocessed files,
with the register, the mask and the value where they are constants, and the
function around the call in the original source. The bits each call can
change are its field: ~mask for maskset and mask, the value for set, the
whole word for write. The fields of a register are the atoms of the
partition those bit sets make; a register reached only by whole-word writes
is one atom, marked "word".

Dynamic: the accesses in the traces, each under the innermost PHY function
(FN.IN/FN.OUT) and the checkpoint that follows it. A read followed by a write
of the same register in the same function is a read-modify-write, not a
reader. Accesses inside the table functions are table contents and are
counted per port, not split into fields; so are registers that only a
function computing the register number writes (a loop over a range), which
get one row marked "range".

One row per field: space, register, bits, origin, the static call sites, the
boards and phases it is written in, the writing functions, the values the
field took (shifted down when its bits are contiguous) and the functions that
read the register; "(harness)" is driver.c standing in for the ucode.
"""

import argparse
import collections
import os
import re
import sys

ACCESSOR = re.compile(r"\bb43_(radio|phy)_(read16|write16|read|write|maskset|mask|set)\s*\(")
OPS = {"read": "rd", "read16": "rd", "write": "wr", "write16": "wr",
       "maskset": "maskset", "mask": "mask", "set": "set"}
TABLE_FUNCS = ("b43_ofdmtab_", "b43_gtab_")
ACCESSOR_FUNCS = {"b43_phy_read", "b43_phy_write", "b43_phy_mask", "b43_phy_set",
                  "b43_phy_maskset", "b43_phy_copy", "b43_radio_read", "b43_radio_write",
                  "b43_radio_mask", "b43_radio_set", "b43_radio_maskset"}
CAST = re.compile(r"\(\s*(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:u8|u16|u32|s8|s16|s32|int|short|char|long)\s*\)")
TRACE_OP = re.compile(r"^cpu0 (PHY|RAD)\.(RD|WR)\s+addr=0x([0-9a-f]+) val=0x([0-9a-f]+)")
TRACE_FN = re.compile(r"^cpu0 FN\.(IN|OUT)\s+fn=(\S+)")


def split_args(text, start):
    """The arguments of the call whose '(' is at text[start - 1]."""
    depth, args, cur, i = 1, [], [], start
    while depth:
        ch = text[i]
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if depth == 1 and ch == ",":
            args.append("".join(cur).strip())
            cur = []
        elif depth:
            cur.append(ch)
        i += 1
    args.append("".join(cur).strip())
    return args


def const(expr):
    """The value of a C constant expression, or None."""
    e = CAST.sub("", expr)
    e = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", e)
    if not re.fullmatch(r"[0-9a-fA-FxX\s()|&^~<>+\-*/]*", e) or not e.strip():
        return None
    e = e.replace("/", "//")
    try:
        return eval(e, {"__builtins__": {}}) & 0xFFFF
    except Exception:
        return None


def functions_of(path):
    """Original source line -> the function it is in, kernel style."""
    head = re.compile(r"^(?=\w)(?:[^;(]*?[\s*])?(\w+)\(")
    where, name = {}, None
    for n, line in enumerate(open(path), 1):
        m = None if name else head.match(line)
        if m and not line.rstrip().endswith(";"):
            name = m.group(1)
        if name:
            where[n] = name
            if line.rstrip() == "}":
                name = None
    return where


def static_calls(ipath):
    """The accessor calls of the .c file a .i file was made from."""
    text = open(ipath).read()
    calls, src, line, funcs = [], None, 0, {}
    pos = 0
    for chunk in re.split(r"(?m)^(# \d+ \"[^\"]+\".*)$", text):
        m = re.match(r"# (\d+) \"([^\"]+)\"", chunk)
        if m:
            line, src = int(m.group(1)), m.group(2)
            continue
        if not (src and src.endswith(".c")):
            line += chunk.count("\n")
            continue
        if src not in funcs:
            funcs[src] = functions_of(src) if os.path.exists(src) else {}
        for c in ACCESSOR.finditer(chunk):
            at = line + chunk.count("\n", 0, c.start()) - 1
            args = split_args(chunk, c.end())
            space, op = c.group(1), OPS[c.group(2)]
            reg = const(args[1]) if len(args) > 1 else None
            mask = val = None
            if op == "maskset" and len(args) > 3:
                mask, val = const(args[2]), const(args[3])
            elif op in ("mask", "set", "wr") and len(args) > 2:
                val = const(args[2])
            calls.append({"src": os.path.basename(src), "line": at,
                          "func": funcs[src].get(at, "?"), "space": space,
                          "op": op, "reg": reg, "regexpr": args[1] if len(args) > 1 else "",
                          "mask": mask, "val": val})
        line += chunk.count("\n")
    return calls


def bits_of(call):
    """The bits a call can change, or None if not known."""
    op, mask, val = call["op"], call["mask"], call["val"]
    if op == "wr":
        return 0xFFFF
    if op == "maskset":
        return None if mask is None else (~mask & 0xFFFF) | (val or 0)
    if op == "mask":
        return None if val is None else ~val & 0xFFFF
    if op == "set":
        return val
    return None


def atoms(sets):
    """The partition of the 16 bits that a list of bit sets induces."""
    groups = collections.defaultdict(int)
    for b in range(16):
        key = tuple(bool(s >> b & 1) for s in sets)
        if any(key):
            groups[key] |= 1 << b
    return sorted(groups.values())


def read_trace(path):
    """Per (space, reg): writes and reads with their function and checkpoint."""
    stack, pending, out = [], [], []
    last = {}
    prev = None
    for raw in open(path):
        line = raw.rstrip("\n")
        if line.startswith("# "):
            for a in pending:
                a["phase"] = line[2:]
            pending = []
            continue
        m = TRACE_FN.match(line)
        if m:
            if m.group(1) == "IN":
                stack.append(m.group(2))
            elif stack:
                stack.pop()
            prev = None
            continue
        m = TRACE_OP.match(line)
        if not m:
            continue
        space = "phy" if m.group(1) == "PHY" else "radio"
        reg, val = int(m.group(3), 16), int(m.group(4), 16)
        func = stack[-1] if stack else "(harness)"
        a = {"space": space, "reg": reg, "dir": m.group(2), "val": val,
             "func": func, "table": func.startswith(TABLE_FUNCS),
             "old": last.get((space, reg)), "phase": None, "rmw": False}
        if a["dir"] == "WR" and prev and prev["dir"] == "RD" and \
           (prev["space"], prev["reg"], prev["func"]) == (space, reg, func):
            prev["rmw"] = True
        last[(space, reg)] = val
        out.append(a)
        pending.append(a)
        prev = a
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--source", nargs="+", required=True)
    ap.add_argument("--trace", nargs="+", default=[], metavar="BOARD=FILE")
    args = ap.parse_args()

    calls = [c for i in args.source for c in static_calls(i) if c["func"] not in ACCESSOR_FUNCS]
    traces = {}
    for t in args.trace:
        board, path = t.split("=", 1)
        traces[board] = read_trace(path)

    by_reg = collections.defaultdict(list)
    for c in calls:
        if c["reg"] is not None and not c["func"].startswith(TABLE_FUNCS):
            by_reg[(c["space"], c["reg"])].append(c)
    dyn = collections.defaultdict(list)
    ports = collections.Counter()
    for board, accs in traces.items():
        for a in accs:
            if a["table"]:
                ports[(a["space"], a["reg"], a["dir"])] += 1
            else:
                dyn[(a["space"], a["reg"])].append((board, a))

    # Functions that write a register they compute, as a loop over a range
    # does: what they write there is a table in register space.
    computed = {c["func"] for c in calls if c["reg"] is None and c["op"] != "rd"}

    w = sys.stdout.write
    w("space\treg\tbits\torigin\tstatic\tboards\tphases\twriters\tvalues\treaders\n")
    for key in sorted(set(by_reg) | set(dyn)):
        space, reg = key
        sc, dc = by_reg.get(key, []), dyn.get(key, [])
        sets = [b for b in map(bits_of, sc) if b is not None]
        partial = [b for c, b in zip(sc, map(bits_of, sc)) if b is not None and c["op"] != "wr"]
        written = [a for _, a in dc if a["dir"] == "WR"]
        changed = 0
        for a in written:
            changed |= a["val"] if a["old"] is None else a["val"] ^ a["old"]
        fields = atoms(partial) if partial else []
        covered = 0
        for f in fields:
            covered |= f
        rest = (0xFFFF if any(c["op"] == "wr" for c in sc) or not sc else 0) & ~covered
        rows = [(f, "mask") for f in fields] + ([(rest, "word")] if rest else [])
        if not sc and written and {a["func"] for a in written} <= computed:
            rows = [(0xFFFF, "range")]
        if not rows and sc:
            rows = [(0xFFFF, "read")]
        site_bits = collections.defaultdict(int)
        for c in sc:
            site_bits[c["func"]] |= bits_of(c) or 0

        def reach(a):
            # The bits the call sites of the writing function can change;
            # without one there (a register passed in or computed), the bits
            # the write changed.
            if site_bits.get(a["func"]):
                return site_bits[a["func"]]
            return 0xFFFF if a["old"] is None else a["val"] ^ a["old"]

        readers = sorted({c["func"] for c in sc if c["op"] == "rd"} |
                         {a["func"] for _, a in dc if a["dir"] == "RD" and not a["rmw"]})
        for bits, origin in rows:
            site = sorted({f"{c['func']}:{c['line']}:{c['op']}" for c in sc
                           if (bits_of(c) or 0) & bits})
            hit = [(b, a) for b, a in dc if a["dir"] == "WR" and reach(a) & bits]
            boards = sorted({b for b, _ in hit})
            phases = sorted({a["phase"] or "?" for _, a in hit})
            writers = sorted({a["func"] for _, a in hit})
            shift = (bits & -bits).bit_length() - 1
            if (bits >> shift) & ((bits >> shift) + 1):
                shift = 0
            values = sorted({(a["val"] & bits) >> shift for _, a in hit})
            vtext = ",".join(f"0x{v:x}" for v in values[:12]) + (f",...({len(values)})" if len(values) > 12 else "")
            if origin == "word" and written:
                origin += f" changed=0x{changed & bits:04x}"
            w(f"{space}\t0x{reg:03x}\t0x{bits:04x}\t{origin}\t{' '.join(site)}\t"
              f"{','.join(boards)}\t{'|'.join(phases)}\t{' '.join(writers)}\t{vtext}\t{' '.join(readers)}\n")

    unresolved = [c for c in calls if c["reg"] is None and not c["func"].startswith(TABLE_FUNCS)]
    sys.stderr.write(f"fields.py: {len(calls)} calls, {len(unresolved)} with a register that is not a constant\n")
    for c in unresolved:
        sys.stderr.write(f"  {c['src']}:{c['line']} {c['func']} {c['space']} {c['op']} {c['regexpr']}\n")
    for (space, reg, d), n in sorted(ports.items()):
        sys.stderr.write(f"  table port {space} 0x{reg:03x} {d}: {n}\n")


if __name__ == "__main__":
    main()
