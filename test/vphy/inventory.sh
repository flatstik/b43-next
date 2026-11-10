#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# The G-PHY boards and options the inventory runs, and the inventory itself.
#
#     VCHIPD=.../interpreter/vchipd.py ./inventory.sh [OUT]
#
# Each line: name, core revision, PHY version word, radio identity word, the
# board's options. The variants turn on the branches b43 takes on board flags
# (PACTRL, FEM, EXTLNA, ALTIQ; RSSI) and on hardware power control.
set -e
: "${VCHIPD:?VCHIPD=path/to/vchipd.py}"
OUT=${1:-build/inventory}
STEPS=channels,power,periodic,rfkill,interf,antenna
mkdir -p "$OUT"

traces=
while read -r name corerev phyver radio opts; do
	case $name in ''|\#*) continue ;; esac
	env B43_CORE_REV=$corerev B43_STEPS=$STEPS $opts \
		./b43phy --trace "$OUT/$name.trace" -- \
		python3 "$VCHIPD" --corerev "$corerev" --phy-ver "$phyver" \
		--radio-id 0x2050 --radio 1="$radio" > "$OUT/$name.log" 2>&1 ||
		echo "inventory.sh: $name failed, see $OUT/$name.log" >&2
	traces="$traces $name=$OUT/$name.trace"
done <<EOF
4306r1		5	0x1201	0x2205017F	B43_CHIP_ID=0x4306
4306		5	0x2202	0x2205017F	B43_CHIP_ID=0x4306
4306-flags	5	0x2202	0x2205017F	B43_CHIP_ID=0x4306 B43_BOARDFLAGS=0x9802
4306-rssi	5	0x2202	0x2205017F	B43_CHIP_ID=0x4306 B43_BOARDFLAGS=0x0008
4318		9	0x3207	0x8205017F	B43_CHIP_ID=0x4318
4318-flags	9	0x3207	0x8205017F	B43_CHIP_ID=0x4318 B43_BOARDFLAGS=0x9802
4318-hwpctl	9	0x3207	0x8205017F	B43_CHIP_ID=0x4318 B43_HWPCTL=1
4311		10	0x4208	0x2205017F	B43_CHIP_ID=0x4311
4311-flags	10	0x4208	0x2205017F	B43_CHIP_ID=0x4311 B43_BOARDFLAGS=0x9802
4311-rssi	10	0x4208	0x2205017F	B43_CHIP_ID=0x4311 B43_BOARDFLAGS=0x0008
4311-hwpctl	10	0x4208	0x2205017F	B43_CHIP_ID=0x4311 B43_HWPCTL=1
EOF

python3 fields.py --source build/*.i --trace $traces > "$OUT/fields.tsv" 2> "$OUT/fields.log"
