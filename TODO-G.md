# G-PHY open items

What is suspected in the G-PHY code (`phy_g.c`, `lo.c`, `wa.c`, and what
`main.c` and `sysfs.c` do for it) and not yet settled, one item per entry.
Closed items are not kept.

The evidence is the code itself, the reverse-engineered specs at
bcm-v4.sipsolutions.net (an archive of 2016) and b43legacy, which has the
same routines from an older reading of the specs. None of it is a capture:
every item stands until a trace of wl, or the card, says otherwise.

## Whether an entry applies

Each entry starts with the condition it applies under. On a running machine
the terms of those conditions are read like this:

- PHY and radio revision: the `Found PHY: Analog .., Type 2 (G), Revision ..`
  and `Found Radio: .., ID 0x2050, Revision ..` lines of the kernel log.
- Boardflags: the `ssb_sprom` attribute of the PCI device. On SPROM revisions
  1 to 3 the low word is at offset 0x72.
- Hardware power control: `/sys/module/b43/parameters/hwpctl`, 0 unless the
  module was loaded with `hwpctl=1`.
- Interference mode: the `interference` attribute of the device, 0 after
  every init.
- LO state: `loctls` in the device's debugfs directory. It gives the TX bias
  and every calibrated setting with its attenuations.
- Any PHY register: `mmio16write` and `mmio16read` in the same directory,
  through MMIO 0x3FC (PHY address) and 0x3FE (PHY data).

## Suspects

An entry that opens with *Changed, to confirm* has its fix in the tree, made
by the commit that marked it. Revert that commit if the capture disagrees;
delete the entry when it agrees.

### TX bias unknown after a quick re-init

Applies: any board, when the core is initialised again within 176 s
(`B43_LO_TXCTL_EXPIRE`) of the last TX control measurement; an interface down
and up is enough. `loctls` then shows `TX Bias: 0xFF`.

Changed, to confirm: `lo_measure_setup()` measures the TX control values
whenever the bias is `0xFF`, so `b43_phy_initg()` always has a measured one.
`b43_gphy_op_prepare_structs()` still resets it at every init; Init/G in the
specs measures when it is not set.

Check: `loctls` right after the interface comes back up. In a capture, what wl
leaves in radio 0x52 and PHY 0x36 after a second init.

### Current LO calibration looked up under two keys

Applies: the TX mixer gain is on (`tx_control & 0x10`) and the RF attenuation
is not 1, 2 or 3: radio 2050 revision 8 at nearly every power level. `loctls`
lists the settings as `RF(4,1)` while another attenuation is in use.

`b43_lo_g_adjust()` looks the setting up, and calibrates it, with the RF
attenuation of `b43_lo_fixup_rfatt()`: 4 for a padmix attenuation outside 1-3.
`b43_lo_g_maintenance_work()` compares and recalibrates with the attenuation
as it is: it does not see the current setting expire, and what it calibrates
when the list is empty is stored where the adjust does not look. LO/Adjust in
the specs takes the value for the required attenuation and has no such remap.

Check: radio 0x43 during wl's LO measurement, on a card with radio revision 8
and software power control; `loctls` on the same card.

### T/R switch value of the LO measurement

Applies: every G-PHY of revision 2 or higher, and revision 1 in G mode, at
each LO calibration.

Changed, to confirm: `lo_measure_gain_values()` sets `trsw_rx` to `0x20` when
the RX gain wanted covers the gain of the switch and to 0 otherwise, as
MeasuringGains in the specs, and the feedthrough is measured with it. Below
the gain of the switch the whole gain goes to the LNA and the PGA. The
assertion of `lo_measure_feedthrough()` is back.

Check: PHY 0x812 during wl's LO measurement: bit 5 against the gain asked for.

### PGA step of the RF override in the 2050 init

Applies: radio revision other than 8 (`b43_radio_init2050()` is not called for
8), in G mode.

Changed, to confirm: the gain goes down by 6 at each pass and the passes are
counted from 1, up to 16: 6 for a gain of 30. Radio/Init/2050 in the specs
counts them from 0.

Check: bits 8-11 of the values written to PHY 0x812 during the 2050 init,
against the loopback gain measured just before. A gain under 6 gives 1 if the
count starts at 1.

### OFDM 0xC3 on OFDM PHY revision 3

Applies: `PHY 0x400 & 0xFF` is 3. To read it, write `0x3FC 0x0 0x400` to
`mmio16write`, `0x3FE` to `mmio16read`, then read `mmio16read`.

`b43_phy_initg()` writes `0x8006` for revisions 3 and 5. Init/G in the specs
and b43legacy write `0x8606` for 3.

Check: PHY 0x4C3 after init.

### PHY 0x47E outside revisions 1 and 2

Applies: revision 3 or higher.

Changed, to confirm: `0x78` is written on revisions 1 and 2 in G mode only.
Init/G in the specs names revision 2.

Check: whether PHY 0x47E is written during init.

### NRSSI table on boards without BFL_RSSI

Applies: boardflags bit `0x0008` is clear, at every init.

`b43_phy_initg()` calls `b43_nrssi_hw_update(dev, 0xFFFF)` and its comment
says that floors every entry at -32. In 16 bits it adds 1 to every entry
instead. The specs (PHY/G/NRSSI) take the entries as 6 bit signed values,
subtract the delta and clamp; the delta the comment quotes from them is
`0x7FFFFFFF`.

Check: the 64 writes to PHY 0x804 during init.

### Upper clamp of the TSSI to dBm table

Applies: the PA parameters are set in the SPROM, so the table is generated,
and an entry reaches 32 dBm. The three SPROM words in the formula of
`b43_tssi2dbm_entry()` tell, or `B43_PA0B0..2` in `test/vphy`.

Changed, to confirm: the clamp is 127; 128 stored in the `s8` read -128.

Check: the arithmetic is enough. What wl clamps to shows in PHY 0x380-0x39F
with hardware power control.

### CRS threshold on revision 1

Applies: G-PHY revision 1.

`b43_wa_crs_thr()` clears bits 6-9 of PHY 0x429 and ORs `0xD000` in, outside
the field. The specs (PHY/A/workarounds/CRS threshold) set `0x340` under the
same mask.

Check: PHY 0x429 after init.

### CCK shiftbits value on revision 1

Applies: G-PHY revision 1.

`b43_wa_cck_shiftbits()` writes `0x26` to PHY 0x4A7. The specs
(PHY/A/workarounds/CCK shiftbits) write `0x1A`, which is 26 in decimal.

Check: PHY 0x4A7 after init.

### Sigma square table on revision 2

Applies: G-PHY revision 2.

`b43_wa_msst()` uploads `b43_tab_sigmasqr2` on every G-PHY. The specs
(PHY/A/workarounds/minimum sigma square table) upload the first table on
revision 2. Nothing uses `b43_tab_sigmasqr1` any more.

Check: the first value written to OFDM table 0x14 during init: `0x007A` is the
first table, `0x00DE` the second.

### Interference stack and the OFDM table entries

Applies: PHY revision 3 or higher, when `2` and then another mode are written
to the `interference` attribute. The kernel log shows the warning of
`_stack_save()` at the first write and of `_stack_restore()` at the second.

Changed, to confirm: 13 bits of offset and 3 of id, as b43legacy, so the two
OFDM table entries (`0x1A02`, `0x1A03`) are found again.

Check: the two warnings are gone.

### Interference mode on a device that is down

Applies: any G-PHY, when `interference` is written while the interface is
down.

Changed, to confirm: the write fails with `-ENODEV` unless the device is
initialised.

Check: the write returns the error. Every init sets the mode back to 0 anyway.

### Bits left by the non-WLAN mode on revision 1

Applies: G-PHY revision 1, after `1` and then another mode are written to
`interference`.

The enable sets bit 12 of PHY 0x401; the disable clears bit 11, and only
without bad frames preemption, and sets bit 14 of PHY 0x429, which the enable
did not clear. b43legacy does the same.

Check: PHY 0x401 and PHY 0x429 before the mode and after it is left.

### Signed RSSI in the ACI detection

Applies: never today. `b43_gphy_aci_scan()` is only called from a branch of
`b43_gphy_op_pwork_15sec()` that is disabled with `0 /*TODO*/`.

`b43_gphy_aci_detect()` makes its 6 bit readings signed in `u16` variables: a
negative reading compares above a positive one.

Check: none until the scan is enabled. The sign test is `> 32` here and `>=
0x20` everywhere else.

### ACI scan on channel 14

Applies: never today. `b43_gphy_aci_scan()` is only called from a branch of
`b43_gphy_op_pwork_15sec()` that is disabled with `0 /*TODO*/`.

`b43_gphy_aci_scan()` returns `ret[channel - 1]` from an array of 13: on
channel 14 it reads past it.

Check: none until the scan is enabled.

### Spread of an ACI detection

Applies: never today. `b43_gphy_aci_scan()` is only called from a branch of
`b43_gphy_op_pwork_15sec()` that is disabled with `0 /*TODO*/`.

After the scan each channel found busy marks the four above it, and the loop
goes upward over what it has just marked: every channel above the first hit
ends up marked, none below. A hit above the current channel never counts.

Check: none until the scan is enabled.

### RX diversity bit with a fixed antenna

Applies: never today: only `B43_ANTENNA_DEFAULT` is passed.

Changed, to confirm: the second write is gone. The first already sets or
clears the bit.

Check: PHY 0x48C after a fixed antenna is selected.

### Order of the DC table

Applies: `hwpctl=1` (`/sys/module/b43/parameters/hwpctl`) and PHY revision 6
or higher.

`b43_gphy_dc_lt_init()` takes entry `i` as baseband `i / rfatt_len`, RF `i %
rfatt_len`. `b43_gphy_gain_lt_init()` and the specs (PHY/G/DC lookup table)
run the RF list outside and the baseband list inside. With 7 by 9 values entry
63 reads a tenth baseband attenuation.

Check: PHY 0x3A0-0x3BF against PHY 0x3C0-0x3FF in a capture with hardware
power control.

### I and Q in the DC table

Applies: `hwpctl=1` (`/sys/module/b43/parameters/hwpctl`) and PHY revision 6
or higher.

A negative Q is cast to `u8` and covers the nibble of I. Which nibble is which
is the `FIXME` of the code, and the values go to 16 while a nibble holds 4
bits.

Check: PHY 0x3A0-0x3BF in the same capture.

### Words of the TSSI power table

Applies: `hwpctl=1` (`/sys/module/b43/parameters/hwpctl`) and PHY revision 6
or higher.

`b43_gphy_tssi_power_lt_init()` packs two `s8` entries in a word: a negative
low entry sets the high byte to `0xFF`. The 64 single writes to OFDM tables
0x3C00 and 0x3C20 are sign extended to 16 bits as well.

Check: PHY 0x380-0x39F in the same capture.

### Idle TSSI delta

Applies: `hwpctl=1` (`/sys/module/b43/parameters/hwpctl`) and PHY revision 6
or higher.

`b43_hardware_pctl_init_gphy()` ORs target minus current idle TSSI into PHY
0x36 and PHY 0x478: a negative delta sets every bit above the field.

Check: PHY 0x36 and PHY 0x478 in the same capture.

## Hunches

Noticed while reading, with nothing to weigh them against.

### NRSSI of -1000 after a failed slope measurement

`b43_calc_nrssi_slope()` keeps its two readings only when the first is -4 or
more. Otherwise `nrssi[]` stays at the -1000 of the init, and
`b43_nrssi_mem_update()` and `b43_calc_nrssi_threshold()` compute from it.
Applies to boards with BFL_RSSI (`0x0008`) set.

### ACPR host flag after channel 14

`b43_gphy_channel_switch()` sets `B43_HF_ACPR` on channel 14 unless the SPROM
country is Japan, and no other channel clears it.

### External LNA steps against b43legacy

`radio2050_rfover_val()` gives `0x1000` to gains from `0x3A` and `0x2000` to
gains from `0x2E`, as Radio/Init/2050 in the specs; b43legacy has the two the
other way round.

## Markers left in the code

The `TODO`, `FIXME` and `XXX` notes, the code disabled with a constant and
the names that say unknown, by file and function.

`phy_g.c`:

- `b43_calc_nrssi_offset()`, four times, and `b43_calc_loopback_gain()`, four
  times: `Not in specs, but needed to prevent PPC machine check`, on the
  accesses to PHY 0x814 and 0x815 skipped for revision 1.
- `b43_phy_initb6()`: `We had code for these revs here...`, a warning for
  radio revisions 6 and 7.
- `b43_phy_initg()`: `//FIXME?` on `b43_nrssi_hw_update()`; see the NRSSI
  entry.
- `b43_phy_initg()`: `FIXME: The spec says ... 'if OFDM may not be used in
  the current locale'`, with the condition fixed to `|| 0`.
- `b43_gphy_op_prepare_hardware()`: `Workaround: Temporarly disable gmode
  through the early init phase`, for revision 1.
- `b43_gphy_op_pwork_15sec()`: `TODO: update_aci_moving_average`,
  `TODO: not scanning?`, `0 /*TODO: bunch of conditions */`, `0 /*TODO*/`
  (the ACI scan) and `TODO: implement rev1 workaround`.

`phy_g.h`:

- `B43_PHY_PGACTL_LPF`: `Low pass filter (?)`.
- `B43_PHY_PGACTL_UNKNOWN` and `B43_PHY_RFOVERVAL_UNK` (`Unknown, always
  set.`).
- `interfstack`: `FIXME: use a data structure`.
- `initval`: `FIXME rename?`.

`lo.c`:

- `b43_gphy_dc_lt_init()`: `FIXME: Is Q really in the low nibble?`.
- `b43_lo_g_maintenance_work()`: `FIXME Recalc the whole DC table from time
  to time?`.

`wa.c`:

- `b43_wa_rssi_lt()`: `0 /* FIXME: For APHY.rev=2 this might be needed */`.
- `b43_wa_all()`: `XXX review rev1`.

`phy_a.h`, used by the G-PHY:

- `FIXME rename` on `B43_PHY_ADIVRELATED`, `B43_PHY_OFDM61`,
  `B43_PHY_OFDM61_10`, `B43_PHY_OFDM9B`, `B43_PHY_CCKSHIFTBITS_WA` and
  `B43_PHY_CCKSHIFTBITS`.
- `TODO rename` on `B43_OFDMTAB_GAINX`, `B43_OFDMTAB_UNKNOWN_0F`,
  `B43_OFDMTAB_UNKNOWN_APHY` and `B43_OFDMTAB_UNKNOWN_11`.
- `TODO remove!` on `B43_OFDMTAB_GAINX_R1`.

## Not reviewed

Not compared with the specs or with b43legacy: `b43_phy_initb5()` and
`b43_phy_initb6()`, `b43_calc_loopback_gain()`, `b43_calc_nrssi_offset()`,
`b43_wa_altagc()` line by line, the interference mitigation register lists,
`b43_phy_init_pctl()`, and the contents of `tables.c`.
