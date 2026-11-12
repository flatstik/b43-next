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

## From the PHY init through up

`gates.sh` scores the cold 20 MHz captures as `../unit/gates.sh` does the
AC-PHY's: the window runs from wl's first PHY operation after `up` to the
`ssid` mark, b43 runs its up on the capture's channel (flow `full`) with
the window as read oracle, and `../unit/cmp_skip.py --bus` scores the two;
it needs `$TRACE_TOOLS` for `tracelib`. The line to quote is `grezzo`,
with its two parameters. What wl does when the BSS starts, at `ssid` and
`bss up`, is not part of b43's up and is not in the window.

On 2026-11-12, `--min-block 8 --gap-tol 2`:

| capture | grezzo | PHY+radio |
|---|---|---|
| cold-2g1-20 | 5334/8498 = 62.77% | 5350/5382 = 99.41% |
| cold-2g6-20 | 5334/8619 = 61.89% | 5350/5382 = 99.41% |
| cold-2g11-20 | 5334/8498 = 62.77% | 5350/5382 = 99.41% |

The second row compares the PHY's and radio's operations only: the core's
MMIO, shared memory and wrapper accesses are outside a harness that
compiles no core, and its calls into the core are counted on stderr. `full`
makes the PHY calls of b43's core in its order: the analog switch of
`b43_wireless_core_reset()` and of `b43_phy_bringup()`, `b43_phy_init()`,
then the TX power check of mac80211's first config and the adjustment it
queues. What is left is mapped in `../../docs/ht-retrace-todo.md`.

`check.sh` takes `$TRACES/bcm4331-rev2/r6400v1`: the board values from its
`nvram.txt`, the windows from its `traces/mmio-*`. A window runs from the
first table 0x1a read of `b43_phy_ht_tx_power_fix()` that is followed by the
control turned off, to the write that turns it on: the tail of
`b43_phy_ht_op_init()`, flow `pctl_init`. The cold captures have one in `up`
and one in `ssid`, the hot ones only on their first channel.

A flow passes only if both streams are equal, no read goes past the capture
and none of the capture's is left over, and b43 printed nothing.

All 14 windows pass.

## A later init

`up_hot` runs the `full` flow with `do_full_init` clear, with the RC
calibration's results and the table 0x25 pair of the cold up given
(`HT_RCCAL0`-`2`, `HT_T25`; on `cold-2g6-20` 0xb, 0xc, 0xe and 0x9). Against
wl's `ssid` segment, which is that init, PHY and radio only, it follows wl
in order to the target power of the TX power control setup (op 1294).
What follows there, wl's calibrations, b43 does not have.

```sh
HT_NVRAM=... HT_CHANNEL=6 HT_RCCAL0=0xb HT_RCCAL1=0xc HT_RCCAL2=0xe HT_T25=0x9 \
    ./ht-trace up_hot OPS 8733 35558 -o hot.trace
```

## Exceptions

Compared by class and address only (`value_free[]` in `trace.c`):

- `TXPCTL_TARG_PWR` and `TXPCTL_TARG_PWR2`: b43 sets the target from
  `maxp2ga*` (0x60) capped by the regulatory and user limits; wl writes
  0x20 in the first init and its own target later (0x5a at 20 MHz, 0x56
  at 40), from a computation not yet known.

## Flows

`pctl_init`, and its pieces for the windows of `cold-2g6-20` `up`:
`power_fix` (#5238–5270), `pctl_off` (#5271–5273), `idle_tssi`
(#5274–5497), `tssi_setup` (#5727–5735), `pctl_on` (#5736–5737).
