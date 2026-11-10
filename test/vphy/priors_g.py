# SPDX-License-Identifier: GPL-2.0-only
"""The prior hypotheses for the fields of the G-PHY and the radio 2050.

Written before any measurement; hypotheses.py applies them and nothing in
them may change once the experiment of step 4 has started.

A hypothesis is a (block, role) pair. The blocks are those of the signal
chain read from the driver:

    DAC -> baseband attenuator -> mixer -> RF attenuator, PAD, PA -> antenna
            LO compensation ---^                                       |
    tone generator -> DAC                                    TR switch |
    detector <- ADC <- RX filter <- PGA <- LNA <-----------------------+
    NRSSI <- log detector <----------^             TSSI <- PA output

plus the synthesizer, the RF/analog override ("override"), power-down
("power"), the antenna switch, the PHY's digital receiver ("digital": CRS,
AGC, classifier) and "configuration" for what is set once and never looked
at again. "unknown" stands for a block not named by any source.

The sources and their weight:

  S1  arithmetic   3.0   what the written value is computed from
  S2  names        2.0   the B43_PHY_ name of the register (none for the 2050)
  S3  phase        1.0   in which functions the field is written
  S4  shape        1.0   how the values the field takes are spread
  S0  base         0.5   (unknown, unknown), on every field

A source that matches several pairs splits its weight among them. The
weights of a field are then normalized to 1. Analogy with other families
(brcmsmac) gives nothing for this radio and is left out.
"""

BLOCKS = ("DAC", "baseband attenuator", "mixer", "LO compensation", "RF attenuator",
          "PAD", "PA", "TR switch", "antenna switch", "LNA", "PGA", "RX filter",
          "ADC", "detector", "NRSSI", "TSSI", "synthesizer", "tone generator",
          "override", "power", "digital", "configuration", "unknown")
ROLES = ("selector", "gain", "offset", "filter", "enable", "tuning", "unknown")

WEIGHT = {"S1": 3.0, "S2": 2.0, "S3": 1.0, "S4": 1.0, "S0": 0.5}

# S1: identifiers in the value a call site writes, as b43 computes them.
ARITHMETIC = [
    (r"\brfatt\b|\brf\b|\brf->att\b|\brfatt->att\b|\bradio_attenuation\b", [("RF attenuator", "gain")]),
    (r"\bbbatt\b|\bbaseband_attenuation\b", [("baseband attenuator", "gain")]),
    (r"\btx_control\b|\btxctl\w*|\bwith_padmix\b|\bpad_mix_gain\b", [("PA", "gain"), ("PAD", "gain")]),
    (r"\btx_bias\b", [("mixer", "offset")]),
    (r"\btx_magn\b", [("mixer", "gain")]),
    (r"\bctl\.[iq]\b|\bloctl\b|\bdc_lt\b", [("LO compensation", "offset")]),
    (r"\blna\w*\b", [("LNA", "gain")]),
    (r"\bpga\w*\b", [("PGA", "gain")]),
    (r"\btrsw\w*\b", [("TR switch", "selector")]),
    (r"\bmax_lb_gain\b|\blb_gain\b", [("LNA", "gain"), ("PGA", "gain")]),
    (r"\bchannel\w*\b|\bfreq\b", [("synthesizer", "tuning")]),
    (r"\bnrssi\w*\b", [("NRSSI", "offset")]),
    (r"\btssi\w*\b", [("TSSI", "offset")]),
    (r"\binitval\b", [("RX filter", "filter")]),
    (r"\bantenna\b|\bautodiv\b", [("antenna switch", "selector")]),
]

# S1 for a register b43 only reads: what it stores the value in.
READS = [
    (r"feedth", [("detector", "unknown")]),
    (r"nrssi", [("NRSSI", "unknown")]),
    (r"tssi", [("TSSI", "unknown")]),
]

# S2: the register's name.
NAMES = [
    (r"^B43_PHY_LO_CTL$|^B43_PHY_G_LO_CONTROL$", [("LO compensation", "offset")]),
    (r"^B43_PHY_LO_MASK$", [("LO compensation", "enable")]),
    (r"^B43_PHY_RFOVER$", [("override", "enable")]),
    (r"^B43_PHY_RFOVERVAL$", [("LNA", "gain"), ("PGA", "gain"), ("TR switch", "selector"),
                              ("power", "enable")]),
    (r"^B43_PHY_ANALOGOVER$", [("override", "enable")]),
    (r"^B43_PHY_ANALOGOVERVAL$", [("DAC", "enable"), ("ADC", "enable")]),
    (r"^B43_PHY_PGACTL$", [("PGA", "gain")]),
    (r"^B43_PHY_LNAHPFCTL$", [("LNA", "gain"), ("RX filter", "filter")]),
    (r"^B43_PHY_LPFGAINCTL$", [("RX filter", "filter"), ("RX filter", "gain")]),
    (r"^B43_PHY_DACCTL$", [("baseband attenuator", "gain"), ("DAC", "unknown")]),
    (r"^B43_PHY_ITSSI$|^B43_PHY_HPWR_TSSICTL$", [("TSSI", "unknown")]),
    (r"^B43_PHY_NRSSILT_", [("NRSSI", "offset")]),
    (r"^B43_PHY_LO_LEAKAGE$", [("detector", "unknown")]),
    (r"^B43_PHY_PWRDOWN$", [("power", "enable")]),
    (r"^B43_PHY_(CCK)?BBANDCFG$|^B43_PHY_ANT", [("antenna switch", "selector")]),
    (r"GAIN", [("digital", "gain")]),
    (r"^B43_PHY_(G_)?CRS|^B43_PHY_CLASSCTL$|^B43_PHY_SYNCCTL$|^B43_PHY_ENCORE$|"
     r"^B43_PHY_PEAK_COUNT$|^B43_PHY_LMS$|^B43_PHY_DIVSRCH|^B43_PHY_CCKSHIFTBITS|"
     r"^B43_PHY_OFDM61$|^B43_PHY_OFDM9B$|^B43_PHY_ADIVRELATED$", [("digital", "unknown")]),
]

# S3: what the functions that write a field are for.
PHASES = {
    "calibration": r"^b43_calc_loopback_gain$|^lo_|^b43_calibrate_lo_setting$|"
                   r"^b43_calc_nrssi|^b43_nrssi_|^b43_gphy_aci_",
    "rx filter calibration": r"^b43_radio_init2050$",
    "tx power": r"^b43_set_txpower_g$|^b43_gphy_set_baseband_attenuation$|^b43_phy_init_pctl$|"
                r"^b43_hardware_pctl_|^b43_gphy_op_adjust_txpower$",
    "tssi table": r"^b43_gphy_tssi_power_lt_init$",
    "channel": r"^b43_gphy_channel_switch$|^b43_synth_pu_workaround$",
    "rfkill": r"^b43_gphy_op_software_rfkill$",
    "interference": r"^b43_radio_interference_mitigation_",
    "antenna": r"^b43_gphy_op_set_rx_antenna$",
    "lo table": r"^b43_gphy_dc_lt_init$",
    "gain table": r"^b43_gphy_gain_lt_init$|^b43_set_all_gains$|^b43_set_original_gains$",
    "init": r"^b43_phy_init(b5|b6|g|a)$|^b43_wa_|^b43_gphy_op_prepare|^b43_gphy_init_",
}

# A field written only in functions of one phase gets these pairs.
PHASE_PAIRS = {
    "calibration": [("override", "enable"), ("override", "selector")],
    "rx filter calibration": [("RX filter", "filter")],
    "tx power": [("PA", "gain"), ("RF attenuator", "gain"), ("baseband attenuator", "gain")],
    "channel": [("synthesizer", "tuning")],
    "rfkill": [("power", "enable")],
    "interference": [("digital", "gain"), ("digital", "unknown")],
    "antenna": [("antenna switch", "selector")],
    "lo table": [("LO compensation", "offset")],
    "tssi table": [("TSSI", "unknown")],
    "gain table": [("LNA", "gain"), ("PGA", "gain")],
    "init": [("configuration", "unknown")],
}

# S4: a field that one function sets to this many values or more, as a loop
# does, is a parameter the driver searches.
SWEEP_VALUES = 4
SWEEP_PAIRS = [("unknown", "tuning"), ("unknown", "filter"), ("unknown", "offset")]
# A range whose final contents rise or fall with the register number.
MONOTONE_PAIRS = [("unknown", "gain")]
