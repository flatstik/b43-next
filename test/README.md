# Tests

Two suites measure the driver; they measure different things and neither
replaces the other.

- **`unit/`** stands in for b43: `unit/main.c` calls the PHY entry points in an
  order it chooses, with stub structures, and compares the output with a stock
  capture. It measures the AC-PHY files of `b43-next/` and produces the quotable
  number. What b43 emits from elsewhere (`MAC.*`, the core's `OBJ.*`,
  `TPL.RAMW`, the address match table) is mirrored in `unit/main.c` or declared
  in `unit/compare.py`. See `unit/README.md`.
- **`integration/`** does not stand in for b43. The order of events in b43
  cannot be read off the source (dozens of gates on `core_rev`, `fw.rev` and
  `phy->type` with free `else` branches), so this suite compiles the tree's
  b43 and bcma core management, stubs the layer below, and lets it drive
  itself. See `integration/README.md`.

**`phy_ht/`** compiles the HT-PHY files and compares the TX power control
init with the R6400 v1's `wl-mmio-trap` captures; see `phy_ht/README.md`.

Two smaller ones check one piece each: **`radar/`** replays recorded and
synthetic radar FIFO reads through `b43-next/radar_ac.c`, and **`sprom/`**
diffs the SROM rev 11 extractor and the NVRAM path against the boards' dumps.

`board_profile.h` holds the board profiles the suites take.

## What lives elsewhere

The captures, their decoders and b43-tools are not in this repository.
`paths.sh`, `paths.mk` and `paths.py` say where the scripts, the Makefiles and
the Python tools find them, from the environment or next to this repository:

| variable | default | what |
|---|---|---|
| `TRACES` | `../b43-next-traces` | the captures, one directory per board |
| `TRACE_TOOLS` | `../b43-next-trace-tools` | the capture decoders (`tracelib.py`, `mmio2ops.py`, ...) |
| `B43_TOOLS` | `../b43-tools` | b43-tools, for the virtual chip |
