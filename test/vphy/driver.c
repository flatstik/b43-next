// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * b43's PHY code on the virtual chip.
 *
 * The PHY files of b43 are compiled as they are; main_part.c is what they
 * call of main.c, cut out of it by extract.py. What is written here is the
 * rest of a kernel around them: the bus, whose accesses go to the chip, time,
 * which the chip is told of, and the few functions of main.c that need a
 * running ucode or a bus reset, which this harness has not.
 *
 * drive() does what b43_wireless_core_init() does to a PHY, then the steps
 * B43_STEPS asks for, and at each checkpoint prints what the driver holds:
 *
 *   channels   b43_switch_channel() over a few channels
 *   sweep      b43_switch_channel() over the fourteen of 2.4 GHz
 *   power      on a G-PHY: frames, each followed by b43_phy_txpower_check();
 *              the TSSI the ucode would leave in shared memory is read from
 *              the PHY
 *   periodic   pwork_15sec and pwork_60sec, each when it is due
 *   rfkill     the radio off and on again
 */
#include <linux/bitrev.h>
#include <net/mac80211.h>

#include "b43.h"
#include "main.h"
#include "phy_common.h"
#include "phy_g.h"
#include "phy_lp.h"
#include "lo.h"

u32 chip_read(u16 off, int bits);
void chip_write(u16 off, u32 val, int bits);
void chip_time(unsigned long us);
void chip_state(const char *checkpoint);
long option(const char *name, long otherwise);
int step_asked(const char *name);
void vsay(const char *prefix, const char *fmt, va_list args);
__printf(1, 2) void say(const char *fmt, ...);

unsigned long volatile jiffies = INITIAL_JIFFIES;
static u64 clock_us;
static struct work_struct *queued_work;

static void elapse(unsigned long us)
{
	chip_time(us);
	clock_us += us;
	jiffies = INITIAL_JIFFIES + clock_us / (USEC_PER_SEC / HZ);
}

void __udelay(unsigned long usecs)
{
	elapse(usecs);
}

void __const_udelay(unsigned long xloops)
{
	elapse(xloops / 0x10c7);
}

void msleep(unsigned int msecs)
{
	elapse(msecs * 1000UL);
}

void usleep_range_state(unsigned long min, unsigned long max, unsigned int state)
{
	elapse(min);
}

unsigned long round_jiffies(unsigned long j)
{
	return j;
}

int __SCT__cond_resched(void)
{
	return 0;
}

int __SCT__might_resched(void)
{
	return 0;
}

void mutex_lock(struct mutex *lock)
{
}

void mutex_unlock(struct mutex *lock)
{
}

void ieee80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	queued_work = work;
}

unsigned long int_sqrt(unsigned long x)
{
	unsigned long root = 0, bit = 1UL << (BITS_PER_LONG - 2);

	while (bit > x)
		bit >>= 2;
	for (; bit; bit >>= 2) {
		if (x >= root + bit) {
			x -= root + bit;
			root = (root >> 1) + bit;
		} else {
			root >>= 1;
		}
	}
	return root;
}

/* The PMU's LDOs are the bus's, which this harness has not. */
void ssbn_pmu_set_ldo_voltage(struct ssbn_chipcommon *cc, enum ssbn_pmu_ldo_volt_id id, u32 voltage)
{
}

void ssbn_pmu_set_ldo_paref(struct ssbn_chipcommon *cc, bool on)
{
}

#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64
#define R4(n) R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)
#define R6(n) R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)
const u8 byte_rev_table[256] = { R6(0), R6(2), R6(1), R6(3) };

#define SAY(name, prefix)						\
void name(struct b43_wl *wl, const char *fmt, ...)			\
{									\
	va_list args;							\
									\
	va_start(args, fmt);						\
	vsay(prefix, fmt, args);					\
	va_end(args);							\
}
SAY(b43info, "b43: ")
SAY(b43dbg, "b43 debug: ")
SAY(b43warn, "b43 warning: ")
SAY(b43err, "b43 ERROR: ")
SAY(b43err_restart, "b43 ERROR: ")

static u16 bus_read16(struct b43_bus_dev *dev, u16 offset)
{
	return chip_read(offset, 16);
}

static u32 bus_read32(struct b43_bus_dev *dev, u16 offset)
{
	return chip_read(offset, 32);
}

static void bus_write16(struct b43_bus_dev *dev, u16 offset, u16 value)
{
	chip_write(offset, value, 16);
}

static void bus_write32(struct b43_bus_dev *dev, u16 offset, u32 value)
{
	chip_write(offset, value, 32);
}

/* main.c waits here for the ucode to say MAC_SUSPENDED. */
void b43_mac_suspend(struct b43_wldev *dev)
{
	if (dev->mac_suspended++ == 0)
		b43_maskset32(dev, B43_MMIO_MACCTL, ~B43_MACCTL_ENABLED, 0);
}

void b43_mac_enable(struct b43_wldev *dev)
{
	if (--dev->mac_suspended == 0)
		b43_maskset32(dev, B43_MMIO_MACCTL, ~0, B43_MACCTL_ENABLED);
}

void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
}

/* main.c resets the core through its bus first. */
void b43_wireless_core_reset(struct b43_wldev *dev, bool gmode)
{
	u32 macctl;

	if (dev->phy.ops)
		dev->phy.ops->switch_analog(dev, 1);
	macctl = b43_read32(dev, B43_MMIO_MACCTL) & ~B43_MACCTL_GMODE;
	if (gmode)
		macctl |= B43_MACCTL_GMODE;
	b43_write32(dev, B43_MMIO_MACCTL, macctl | B43_MACCTL_IHR_ENABLED);
}

#include "main_part.c"

static struct ssbn_sprom sprom;
static struct ssbn_bus ssb = { .bustype = SSBN_BUSTYPE_PCI };
static struct ssbn_device ssb_dev = { .bus = &ssb };
static struct ieee80211_channel channel = { .band = NL80211_BAND_2GHZ, .hw_value = 1 };
static struct ieee80211_hw hw;
static struct b43_wl wl = { .hw = &hw };
static struct b43_bus_dev bus = {
	.bus_type = B43_BUS_SSB,
	.sdev = &ssb_dev,
	.read16 = bus_read16,
	.read32 = bus_read32,
	.write16 = bus_write16,
	.write32 = bus_write32,
	.bus_sprom = &sprom,
};
static struct b43_wldev wldev = { .dev = &bus, .wl = &wl };

static void board(struct b43_wldev *dev)
{
	bus.core_rev = option("B43_CORE_REV", 5);
	bus.chip_id = option("B43_CHIP_ID", 0x4306);
	bus.chip_rev = option("B43_CHIP_REV", 3);
	bus.board_vendor = option("B43_BOARD_VENDOR", 0);
	bus.board_type = option("B43_BOARD_TYPE", 0);
	sprom.boardflags_lo = option("B43_BOARDFLAGS", 0);
	sprom.pa0b0 = option("B43_PA0B0", 0);
	sprom.pa0b1 = option("B43_PA0B1", 0);
	sprom.pa0b2 = option("B43_PA0B2", 0);
	sprom.itssi_bg = option("B43_ITSSI", 0);
	sprom.maxpwr_bg = option("B43_MAXPWR", 0x48);
	ssb.chipco.capabilities = SSBN_CHIPCO_CAP_PMU;
	ssb.chipco.pmu.crystalfreq = option("B43_XTAL_KHZ", 20000);
	hw.conf.chandef.chan = &channel;
	wl.current_dev = dev;
	dev->phy.gmode = true;
	dev->phy.hardware_power_control = option("B43_HWPCTL", 0);
	dev->phy.desired_txpower = option("B43_TXPOWER", 15);
	dev->mac_suspended = 1;
}

static void checkpoint_g(struct b43_wldev *dev)
{
	struct b43_phy_g *g = dev->phy.g;
	struct b43_txpower_lo_control *lo = g->lo_control;
	struct b43_lo_calib *cal;

	say("driver channel=%u bbatt=%u rfatt=%u padmix=%u tx_control=0x%02x tx_bias=0x%02x tx_magn=0x%02x\n",
	    dev->phy.channel, g->bbatt.att, g->rfatt.att, g->rfatt.with_padmix,
	    g->tx_control, lo->tx_bias, lo->tx_magn);
	say("driver max_lb_gain=%d trsw_rx_gain=%d lna_lod_gain=%d lna_gain=%d pga_gain=%d initval=0x%04x\n",
	    g->max_lb_gain, g->trsw_rx_gain, g->lna_lod_gain, g->lna_gain, g->pga_gain, g->initval);
	say("driver nrssi0=%d nrssi1=%d nrssislope=%d\n", g->nrssi[0], g->nrssi[1], g->nrssislope);
	say("driver tgt_idle_tssi=%d cur_idle_tssi=%d average_tssi=%u\n",
	    g->tgt_idle_tssi, g->cur_idle_tssi, g->average_tssi);
	list_for_each_entry(cal, &lo->calib_list, list)
		say("driver lo bbatt=%u rfatt=%u padmix=%u i=%d q=%d\n",
		    cal->bbatt.att, cal->rfatt.att, cal->rfatt.with_padmix, cal->ctl.i, cal->ctl.q);
}

static void checkpoint_lp(struct b43_wldev *dev)
{
	struct b43_phy_lp *lp = dev->phy.lp;

	say("driver channel=%u rc_cap=%u pdiv=%u txpctl_mode=%d tssi_idx=%d tssi_npt=%d\n",
	    lp->channel, lp->rc_cap, lp->pdiv, lp->txpctl_mode, lp->tssi_idx, lp->tssi_npt);
}

static __printf(2, 3) void checkpoint(struct b43_wldev *dev, const char *fmt, ...)
{
	char name[64];
	va_list args;

	va_start(args, fmt);
	vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);
	chip_state(name);
	if (dev->phy.type == B43_PHYTYPE_G)
		checkpoint_g(dev);
	else if (dev->phy.type == B43_PHYTYPE_LP)
		checkpoint_lp(dev);
}

static void channels(struct b43_wldev *dev, const u8 *list, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		b43_switch_channel(dev, list[i]);
		dev->phy.channel = list[i];
		checkpoint(dev, "channel %u", list[i]);
	}
}

/*
 * A frame, and what the ucode does after one: the PHY's TSSI into the four
 * samples of shared memory that recalc_txpower averages.
 */
static void frame(struct b43_wldev *dev)
{
	u16 tssi;

	b43_mac_suspend(dev);
	b43_dummy_transmission(dev, false, true);
	tssi = b43_phy_read(dev, B43_PHY_ITSSI) & 0x3F;
	b43_mac_enable(dev);
	b43_shm_write32(dev, B43_SHM_SHARED, B43_SHM_SH_TSSI_CCK, tssi * 0x01010101U);
}

static void power(struct b43_wldev *dev)
{
	int round;

	for (round = 1; round <= option("B43_POWER_ROUNDS", 8); round++) {
		frame(dev);
		queued_work = NULL;
		b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME);
		if (queued_work)
			b43_phy_txpower_adjust_work(queued_work);
		checkpoint(dev, "power %d%s", round, queued_work ? " adjusted" : "");
	}
}

static void periodic(struct b43_wldev *dev)
{
	const struct b43_phy_operations *ops = dev->phy.ops;
	int quarter;

	for (quarter = 1; quarter <= 4; quarter++) {
		msleep(15000);
		if (ops->pwork_15sec)
			ops->pwork_15sec(dev);
		if (quarter == 4 && ops->pwork_60sec)
			ops->pwork_60sec(dev);
		checkpoint(dev, "periodic %ds", 15 * quarter);
	}
}

static void rfkill(struct b43_wldev *dev)
{
	b43_software_rfkill(dev, true);
	checkpoint(dev, "rfkill blocked");
	b43_software_rfkill(dev, false);
	checkpoint(dev, "rfkill unblocked");
}

int drive(void)
{
	struct b43_wldev *dev = &wldev;
	struct b43_phy *phy = &dev->phy;
	int err;

	board(dev);
	err = b43_phy_versioning(dev);
	if (!err)
		err = b43_phy_allocate(dev);
	if (err)
		return err;
	b43_wireless_core_reset(dev, phy->gmode);
	phy->ops->prepare_structs(dev);
	if (phy->ops->prepare_hardware)
		err = phy->ops->prepare_hardware(dev);
	if (!err)
		err = b43_phy_init(dev);
	if (err)
		return err;
	b43_mac_enable(dev);
	atomic_set(&dev->__init_status, B43_STAT_STARTED);
	checkpoint(dev, "init");

	if (step_asked("channels"))
		channels(dev, (const u8[]){ 6, 11, 13, 1 }, 4);
	if (step_asked("sweep"))
		channels(dev, (const u8[]){ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 }, 14);
	if (step_asked("power") && phy->type == B43_PHYTYPE_G)
		power(dev);
	if (step_asked("periodic"))
		periodic(dev);
	if (step_asked("rfkill"))
		rfkill(dev);
	return 0;
}
