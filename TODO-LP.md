# LP-PHY open items

What is suspected in `phy_lp.c` and not yet settled, one item per entry, with
the evidence and the check that closes it. Closed items are not kept.

The evidence is the code itself, the reverse-engineered specs at
bcm-v4.sipsolutions.net (an archive of 2016, partly updated to wl 5.10.56.46,
partly older than the code and in places wrong) and brcmsmac's LCN-PHY, a
sibling with the same register block. None of it is a capture: every item
stands until a trace of wl says otherwise.

Three captures cover almost everything:

- BCM4312 (PHY rev 1, radio 2062): init, a channel switch to 1, 7 and 14, one
  periodic calibration. Its SPROM (`rssisav2g`, boardflags) is needed to read
  the result.
- a rev 2 chip (BCM4325, radio 2063): init and a channel switch, with the
  crystal frequency.
- BCM5354 (PHY rev 0): init only.

Hardware TX power control is compiled out (`if (0)` in
`lpphy_tx_pctl_init()`), so the items marked *unreachable* change nothing
until it is enabled.

## Suspects

An entry that opens with *Changed, to confirm* has its fix in the tree, made
by the commit that marked it. Revert that commit if the capture disagrees;
delete the entry when it agrees.

### `rssi_gs` in AFE_RSSI_CTL_0 on rev0/1

Changed, to confirm: `gs` is written. The specs write
`vf | vc << 4 | gs << 10 | 0xA000` (Rev0BasebandInit, steps 25-26).

Check: the write to PHY 0x43D in the BCM4312 init. Only tells if
`rssisav2g` is not 0.

### Mask of the second write to TR_LOOKUP_1

Changed, to confirm: `0xC0FF`, as on the seven writes next to it. The specs
have `0x3F00` (Rev0BasebandInit, step 34.2), taken as a mask that was not
inverted.

Check: PHY 0x44E after init on a rev 1 board with boardflags `0x00400000`
(FEM_BT): low six bits `0x0A` and bits 8-13 `0x09` mean `0xC0FF`.

### When TR lookups 1-4 are copied to 5-8

Changed, to confirm: the copy is made on rev 1 without FEM_BT (`0x00400000`),
as in the specs (step 38), and no longer with PAREF (`0x00040000`).

Check: PHY 0x4C7-0x4CA in the BCM4312 init against 0x44E, 0x44F, 0x4BB,
0x4BC, with the board's flags.

### Which rev1 FEM boards get which TR lookup values

Rev0BasebandInit gives steps 35 and 36 the same last condition (rev 1 and
FEM), so step 36 cannot be reached as written. The code has rev 0 and FEM in
the first, rev 1 or FEM in the second.

Check: PHY 0x44E, 0x44F, 0x4BB, 0x4BC on a rev 1 board with boardflags
`0x0800`, bits 0-5 and 8-13: `0x0401/0x0501/0x0802/0x0A02` is step 35,
`0x0804/0x0C04/0x0102/0x0302` is step 36.

### Flag of the 5354 PR45960 workaround

Changed, to confirm: PHASESHIFT (`0x00100000`) as in the specs, step 39,
instead of FEM_BT.

Check: on a 5354 in the 4712S package, whether PHY 0x4BF = 5 and
0x4BE = 0xFFFF are written, with the board's flags.

### TX filter cap on channel 14

The specs skip the `+ 5` on rev 1 when the channel is 14 (SetRCCap, step 3);
the code adds it on every channel.

Check: radio 0x4F (north) on the BCM4312 tuned to 14 and to another channel:
the low nibble differs by 5 if channel 14 is left out.

### DDFS left running by a failed rev0/1 RC calibration

Code reading only: the failure path skips `lpphy_stop_ddfs()`.

Check: RCCalRev0 in the specs, not read yet. A capture does not show a
failure path.

### Partial `rc_cap` after a failed rev0/1 RC calibration

If the estimate fails after some caps were tried, the best so far is
applied and kept until the next core init. Whether wl applies it or falls
back to the init table is not known.

### Start of the rev2 TX RC calibration

Changed, to confirm, against the letter of the specs: the TX half writes its
`0x7D` to radio 0x5A as the RX half does (step 10), not to 0x52 (RCCalRev2,
step 21).

Check: which radio register gets `0x7D` after the second 0x5D/0x5E pair.

Also open: the poll is 10000 times `msleep(1)`; the specs poll every 10 us
for 10 s at most.

### `tssi_npt` in TX_PWR_CTL_NNUM (unreachable)

Changed, to confirm: mask `0xF8FF`, shift 8, from the specs (SetTXPwrCtrl)
and from the LCN-PHY.

Check: the write to PHY 0x4A5 when wl enters hardware TX power control, if
it does on this chip.

### Initial TX power index override (unreachable)

`-1` means no override in the code, but the state starts zeroed:
`lpphy_pr41573_workaround()` forces index 0. What wl starts from is not known
and a capture does not show it.

### PA gain from the rev2 TX gain table

Changed, to confirm: Set PA Gain is called on rev2+, as in the specs
(SetTXPwrbyIndex, step 14), and no longer on rev0/1.

Check: PHY 0x4FB and 0x4FD written after 0x4B5 when wl sets a power index.

### Layout of the PA gain in PHY 0x4FB

The specs give three layouts (GetPAGain bits 0-6, SetPAGain bits 6-12,
SetTXGain bits 8-14); the code reads bits 0-6 and writes bits 6-12. The
LCN-PHY has bits 8-14 for all three.

Check: the value wl writes to 0x4FB on 2 GHz, where the table's PA gain is
`0x7F`: `0x7Fxx` is bits 8-14, `0x1FC0 | pad` is bits 6-12.

### 2063 PLL words

Changed, to confirm: the words follow Radio/2063ChannelTune and agree with a
separate implementation of it.

Check: radio 0x7B-0x7E, 0x70-0x73, 0x68, 0x69 after a tune. For a 26 MHz
crystal on channel 1 the specs give 0x7B = `0x09`, 0x7C low three bits 5,
0x7D bits 5-6 clear and low nibble 6, 0x7E = `0x57`, 0x70 low five bits 7,
0x71 = `0xBB`, 0x72 = `0x13`, 0x73 = `0xB1`, 0x68 low six bits `0x30` with
bit 6 clear, 0x69 low five bits 6 with bit 5 clear.

Also open in the same function:

- the count: the specs do not subtract 1, brcmsmac's 2064 tune does. 0x7E
  one lower than above means it is subtracted.
- radio 0x81 bit 0: the specs set and clear it with bit 1 (steps 78-79);
  the code only handles bit 1.
- radio 0x75 bit 1: the specs test `value1a > 45` on a Q16 value, always
  true; the code tests `val1 == 45`, never true.
- the channel table has the 12 columns of the older driver; the updated
  specs have 15, other registers, and write PHY 0x4F0/0x4F1 in the tune.

### Field mask in `lpphy_sync_stx()`

Changed, to confirm: only the field is copied. The specs OR the whole
shifted register in; wl's `mod_phy_reg()` cuts the value to the mask.

Check: PHY 0x4F2-0x4F8 after the 2063 init, against the radio registers
0x3D-0x52 read in the same capture.

Missing: the specs put writes of `0xFF` to radio 0x04-0x08 before the loop
and clear them after (SYNCHSTX, steps 1-5 and 7-11); the masks given there do
not agree with each other. The capture shows the real ones.

### TX filter on the 2063

`b43_lpphy_op_switch_channel()` runs the filter setup and the gain table
adjust for the 2062 only; the specs run both for either radio
(SetChanSpecLPPHY, steps 6-7). The rev2 branch of
`lpphy_set_analog_filter()` is the old one (radio 0x55 = `0x3F`) and is never
reached; the updated specs write PHY 0x448, 0x4EA, 0x4EB and a digital
filter instead (TxFilterInit, step 4).

Check: what wl writes after a 2063 tune.

### Overrides around the RX IQ calibration

`lpphy_rx_iq_cal()` sets bit `0x800` in PHY 0x44C and 0x44D and at the end
clears `0x800` in 0x43C, leaving 0x44C/0x44D as set; it clears bit 0 of
0x43B at the start, where the LCN-PHY sets it. All of it is in the specs
(RXIQCal, steps 10-13 and 23-24).

Check: the writes to 0x43B, 0x43C, 0x44C, 0x44D around a noise calibration
(the 0x49E writes mark it), and 0x44C/0x44D after it.

### Overflow in the RX IQ estimate

`ipwr << (20 - prod_msb)` in `lpphy_calc_rx_iq_comp()` leaves 31 bits when
the IQ product is more than about 2^11 times smaller than the I power: the
coefficient is then computed from a wrapped divisor. The specs do the same
in 32 bits.

Check: by computation on `i_pwr`, `q_pwr`, `iq_prod` taken from a capture
(PHY 0x483-0x488 after the estimate), comparing the coefficient wl writes to
0x49E with the one the code would.

### Loop count of the tone

`lpphy_run_samples()` ORs `0xFFFF` into PHY 0x4A0 under a mask that keeps the
top four bits, and `lpphy_stop_tx_tone()` clears the low twelve only. No tone
is played today. RunSamples in the specs not read yet.

### CCK power offsets

`lpphy_read_band_sprom()` fills the four CCK entries of `tx_max_rate` from
`ofdm2gpo`, as the specs do (ReadBandSrom); the LCN-PHY uses `cck2gpo`.
Nothing reads the array.

## Hunches

Noticed while reading, with nothing to weigh them against: no spec page read,
no sibling code, no computation. They are places to look first, not claims.
The `SPEC FIXME` notes already in `phy_lp.c` are not repeated.

### Band data on SDIO

`ssbn_sdio_get_invariants()` fills the SPROM with `0xFF` and reads the MAC,
the PA parameters and the maximum powers from the CIS. `tri2g`, `bxa2g`,
`rxpo2g` and the three `rssi*2g` stay `0xFF`, so on an SDIO chip the rev2
init writes `0xFF` to the low byte of INPUT_PWRDB and `0xFFFF` to
AFE_RSSI_CTL_1. Where wl takes them from there is not known.

### Digital filter state saved twice

`lpphy_save_dig_flt_state()` saves the nine filter registers and loads fixed
coefficients over them. The rev2 init calls it; `lpphy_calibration()` calls
it again and restores at the end. From the second call on, what is saved is
the fixed coefficients: the restore changes nothing and the values of the
init are gone. Whether wl's save loads anything is not known.

### RSSI of received frames

`b43_rx()` sends LP-PHY frames through `b43_rssi_postprocess()`, the G-PHY
conversion of the JSSI byte. Nothing says the LP-PHY reports the same
quantity.

### Crystal entry of the 2062 init

`lpphy_2062_init()` takes the first entry of `freqdata_tab` above the
reference frequency. The table ends at 19200 kHz: the 20 MHz of the BCM4312
and the 25 MHz of the 5354 match nothing and get the last entry from the
fallback.

### Width of the RX gain table read

`lpphy_set_rx_gain_by_index()` reads table 12 as 16 bits;
`lpphy_rev2plus_set_rx_gain()` uses bits 16-21 of the gain. Only the rev0/1
RC calibration calls it today.

### TR isolation below 26

`lpphy_adjust_gain_table()` computes `(isolation - 26) / 12` and shifts it
by 12 into a u16. An isolation under 26 goes negative; `0xFF` gives 19 and
the shift drops its top bit.

### Arithmetic of the rev0/1 RC calibration

The squared error is kept in 32 bits and wraps when a tone measures more than
about 16 times the first. If the first tone measures 0, every cap scores the
same and 128 is taken.

### Values wider than their field, elsewhere

The pattern of `lpphy_sync_stx()` (a value ORed in under a mask narrower
than it) is also in `lpphy_run_ddfs()`, `lpphy_set_dac_gain()`,
`lpphy_set_pa_gain()` and in the `bx_arch` write of the rev0/1 baseband
init. The values fit with the callers of today and a programmed SPROM.

## Not reviewed

Not compared with the specs or with anything else: the rev2 baseband init,
the 2062 and 2063 radio init, the rev0/1 RC calibration and the loopback
search, `lpphy_disable_crs()` and its restore, the software rfkill,
`lpphy_calibration()` and the digital filter save and restore around it,
`lpphy_pr41573_workaround()`, `lpphy_tx_pctl_init_hw()`, and the tables in
`tables_lpphy.c`.
