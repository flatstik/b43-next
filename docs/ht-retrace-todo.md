# HT-PHY retrace: what is left

The gate is `test/phy_ht/gates.sh --min-block 8 --gap-tol 2`, against the
R6400 v1's cold captures (BCM4331, wl 7.14.164.18). Numbers here are from
`cold-2g6-20`, window #173-#39861.

## Where the window's operations are

| segment | wl ops | matched by b43 |
|---|---|---|
| `up` | 8559 | 4449 |
| `ssid` | 26827 | 115 |
| `bss-up` | 4303 | 0 |

b43 runs one `b43_phy_init()`, 4801 ops, and 4361 of them are in blocks
the score counts. Everything left is code b43 does not have.

## `up`

The unmatched runs of at least 20 wl ops:

| ops | what |
|---|---|
| #173-#697 | the core before the PHY: MMIO writes, shared memory, the ucode's bulk writes |
| #962-#992 | wrapper and chipcommon |
| #1011-#1122 | PHY writes after a read of 0x541 |
| #1156-#1527 | table writes from table 2 entry 0x20 on, radio reads in between |
| #5738-#9669 | after the TX power control: the core (1540 MMIO reads, 876 shared memory writes) and 346 PHY writes |

In order, PHY and radio only (`compare.py --bus` on the two streams
filtered to `PHY.*` and `RAD.*`), b43 follows wl up to the end of the
channel setup's power fix, op 274 of the window. There wl sets up the gain
control (#1011-#1182 on ch 6, 170 PHY and radio operations): registers
0x541, 0x280/0x283, 0x4x4, 0x40d-0x417 and 0x410/0x411 per core, the
entries 0x00-0x29 of the gain tables 0x00-0x03, 0x28 and 0x29, the groups
of table 8, a run of reads of gain registers, tables 0 and 4 and the
radio, and read-modify-writes of 0x273, 0x276 and 0x279. It is the same on
every capture but for one table 0x25 write, whose offset depends on the
channel and the width (0x009 on ch 6, 0x019 on ch 1, 0x03a on ch 11) by a
rule not yet known. b43 has pieces of it in `b43_phy_ht_op_init()`, after
the tables, with other values.

The PHY's calls into the core (`b43_mac_suspend()`, the shared memory,
`b43_mac_switch_freq()`, the PLL) emit nothing in the harness: on the AC
`test/unit/main.c` mirrors them, here they are only counted.

## `ssid`

wl initialises the PHY for the channel again, without the radio init of
`up`, then calibrates:

- the gain tables (0x00, 0x01, 0x28, 0x02, 0x03, 0x29), tables 0x08, 0x25,
  0x04, the power fix (table 0x1a 0xe8, 0x07, 0x0d) three times;
- the TX power control init, the chain `test/phy_ht/check.sh` passes, four
  times (#10982, #12770, #13664, #25138);
- two passes of a tone calibration, #15344-#23859 and #26788-#35294, each 12
  tone loads into table 0x11 with their playback and measurement.

The tones are exactly those of the N-PHY's `b43_nphy_gen_load_samples()`:
160 samples at 20 MHz, 2500 kHz at amplitude 250 and 2000 kHz at 181, all
26 loads of the segment.

## `bss-up`

Mostly the core: 2117 MMIO reads, 976 writes, 614 shared memory accesses;
546 PHY accesses.
