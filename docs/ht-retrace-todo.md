# HT-PHY retrace: what is left

The gate is `test/phy_ht/gates.sh --min-block 8 --gap-tol 2`, against the
R6400 v1's cold captures (BCM4331, wl 7.14.164.18). Numbers here are from
`cold-2g6-20`, window #173-#8731.

## Where the operations are

`test/phy_ht/gates.sh` scores `up`, from wl's first PHY operation after the
`up` mark to the `ssid` mark: on `cold-2g6-20`, 8559 of wl's operations, of
which 5382 are the PHY's and the radio's. `ssid` (26827) and `bss-up`
(4303) are what wl does when the BSS starts, b43's start of the AP, not
part of its up.

## `up`

Of the operations outside the PHY and the radio, the larger unmatched
runs are the core's, which this harness does not compile:

| ops | what |
|---|---|
| #173-#697 | the core before the PHY: MMIO writes, shared memory, the ucode's bulk writes |
| #962-#992 | wrapper and chipcommon |
| #5738-#8731 | after the TX power control: MMIO reads, shared memory |

In order, PHY and radio only, b43's operations of the cold ch 6 `up`
are one block, 5350 of wl's 5382 (99.41%), six values apart: the target
power, by design (`test/phy_ht/README.md`, "Exceptions"). The 20 left are
wl's during the ten idle seconds after `wl up`, between MAC suspends:
a read-modify-write of 0x280/0x283 (#8131), a read of table 13 entry 0x5f
(#8520), and, after a core reset, the analog switch and the radio off
(#8590-#8613), wl's power saving while no BSS is up. b43 runs nothing
there; on the AC the harness plays such periods from the capture's
timeline.

The PHY's calls into the core (`b43_mac_suspend()`, the shared memory,
`b43_mac_switch_freq()`, the PLL) emit nothing in the harness: on the AC
`test/unit/main.c` mirrors them, here they are only counted.

## `ssid`

b43 runs the later init when beaconing starts (`b43_ht_phy_recycle()`;
flow `ap_start` in `test/phy_ht`), in wl's order up to the TX power control.
What follows in wl is missing:

- the TX power recalculation of `b43_phy_ht_op_adjust_txpower()`, two or
  three times per start of the BSS, at times set by wl's events, not by
  the init: a timeline is needed to place them, as on the AC;
- two passes of calibration (#15069-#24098 and #26530-#35530 on ch 6).
  Each: the TX power control off with its indices saved, an idle TSSI
  measurement as `b43_phy_ht_tx_power_ctl_idle_tssi()`, table 0x1a's late
  part again, then
  - a TX I/Q and LO calibration (#15765-#17663): tone 2500 kHz at 250,
    the I/Q LO commands at 0x0c2 and the coefficients in table 13;
  - an RX I/Q calibration, twelve steps (#17939-#23999): tone 2000 kHz at
    181, playback, the estimator at 0x129, table 13;
  - the control on again.

  The steps are those of the N-PHY's rev 3+ calibrations in b43
  (`b43_nphy_cal_tx_iq_lo()`, `b43_nphy_rev3_cal_rx_iq()`) and in brcmsmac,
  which are where the port starts from.

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
