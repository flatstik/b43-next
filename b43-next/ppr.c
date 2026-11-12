// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Broadcom B43 wireless driver
 * PPR (Power Per Rate) management
 *
 * Copyright (c) 2014 Rafał Miłecki <zajec5@gmail.com>
 * Copyright (c) 2026 Alessio Ferri <alessio.ferri.3012@gmail.com>
 * Copyright (c) 2026 Andreas Wendleder <andreas.wendleder@proton.me>
 */

#include "b43.h"
#include "ppr.h"

/*
 * The groups of the table, the unit a loader marks loaded: CCK, then OFDM and
 * MCS by bandwidth and mode, in the order of struct b43_ppr.
 */
#define B43_PPR_G_CCK		0
#define B43_PPR_G_OFDM(bw, mode)					\
	(1 + (bw) * B43_PPR_OFDM_MODES + (mode))
#define B43_PPR_G_MCS(bw, mode)						\
	(B43_PPR_G_OFDM(B43_PPR_BW_NUM, 0) + (bw) * B43_PPR_MODE_NUM + (mode))
#define B43_PPR_GROUPS		B43_PPR_G_MCS(B43_PPR_BW_NUM, 0)

/* Index in __all of the first entry of group @g, and in @len its size. */
static unsigned int b43_ppr_group(const struct b43_ppr *ppr, unsigned int g,
				  unsigned int *len)
{
	*len = 0;
	if (!(ppr->loaded & BIT(g)))
		return 0;
	if (g == B43_PPR_G_CCK) {
		*len = B43_PPR_CCK_RATES;
		return offsetof(struct b43_ppr, cck);
	}
	if (g < B43_PPR_G_MCS(0, 0)) {
		*len = B43_PPR_OFDM_RATES;
		return offsetof(struct b43_ppr, ofdm) +
		       (g - B43_PPR_G_OFDM(0, 0)) * B43_PPR_OFDM_RATES;
	}
	*len = ppr->mcs_num;
	return offsetof(struct b43_ppr, mcs) +
	       (g - B43_PPR_G_MCS(0, 0)) * B43_PPR_MCS_RATES;
}

/* The bandwidth of an OFDM or MCS group; CCK has none. */
static int b43_ppr_group_bw(unsigned int g)
{
	if (g == B43_PPR_G_CCK)
		return -1;
	if (g < B43_PPR_G_MCS(0, 0))
		return (g - B43_PPR_G_OFDM(0, 0)) / B43_PPR_OFDM_MODES;
	return (g - B43_PPR_G_MCS(0, 0)) / B43_PPR_MODE_NUM;
}

#define ppr_for_each_entry(ppr, g, i, n)				\
	for (g = 0; g < B43_PPR_GROUPS; g++)				\
		for (i = b43_ppr_group(ppr, g, &n); n; i++, n--)

void b43_ppr_clear(struct b43_ppr *ppr)
{
	memset(ppr, 0, sizeof(*ppr));

	BUILD_BUG_ON(offsetof(struct b43_ppr, loaded) !=
		     B43_PPR_ENTRIES * sizeof(u8));
	BUILD_BUG_ON(B43_PPR_GROUPS > 32);
}

void b43_ppr_add(struct b43_ppr *ppr, int diff)
{
	unsigned int g, i, n;

	ppr_for_each_entry(ppr, g, i, n)
		ppr->__all[i] = clamp_t(int, ppr->__all[i] + diff, 0, 127);
}

void b43_ppr_apply_max(struct b43_ppr *ppr, u8 max)
{
	unsigned int g, i, n;

	ppr_for_each_entry(ppr, g, i, n)
		ppr->__all[i] = min(ppr->__all[i], max);
}

void b43_ppr_apply_min(struct b43_ppr *ppr, u8 min)
{
	unsigned int g, i, n;

	ppr_for_each_entry(ppr, g, i, n)
		ppr->__all[i] = max(ppr->__all[i], min);
}

u8 b43_ppr_get_max(const struct b43_ppr *ppr)
{
	unsigned int g, i, n;
	u8 res = 0;

	ppr_for_each_entry(ppr, g, i, n)
		res = max(ppr->__all[i], res);

	return res;
}

u8 b43_ppr_get_bw_max(const struct b43_ppr *ppr, enum b43_ppr_bw bw)
{
	unsigned int g, i, n;
	u8 res = 0;

	ppr_for_each_entry(ppr, g, i, n)
		if (b43_ppr_group_bw(g) == bw)
			res = max(ppr->__all[i], res);

	return res;
}

bool b43_ppr_has_cck(const struct b43_ppr *ppr)
{
	return ppr->loaded & BIT(B43_PPR_G_CCK);
}

u8 b43_ppr_get_cck(const struct b43_ppr *ppr, unsigned int rate)
{
	if (WARN_ON_ONCE(rate >= B43_PPR_CCK_RATES))
		return 0;
	return ppr->cck[rate];
}

u8 b43_ppr_get_ofdm(const struct b43_ppr *ppr, enum b43_ppr_bw bw,
		    enum b43_ppr_mode mode, unsigned int rate)
{
	if (WARN_ON_ONCE(bw >= B43_PPR_BW_NUM || mode >= B43_PPR_OFDM_MODES ||
			 rate >= B43_PPR_OFDM_RATES))
		return 0;
	return ppr->ofdm[bw][mode][rate];
}

u8 b43_ppr_get_mcs(const struct b43_ppr *ppr, enum b43_ppr_bw bw,
		   enum b43_ppr_mode mode, unsigned int mcs)
{
	if (WARN_ON_ONCE(bw >= B43_PPR_BW_NUM || mode >= B43_PPR_MODE_NUM ||
			 mcs >= B43_PPR_MCS_RATES))
		return 0;
	return ppr->mcs[bw][mode][mcs];
}

enum b43_ppr_bw b43_ppr_bw(enum nl80211_chan_width width)
{
	switch (width) {
	case NL80211_CHAN_WIDTH_80:
		return B43_PPR_BW_80;
	case NL80211_CHAN_WIDTH_40:
		return B43_PPR_BW_40;
	default:
		return B43_PPR_BW_20;
	}
}

bool b43_ppr_band(const struct ieee80211_channel *chan, enum b43_band *band)
{
	u16 freq = chan->center_freq;

	if (chan->band == NL80211_BAND_2GHZ)
		*band = B43_BAND_2G;
	else if (freq >= 4900 && freq < 5100)
		*band = B43_BAND_5G_LO;
	else if (freq >= 5100 && freq < 5500)
		*band = B43_BAND_5G_MI;
	else if (freq >= 5500)
		*band = B43_BAND_5G_HI;
	else
		return false;
	return true;
}

/* The offset of rate group @i in a word of nibbles, in half dB. */
static u8 b43_ppr_off(u32 po, unsigned int i)
{
	return ((po >> (4 * i)) & 0xf) * 2;
}

/* @maxp less @off, at zero rather than wrapping past it. */
static u8 b43_ppr_sub(u8 maxp, u8 off)
{
	return off > maxp ? 0 : maxp - off;
}

static void b43_ppr_load_cck(struct b43_ppr *ppr, u8 maxp, u16 po)
{
	unsigned int i;

	for (i = 0; i < B43_PPR_CCK_RATES; i++)
		ppr->cck[i] = b43_ppr_sub(maxp, b43_ppr_off(po, i));
	ppr->loaded |= BIT(B43_PPR_G_CCK);
}

static u8 *b43_ppr_load_ofdm(struct b43_ppr *ppr, enum b43_ppr_bw bw,
			     enum b43_ppr_mode mode)
{
	ppr->loaded |= BIT(B43_PPR_G_OFDM(bw, mode));
	return ppr->ofdm[bw][mode];
}

static u8 *b43_ppr_load_mcs(struct b43_ppr *ppr, enum b43_ppr_bw bw,
			    enum b43_ppr_mode mode)
{
	ppr->loaded |= BIT(B43_PPR_G_MCS(bw, mode));
	return ppr->mcs[bw][mode];
}

u8 b43_ppr_load_sprom_legacy(struct b43_ppr *ppr,
			     const struct ssbn_sprom *sprom,
			     enum b43_band band, bool ht)
{
	u8 maxp, *ofdm, *mcs;
	u32 ofdm_po;
	const u16 *mcs_po;
	unsigned int i;

	b43_ppr_clear(ppr);

	switch (band) {
	case B43_BAND_2G:
		maxp = sprom->maxpwr_bg;
		ofdm_po = sprom->ofdm2gpo;
		mcs_po = sprom->mcs2gpo;
		break;
	case B43_BAND_5G_LO:
		maxp = sprom->maxpwr_al;
		ofdm_po = sprom->ofdm5glpo;
		mcs_po = sprom->mcs5glpo;
		break;
	case B43_BAND_5G_MI:
		maxp = sprom->maxpwr_a;
		ofdm_po = sprom->ofdm5gpo;
		mcs_po = sprom->mcs5gpo;
		break;
	case B43_BAND_5G_HI:
		maxp = sprom->maxpwr_ah;
		ofdm_po = sprom->ofdm5ghpo;
		mcs_po = sprom->mcs5ghpo;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	if (band == B43_BAND_2G)
		b43_ppr_load_cck(ppr, maxp, sprom->cck2gpo);
	ofdm = b43_ppr_load_ofdm(ppr, B43_PPR_BW_20, B43_PPR_SISO);
	for (i = 0; i < B43_PPR_OFDM_RATES; i++)
		ofdm[i] = b43_ppr_sub(maxp, b43_ppr_off(ofdm_po, i));

	if (ht) {
		ppr->mcs_num = 8;
		mcs = b43_ppr_load_mcs(ppr, B43_PPR_BW_20, B43_PPR_SISO);
		for (i = 0; i < 8; i++)
			mcs[i] = b43_ppr_sub(maxp,
					     b43_ppr_off(mcs_po[i / 4], i % 4));
	}

	return maxp;
}

u8 b43_ppr_sprom_r8_maxpwr(const struct ssbn_sprom *sprom, enum b43_band band,
			   u8 coremask)
{
	const struct ssbn_sprom_core_pwr_info *pi;
	bool found = false;
	unsigned int c;
	u8 maxp = 0xff, p;

	for (c = 0; c < ARRAY_SIZE(sprom->core_pwr_info); c++) {
		if (!(coremask & BIT(c)))
			continue;
		pi = &sprom->core_pwr_info[c];
		switch (band) {
		case B43_BAND_2G:
			p = pi->maxpwr_2g;
			break;
		case B43_BAND_5G_LO:
			p = pi->maxpwr_5gl;
			break;
		case B43_BAND_5G_MI:
			p = pi->maxpwr_5g;
			break;
		case B43_BAND_5G_HI:
			p = pi->maxpwr_5gh;
			break;
		default:
			WARN_ON_ONCE(1);
			return 0;
		}
		maxp = min(maxp, p);
		found = true;
	}

	return found ? maxp : 0;
}

/*
 * One MCS mode at 20 MHz from two words of mcs*po, MCS 0-3 and 4-7, less
 * @extra on every rate.
 */
static void b43_ppr_load_r8_mcs(struct b43_ppr *ppr, enum b43_ppr_mode mode,
				u8 maxp, const u16 *po, u8 extra)
{
	u8 *mcs = b43_ppr_load_mcs(ppr, B43_PPR_BW_20, mode);
	unsigned int i;

	for (i = 0; i < 8; i++)
		mcs[i] = b43_ppr_sub(b43_ppr_sub(maxp,
						 b43_ppr_off(po[i / 4], i % 4)),
				     extra * 2);
}

u8 b43_ppr_load_sprom_r8(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			 enum b43_band band, u8 coremask, bool cdd_stbc_po)
{
	/* The OFDM rate each MCS and CDD OFDM rate takes its power from. */
	static const u8 mcs_from_ofdm[8] = { 0, 2, 3, 4, 5, 6, 7, 7 };
	static const u8 ofdm_from_cdd_mcs[8] = { 0, 0, 1, 2, 3, 4, 5, 6 };
	u8 maxp = b43_ppr_sprom_r8_maxpwr(sprom, band, coremask);
	u8 *ofdm, *mcs, *cdd, cdd_po = 0, stbc_po = 0;
	u32 ofdm_po;
	const u16 *mcs_po;
	unsigned int i, nib;

	b43_ppr_clear(ppr);

	switch (band) {
	case B43_BAND_2G:
		ofdm_po = sprom->ofdm2gpo;
		mcs_po = sprom->mcs2gpo;
		nib = 0;
		break;
	case B43_BAND_5G_LO:
		ofdm_po = sprom->ofdm5glpo;
		mcs_po = sprom->mcs5glpo;
		nib = 2;
		break;
	case B43_BAND_5G_MI:
		ofdm_po = sprom->ofdm5gpo;
		mcs_po = sprom->mcs5gpo;
		nib = 1;
		break;
	case B43_BAND_5G_HI:
		ofdm_po = sprom->ofdm5ghpo;
		mcs_po = sprom->mcs5ghpo;
		nib = 3;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	if (band == B43_BAND_2G)
		b43_ppr_load_cck(ppr, maxp, sprom->cck2gpo);

	ofdm = b43_ppr_load_ofdm(ppr, B43_PPR_BW_20, B43_PPR_SISO);
	for (i = 0; i < B43_PPR_OFDM_RATES; i++)
		ofdm[i] = b43_ppr_sub(maxp, b43_ppr_off(ofdm_po, i));

	ppr->mcs_num = 8;
	mcs = b43_ppr_load_mcs(ppr, B43_PPR_BW_20, B43_PPR_SISO);
	for (i = 0; i < 8; i++)
		mcs[i] = ofdm[mcs_from_ofdm[i]];

	if (cdd_stbc_po) {
		cdd_po = (sprom->cddpo >> (4 * nib)) & 0xf;
		stbc_po = (sprom->stbcpo >> (4 * nib)) & 0xf;
	}
	b43_ppr_load_r8_mcs(ppr, B43_PPR_CDD, maxp, &mcs_po[0], cdd_po);
	b43_ppr_load_r8_mcs(ppr, B43_PPR_STBC, maxp, &mcs_po[0], stbc_po);
	b43_ppr_load_r8_mcs(ppr, B43_PPR_SDM, maxp, &mcs_po[2], 0);

	cdd = ppr->mcs[B43_PPR_BW_20][B43_PPR_CDD];
	ofdm = b43_ppr_load_ofdm(ppr, B43_PPR_BW_20, B43_PPR_CDD);
	for (i = 0; i < B43_PPR_OFDM_RATES; i++)
		ofdm[i] = cdd[ofdm_from_cdd_mcs[i]];

	return maxp;
}

/*
 * Rev 9 and 10 keep the per-core maxima of rev 8 and give each rate its own
 * offset, a nibble of half dB with the lowest rate in the lowest nibble:
 * cckbw202gpo for 1-11 Mb/s, legofdmbw20*po for 6-54 Mb/s, mcsbw20*po for
 * MCS 0-7. No word tells the transmit modes apart, so every MCS mode takes
 * the MCS offsets, each stream of SDM included, and legacy OFDM over CDD the
 * legacy ones.
 */
u8 b43_ppr_load_sprom_r9(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			 enum b43_band band, u8 coremask)
{
	u8 maxp = b43_ppr_sprom_r8_maxpwr(sprom, band, coremask);
	enum b43_ppr_mode mode;
	u32 ofdm_po, mcs_po;
	unsigned int i;
	u8 *ofdm, *mcs;

	b43_ppr_clear(ppr);

	switch (band) {
	case B43_BAND_2G:
		ofdm_po = sprom->legofdmbw202gpo;
		mcs_po = sprom->mcsbw202gpo;
		break;
	case B43_BAND_5G_LO:
		ofdm_po = sprom->legofdmbw205glpo;
		mcs_po = sprom->mcsbw205glpo;
		break;
	case B43_BAND_5G_MI:
		ofdm_po = sprom->legofdmbw205gmpo;
		mcs_po = sprom->mcsbw205gmpo;
		break;
	case B43_BAND_5G_HI:
		ofdm_po = sprom->legofdmbw205ghpo;
		mcs_po = sprom->mcsbw205ghpo;
		break;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}

	if (band == B43_BAND_2G)
		b43_ppr_load_cck(ppr, maxp, sprom->cckbw202gpo);

	for (mode = B43_PPR_SISO; mode < B43_PPR_OFDM_MODES; mode++) {
		ofdm = b43_ppr_load_ofdm(ppr, B43_PPR_BW_20, mode);
		for (i = 0; i < B43_PPR_OFDM_RATES; i++)
			ofdm[i] = b43_ppr_sub(maxp, b43_ppr_off(ofdm_po, i));
	}

	ppr->mcs_num = 8;
	for (mode = B43_PPR_SISO; mode < B43_PPR_MODE_NUM; mode++) {
		mcs = b43_ppr_load_mcs(ppr, B43_PPR_BW_20, mode);
		for (i = 0; i < 8; i++)
			mcs[i] = b43_ppr_sub(maxp, b43_ppr_off(mcs_po, i));
	}

	return maxp;
}

/*
 * SROM revision 11, the AC-PHY's. Per-core maxima, maxp2ga on 2.4 GHz and
 * maxp5ga[] over four 5 GHz sub-bands, and power offsets per band, bandwidth
 * and modulation class rather than per rate. Each mcsbw{20,40,80}5g{l,m,h}po
 * word carries eight nibbles, and each nibble is the offset of one
 * modulation and coding class, shared by every legacy OFDM rate and every
 * MCS that uses it:
 *
 *   group  class          OFDM rates       MCS
 *     0    BPSK, QPSK     6, 9, 12, 18     0, 1, 2
 *     1    16-QAM 1/2     24               3
 *     2    16-QAM 3/4     36               4
 *     3    64-QAM 2/3     48               5
 *     4    64-QAM 3/4     54               6
 *     5    64-QAM 5/6     --               7
 *     6    256-QAM 3/4    --               8
 *     7    256-QAM 5/6    --               9
 *
 * This is the decode `wl curpower` prints as "Board Limits" on the agcombo and
 * DSL-3580L dumps (docs/txpwr-target-derivation.md). There is no CCK field
 * for 5 GHz in rev 11.
 *
 * A channel loads the 20 MHz row at every width, the 40 MHz row from 40 MHz up
 * and the 80 MHz row at 80: those are the 20-in-40, 20-in-80 and 40-in-80 rate
 * groups of a bonded channel, and the reason the maximum over the table is
 * taken over every width the channel contains.
 *
 * On 2.4 GHz the same groups come from mcsbw202gpo and mcsbw402gpo under
 * maxp2ga, and the four CCK rates, 1, 2, 5.5 and 11 Mb/s, from the four
 * nibbles of cckbw202gpo. The table takes the minimum maximum over the
 * active cores, as the rev 8 loader does over its cores.
 */
static const u8 b43_ppr_r11_ofdm_group[B43_PPR_OFDM_RATES] = {
	0, 0, 0, 0, 1, 2, 3, 4
};

static const u8 b43_ppr_r11_mcs_group[B43_PPR_MCS_RATES] = {
	0, 0, 0, 1, 2, 3, 4, 5, 6, 7
};

/*
 * Sub-band index into maxp5ga[], keyed on the primary channel: at 80 MHz the
 * captures follow it, and the centre puts ch36 one group too high.
 *
 * The first boundary is 5210 at 20 and 80 MHz, 5250 at 40, settled by the
 * sign of the residuals: a regulatory limit can only lower a target. At 40
 * MHz 5210 leaves ch44 two units low, which nothing downstream raises; 5250
 * leaves ch36 and ch52 two units high, which a limit explains, and the rest
 * exact. The 20 MHz boundary rests on the d6220 alone, the only board with
 * distinct first entries: ch40 gives 66 and ch44 64 with maxp5ga = {72, 70,
 * ...}.
 *
 * Not b43_phy_ac_pa5g_group(), which splits at 5250 at every width
 * (subband5gver=4) for the pa5ga coefficients: the two coincide at 40 MHz
 * only.
 *
 * ch149-165 take the fourth entry: the tg789vac-v2 (maxp5ga 90/88/92/88)
 * writes 0x52 on all nine UNII-3 configurations, 88 less the margin, where
 * the third would give 0x56; the d6220 (72/70/86/0) writes 0x04 on its eight.
 */
unsigned int b43_ppr_sprom_r11_subband(u16 chan, enum nl80211_chan_width width)
{
	u16 freq = 5000 + 5 * chan;
	u16 first = (width == NL80211_CHAN_WIDTH_40) ? 5250 : 5210;

	if (freq < first)
		return 0;
	if (freq < 5500)
		return 1;
	if (freq < 5745)
		return 2;
	return 3;
}

/*
 * The l/m/h band of the mcsbw*po words: 0 = 5gl, 1 = 5gm, 2 = 5gh, by channel
 * number. It is not the maxp5ga sub-band of b43_ppr_sprom_r11_subband(),
 * which is by frequency: the two partitions are the SROM's, not this
 * driver's, and they differ at ch48-51 and ch100.
 */
static unsigned int b43_ppr_r11_po_band(u16 chan)
{
	if (chan < 52)
		return 0;
	if (chan < 100)
		return 1;
	return 2;
}

static bool b43_ppr_r11_is_2g(u16 chan)
{
	return chan <= 14;
}

/*
 * Fill the rows the width loads from @top less each group's nibble, and on
 * 2.4 GHz the CCK rates less theirs.
 */
static void b43_ppr_r11_fill(struct b43_ppr *ppr,
			     const struct ssbn_sprom *sprom, u16 chan,
			     enum nl80211_chan_width width, u8 top)
{
	const u32 po[3][B43_PPR_BW_NUM] = {
		{ sprom->mcsbw205glpo, sprom->mcsbw405glpo,
		  sprom->mcsbw805glpo },
		{ sprom->mcsbw205gmpo, sprom->mcsbw405gmpo,
		  sprom->mcsbw805gmpo },
		{ sprom->mcsbw205ghpo, sprom->mcsbw405ghpo,
		  sprom->mcsbw805ghpo },
	};
	const u32 po2g[B43_PPR_BW_NUM] = {
		sprom->mcsbw202gpo, sprom->mcsbw402gpo, 0,
	};
	bool g = b43_ppr_r11_is_2g(chan);
	const u32 *rowpo = g ? po2g : po[b43_ppr_r11_po_band(chan)];
	enum b43_ppr_bw bw, last = b43_ppr_bw(width);
	unsigned int i;
	u8 *ofdm, *mcs;

	b43_ppr_clear(ppr);
	ppr->mcs_num = B43_PPR_MCS_RATES;

	if (g)
		b43_ppr_load_cck(ppr, top, sprom->cckbw202gpo);
	for (bw = B43_PPR_BW_20; bw <= last; bw++) {
		ofdm = b43_ppr_load_ofdm(ppr, bw, B43_PPR_SISO);
		for (i = 0; i < B43_PPR_OFDM_RATES; i++)
			ofdm[i] = b43_ppr_sub(top, b43_ppr_off(rowpo[bw],
						b43_ppr_r11_ofdm_group[i]));
		mcs = b43_ppr_load_mcs(ppr, bw, B43_PPR_SISO);
		for (i = 0; i < B43_PPR_MCS_RATES; i++)
			mcs[i] = b43_ppr_sub(top, b43_ppr_off(rowpo[bw],
						b43_ppr_r11_mcs_group[i]));
	}
}

u8 b43_ppr_load_sprom_r11(struct b43_ppr *ppr, const struct ssbn_sprom *sprom,
			  u8 coremask, unsigned int num_cores, u16 chan,
			  enum nl80211_chan_width width, u8 ceiling)
{
	unsigned int sb = b43_ppr_sprom_r11_subband(chan, width);
	unsigned int c;
	u8 maxp = 0xff;

	for (c = 0; c < num_cores; c++) {
		const struct ssbn_sprom_core_pwr_info *pi;

		if (c >= ARRAY_SIZE(sprom->core_pwr_info))
			break;
		if (!(coremask & BIT(c)))
			continue;
		pi = &sprom->core_pwr_info[c];
		maxp = min(maxp, b43_ppr_r11_is_2g(chan) ? pi->maxp2ga
							 : pi->maxp5ga[sb]);
	}
	if (maxp == 0xff)
		maxp = 0;

	/*
	 * The regulatory ceiling caps each finished rate, so the rates that
	 * would sit above it saturate into one another. It is what the stock
	 * driver shows of itself: `wl curpower` on the DSL-3580L at ch52/80
	 * (router-data/dsl3580l/wl1_curpower_ch52-bw80.txt) has every power
	 * target at min(board limit, regulatory limit) - 1.5 dB, with OFDM6 to
	 * OFDM24 all at 15.0 under a 16.5 limit and a board ramp of 0 to 2 dB.
	 * The tg789vac's 0x38 on ch36-48 at 20 MHz points the same way: its
	 * row starts 1 dB below maxp, and only a cap on the rows gives the
	 * value the d6220 writes there. On the d6220's sweep the two forms give
	 * the same stream on every segment where a ceiling binds. The returned
	 * maxp is the uncapped one, because the caller's per-core add-back is a
	 * distance to that core's own maxp5ga.
	 */
	b43_ppr_r11_fill(ppr, sprom, chan, width, maxp);
	if (ceiling)
		b43_ppr_apply_max(ppr, ceiling);
	return maxp;
}

/*
 * The rate spacing of the channel's SROM table, with nothing saturated: the
 * rows filled from the top of the scale, so no rate hits zero. Where the
 * power table saturates -- a sub-band whose maxp5ga is 0 -- the distances
 * between rates are still the SROM's, and those are what the vendor writes
 * in the per-rate offsets. Everywhere else the two tables differ by a
 * constant and give the same distances.
 */
void b43_ppr_load_sprom_r11_spacing(struct b43_ppr *ppr,
				    const struct ssbn_sprom *sprom, u16 chan,
				    enum nl80211_chan_width width)
{
	b43_ppr_r11_fill(ppr, sprom, chan, width, 0x7f);
}

/*
 * Rev 11 also carries explicit offsets for the sub-band rows of a bonded
 * channel -- 20-in-40, 20-in-80/160, 40-in-80, duplicate legacy, and the MCS
 * 8-9 pair -- in low-rate and high-rate halves. They are all zero on every
 * board captured, so which nibble lands on which rate is not established and
 * the loader does not add them. A board where they are not zero is one this
 * table does not describe yet.
 */
bool b43_ppr_sprom_r11_has_subband_po(const struct ssbn_sprom *sprom)
{
	unsigned int i;

	if (sprom->sb20in40hrpo || sprom->sb20in40lrpo ||
	    sprom->dot11agduphrpo || sprom->dot11agduplrpo)
		return true;
	for (i = 0; i < 3; i++)
		if (sprom->mcslr5gpo[i] || sprom->sb20in80and160hr5gpo[i] ||
		    sprom->sb20in80and160lr5gpo[i] ||
		    sprom->sb40and80hr5gpo[i] || sprom->sb40and80lr5gpo[i])
			return true;
	return false;
}
