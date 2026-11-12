# test/phy_ht — the HT-PHY against wl's bus

Compiles `phy_ht.c`, `tables_phy_ht.c`, `radio_2059.c` and `phy_common.c`
as they are, against the kernel headers (as `../integration` does), puts a
BCM4331 on a bus that turns their MMIO and agent accesses into the ops
`mmio2ops.py` prints, serves every read from the capture and compares the
two streams op by op.

```sh
make K=... KG=...        # the kernel headers, see ../integration/README.md
./gates.sh --min-block 8 --gap-tol 2   # PHY init to BSS up, scored
./check.sh               # the TX power control init, op for op
./ht-trace FLOW OPS FIRST LAST [-v | -o TRACE]
```

## From the PHY init to the BSS up

`gates.sh` scores the cold 20 MHz captures as `../unit/gates.sh` does the
AC-PHY's: the window runs from wl's first PHY operation after `up` to the
end of `bss up`, b43 runs `b43_phy_init()` on the capture's channel (flow
`full`) with the window as read oracle, and `../unit/cmp_skip.py --bus`
scores the two; it needs `$TRACE_TOOLS` for `tracelib`. The line to quote
is `grezzo`, with its two parameters.

On 2026-11-12, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| cold-2g1-20 | 4392/49020 = 8.96% | 4413/26236 = 16.82% |
| cold-2g6-20 | 4392/39898 = 11.01% | 4413/26243 = 16.82% |
| cold-2g11-20 | 4392/44894 = 9.78% | 4413/26228 = 16.83% |

The second row compares the PHY's and radio's operations only: the core's
MMIO, shared memory and wrapper accesses are outside a harness that
compiles no core. `full` makes the PHY calls of b43's core in its order:
the analog switch of `b43_wireless_core_reset()` and of
`b43_phy_bringup()`, then `b43_phy_init()`.

b43 emits 4801 ops, wl 40 to 49 thousand: wl initialises the PHY again at
`ssid`, with its calibrations, and b43 has neither. The calls b43's PHY makes
into its core (MAC suspend, shared memory, PLL) are counted on stderr but
emit no ops here. What is left is mapped in `../../docs/ht-retrace-todo.md`.

`check.sh` takes `$TRACES/bcm4331-rev2/r6400v1`: the board values from its
`nvram.txt`, the windows from its `traces/mmio-*`. A window runs from the
first table 0x1a read of `b43_phy_ht_tx_power_fix()` that is followed by the
control turned off, to the write that turns it on: the tail of
`b43_phy_ht_op_init()`, flow `pctl_init`. The cold captures have one in `up`
and one in `ssid`, the hot ones only on their first channel.

A flow passes only if both streams are equal, no read goes past the capture
and none of the capture's is left over, and b43 printed nothing.

All 14 windows pass.

## Exceptions

Compared by class and address only (`value_free[]` in `trace.c`):

- `TXPCTL_TARG_PWR` and `TXPCTL_TARG_PWR2`: b43 sets the target from
  `maxp2ga*` (0x60); wl writes 0x20 in the first init and its own target
  later (0x5a at 20 MHz, 0x56 at 40), from a computation not yet known.

## Flows

`pctl_init`, and its pieces for the windows of `cold-2g6-20` `up`:
`power_fix` (#5238–5270), `pctl_off` (#5271–5273), `idle_tssi`
(#5274–5497), `tssi_setup` (#5727–5735), `pctl_on` (#5736–5737).
