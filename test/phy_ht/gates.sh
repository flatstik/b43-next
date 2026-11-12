#!/bin/sh
# Scores b43's HT-PHY against the R6400 v1's cold captures, from the PHY init
# through `wl up`, as ../unit/gates.sh does for the AC-PHY. The window runs from
# wl's first PHY operation after the `up` mark to the `ssid` mark: what wl
# does when the BSS starts is not part of b43's up. b43 runs b43_phy_init()
# on the capture's channel, its reads served from the window, and
# ../unit/cmp_skip.py --bus scores the two streams.
#
#   ./gates.sh --min-block 8 --gap-tol 2 [CAPTURE...]
#
# CAPTURE is a name under traces/mmio-cold, e.g. cold-2g6-20; without, the
# three 20 MHz ones (b43's HT-PHY runs at 20 MHz only). GATE_TMP keeps the
# window's trace and b43's. Each capture gets two rows: all the operations,
# and the PHY's and radio's only, the perimeter of this harness, which
# compiles no core.
set -u
REPO=$(cd "$(dirname "$0")/../.." && pwd)
. "$REPO/test/paths.sh"
cd "$(dirname "$0")"

B=
while [ $# -gt 0 ]; do
	case $1 in
	--min-block|--gap-tol) B="$B $1 $2"; shift 2 ;;
	*) break ;;
	esac
done
[ -n "$B" ] || { echo "usage: $0 --min-block N --gap-tol M [CAPTURE...]"; exit 2; }

BOARD=$TRACES/bcm4331-rev2/r6400v1
export HT_NVRAM=$BOARD/nvram.txt
T=${GATE_TMP:-$(mktemp -d)}
mkdir -p "$T"
[ -n "${GATE_TMP:-}" ] || trap 'rm -rf "$T"' EXIT

[ $# -gt 0 ] || set -- cold-2g1-20 cold-2g6-20 cold-2g11-20
for c in "$@"; do
	ops=$T/$c.ops
	zcat "$BOARD/traces/mmio-cold/$c-ops.txt.gz" > "$ops"
	first=$(awk "/MARK 'up'/ { u = 1; next } u && / PHY\./ { print substr(\$2, 2); exit }" "$ops")
	last=$(awk "/MARK 'ssid'/ { print substr(\$2, 2) - 1; exit }" "$ops")
	ch=$(echo "$c" | sed 's/cold-2g\([0-9]*\).*/\1/')
	echo "== $c, ops $first:$last, channel $ch"
	HT_CHANNEL=$ch ./ht-trace full "$ops" "$first" "$last" -o "$T/$c.trace"
	python3 ../unit/cmp_skip.py "$ops" "$T/$c.trace" "$first:$last" \
		--board r6400v1 --bus $B | grep -A2 '^grezzo'
	grep -E ' (PHY|RAD)\.' "$ops" > "$T/$c.phy.ops"
	grep -E ' (PHY|RAD)\.' "$T/$c.trace" > "$T/$c.phy.trace"
	python3 ../unit/cmp_skip.py "$T/$c.phy.ops" "$T/$c.phy.trace" \
		"$first:$last" --board r6400v1 --bus $B | grep -A2 '^grezzo' |
		sed '1s/^grezzo   /PHY+radio/'
done
