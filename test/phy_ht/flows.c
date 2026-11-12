// SPDX-License-Identifier: GPL-2.0
/*
 * Driver side of the HT-PHY harness, built against the kernel headers like
 * the driver. It compiles phy_ht.c into this file to reach its static
 * functions, puts a BCM4331 (HT-PHY rev 1, radio 2059) on a bus whose
 * accesses go to trace.c, and runs one flow. Nothing of b43 is modified.
 */
#include "phy_ht.c"
#include "ht_trace.h"

static struct ieee80211_channel chan = {
	.band = NL80211_BAND_2GHZ,
};
static struct ieee80211_hw hw;
static struct b43_wl wl;
static struct bcmn_host_ops agent_ops;
static struct bcmn_bus agent_bus;
static struct bcmn_device core;
static struct b43_bus_dev busdev;
static struct ssbn_sprom sprom;
static struct b43_phy_ht phy_ht;
static struct b43_wldev wldev;

static u16 bus_read16(struct b43_bus_dev *d, u16 offset)
{
	return ht_mmio_read16(offset);
}

static void bus_write16(struct b43_bus_dev *d, u16 offset, u16 value)
{
	ht_mmio_write16(offset, value);
}

static u32 bus_read32(struct b43_bus_dev *d, u16 offset)
{
	return ht_mmio_read32(offset);
}

static void bus_write32(struct b43_bus_dev *d, u16 offset, u32 value)
{
	ht_mmio_write32(offset, value);
}

static u32 agent_read32(struct bcmn_device *c, u16 offset)
{
	return ht_wrap_read32(offset);
}

static void agent_write32(struct bcmn_device *c, u16 offset, u32 value)
{
	ht_wrap_write32(offset, value);
}

static struct b43_wldev *setup(void)
{
	int c;

	chan.hw_value = ht_env("HT_CHANNEL", 6);
	chan.center_freq = 2407 + 5 * chan.hw_value;
	hw.conf.chandef.chan = &chan;
	hw.conf.chandef.width = NL80211_CHAN_WIDTH_20;
	hw.conf.chandef.center_freq1 = chan.center_freq;
	wl.hw = &hw;

	agent_ops.aread32 = agent_read32;
	agent_ops.awrite32 = agent_write32;
	agent_bus.ops = &agent_ops;
	core.bus = &agent_bus;

	busdev.bus_type = B43_BUS_BCMA;
	busdev.bdev = &core;
	busdev.read16 = bus_read16;
	busdev.write16 = bus_write16;
	busdev.read32 = bus_read32;
	busdev.write32 = bus_write32;
	busdev.flush_writes = false;
	busdev.chip_id = 0x4331;
	busdev.core_rev = 29;
	busdev.bus_sprom = &sprom;

	sprom.revision = ht_nvram("sromrev", 0);
	sprom.boardflags_lo = ht_nvram("boardflags", 0) & 0xffff;
	sprom.boardflags_hi = ht_nvram("boardflags", 0) >> 16;
	sprom.boardflags2_lo = ht_nvram("boardflags2", 0) & 0xffff;
	sprom.boardflags2_hi = ht_nvram("boardflags2", 0) >> 16;
	for (c = 0; c < 3; c++) {
		char v[16];
		int k;

		for (k = 0; k < 3; k++) {
			snprintf(v, sizeof(v), "pa2gw%da%d", k, c);
			sprom.core_pwr_info[c].pa_2g[k] = (s16)ht_nvram(v, 0);
		}
		snprintf(v, sizeof(v), "maxp2ga%d", c);
		sprom.core_pwr_info[c].maxpwr_2g = ht_nvram(v, 0);
	}

	wldev.dev = &busdev;
	wldev.wl = &wl;
	wldev.phy.ops = &b43_phyops_ht;
	wldev.phy.ht = &phy_ht;
	wldev.phy.type = B43_PHYTYPE_HT;
	wldev.phy.rev = 1;
	wldev.phy.radio_ver = 0x2059;
	wldev.phy.channel = chan.hw_value;
	wldev.phy.chandef = &hw.conf.chandef;

	b43_phy_ht_op_prepare_structs(&wldev);
	return &wldev;
}

int ht_run_flow(const char *name)
{
	struct b43_wldev *dev = setup();

	if (!strcmp(name, "power_fix"))
		b43_phy_ht_tx_power_fix(dev);
	else if (!strcmp(name, "pctl_off"))
		b43_phy_ht_tx_power_ctl(dev, false);
	else if (!strcmp(name, "idle_tssi"))
		b43_phy_ht_tx_power_ctl_idle_tssi(dev);
	else if (!strcmp(name, "tssi_setup"))
		b43_phy_ht_tssi_setup(dev);
	else if (!strcmp(name, "pctl_on"))
		b43_phy_ht_tx_power_ctl(dev, true);
	else if (!strcmp(name, "full")) {
		/*
		 * What b43 runs of the PHY from its init to the BSS up: the
		 * analog switch of b43_wireless_core_reset() and of
		 * b43_phy_bringup(), then b43_phy_init().
		 */
		dev->phy.chandef = NULL;
		dev->phy.do_full_init = true;
		dev->phy.ops->switch_analog(dev, true);
		dev->phy.ops->switch_analog(dev, true);
		b43_phy_init(dev);
		/* mac80211's first config sets the power: the check, then its work */
		b43_phy_txpower_check(dev, B43_TXPWR_IGNORE_TIME |
				      B43_TXPWR_IGNORE_TSSI);
		dev->phy.ops->adjust_txpower(dev);
	} else if (!strcmp(name, "pctl_init")) {
		/* The TX power control at the end of b43_phy_ht_op_init() */
		bool saved = phy_ht.tx_pwr_ctl;

		b43_phy_ht_tx_power_fix(dev);
		b43_phy_ht_tx_power_ctl(dev, false);
		b43_phy_ht_tx_power_ctl_idle_tssi(dev);
		b43_phy_ht_tx_power_ctl_setup(dev);
		b43_phy_ht_tssi_setup(dev);
		b43_phy_ht_tx_power_ctl(dev, saved);
	} else
		return -1;
	return 0;
}

/*
 * What the files above need from the rest of the kernel and of b43. The
 * flows reach none of it; a call is logged, so that a run that does shows.
 */
#define UNREACHED(f) ht_log("unreached: " f)

void b43err(struct b43_wl *w, const char *fmt, ...) { ht_log(fmt); }
void b43err_restart(struct b43_wl *w, const char *fmt, ...) { ht_log(fmt); }
void __const_udelay(unsigned long xloops) { }
void __udelay(unsigned long usecs) { }
void msleep(unsigned int msecs) { }
void usleep_range_state(unsigned long min, unsigned long max,
			unsigned int state) { }
unsigned long volatile jiffies;
unsigned long round_jiffies(unsigned long j) { return j; }
void kfree(const void *p) { UNREACHED("kfree"); }
void *__kmalloc_cache_noprof(struct kmem_cache *s, gfp_t flags, size_t size)
{
	UNREACHED("kmalloc");
	return NULL;
}
struct kmem_cache *kmalloc_caches[NR_KMALLOC_TYPES][KMALLOC_SHIFT_HIGH + 1];
void mutex_lock(struct mutex *lock) { }
void mutex_unlock(struct mutex *lock) { }
void ieee80211_queue_work(struct ieee80211_hw *h, struct work_struct *w)
{
	ht_core("ieee80211_queue_work");
}

/*
 * b43's core: on hardware these make MMIO accesses of their own, which the
 * harness does not emit. The calls are counted.
 */
void b43_mac_suspend(struct b43_wldev *dev) { ht_core("b43_mac_suspend"); }
void b43_mac_enable(struct b43_wldev *dev) { ht_core("b43_mac_enable"); }
void b43_mac_switch_freq(struct b43_wldev *dev, u8 spurmode)
{
	ht_core("b43_mac_switch_freq");
}
void b43_mac_phy_clock_set(struct b43_wldev *dev, bool on)
{
	ht_core("b43_mac_phy_clock_set");
}
void b43_wireless_core_phy_pll_reset(struct b43_wldev *dev)
{
	ht_core("b43_wireless_core_phy_pll_reset");
}
void b43_power_saving_ctl_bits(struct b43_wldev *dev, unsigned int ps_flags)
{
	ht_core("b43_power_saving_ctl_bits");
}
u16 b43_shm_read16(struct b43_wldev *dev, u16 routing, u16 offset)
{
	ht_core("b43_shm_read16");
	return 0;
}
u32 b43_shm_read32(struct b43_wldev *dev, u16 routing, u16 offset)
{
	ht_core("b43_shm_read32");
	return 0;
}
void b43_shm_write16(struct b43_wldev *dev, u16 routing, u16 offset, u16 value)
{
	ht_core("b43_shm_write16");
}
void b43_shm_write32(struct b43_wldev *dev, u16 routing, u16 offset, u32 value)
{
	ht_core("b43_shm_write32");
}
void bcmn_core_pll_ctl(struct bcmn_device *c, u32 req, u32 status, bool on)
{
	ht_core("bcmn_core_pll_ctl");
}
void bcmn_pmu_spuravoid_pllupdate(struct bcmn_drv_cc *cc, int spuravoid)
{
	ht_core("bcmn_pmu_spuravoid_pllupdate");
}
