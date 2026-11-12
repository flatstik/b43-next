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
	# wl's 1 s watchdog turns in a window: each reads MMIO 0x4d0 once,
	# which nothing else in the captures reads.
	turns() {
		awk -v a="$1" -v z="$2" '{ n = substr($2, 2) + 0 }
			n >= a && n <= z && /REG\.RD off=0x04d0/ { k++ }
			END { print k + 0 }' "$ops"
	}
	echo "== $c, ops $first:$last, channel $ch"
	HT_CHANNEL=$ch HT_WD_TURNS=$(turns "$first" "$last") \
		./ht-trace full "$ops" "$first" "$last" -o "$T/$c.trace"
	python3 ../unit/cmp_skip.py "$ops" "$T/$c.trace" "$first:$last" \
		--board r6400v1 --bus $B | grep -A2 '^grezzo'
	grep -E ' (PHY|RAD)\.' "$ops" > "$T/$c.phy.ops"
	grep -E ' (PHY|RAD)\.' "$T/$c.trace" > "$T/$c.phy.trace"
	python3 ../unit/cmp_skip.py "$T/$c.phy.ops" "$T/$c.phy.trace" \
		"$first:$last" --board r6400v1 --bus $B | grep -A2 '^grezzo' |
		sed '1s/^grezzo   /PHY+radio/'

	# The start of the BSS: wl's init at `ssid`, b43's b43_ht_phy_recycle(),
	# with what the up left read off the up.
	a=$(awk "/MARK 'ssid'/ { print substr(\$2, 2) + 1; exit }" "$ops")
	z=$(awk "/MARK 'beaconing'/ { print substr(\$2, 2) - 1; exit }" "$ops")
	rc() { awk -v r="RAD.RD addr=$1 " 'index($0, r) { print $NF; exit }' "$ops" | sed 's/val=//'; }
	t25=$(awk '/PHY\.WR addr=0x0072 val=0x94/ { print "0x" substr($NF, 9); exit }' "$ops")
	echo "== $c, ap start, ops $a:$z"
	HT_CHANNEL=$ch HT_WD_TURNS=$(turns "$a" "$z") \
		HT_RCCAL0=$(rc 0x034c) HT_RCCAL1=$(rc 0x034d) \
		HT_RCCAL2=$(rc 0x034e) HT_T25=$t25 \
		./ht-trace ap_start "$ops" "$a" "$z" -o "$T/$c.ap.trace" 2>/dev/null
	grep -E ' (PHY|RAD)\.' "$T/$c.ap.trace" > "$T/$c.ap.phy.trace"
	python3 ../unit/cmp_skip.py "$T/$c.phy.ops" "$T/$c.ap.phy.trace" \
		"$a:$z" --board r6400v1 --bus $B | grep -A2 '^grezzo' |
		sed '1s/^grezzo   /PHY+radio/'
done
