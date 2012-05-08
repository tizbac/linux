/*
 * Copyright 2008 Stuart Bennett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __NOUVEAU_HW_H__
#define __NOUVEAU_HW_H__

#include "drmP.h"
#include "nouveau_drv.h"

#define MASK(field) ( \
	(0xffffffff >> (31 - ((1 ? field) - (0 ? field)))) << (0 ? field))

#define XLATE(src, srclowbit, outfield) ( \
	(((src) >> (srclowbit)) << (0 ? outfield)) & MASK(outfield))

void NVWriteVgaSeq(struct nouveau_device *, int head, u8 index, u8 value);
u8 NVReadVgaSeq(struct nouveau_device *, int head, u8 index);
void NVWriteVgaGr(struct nouveau_device *, int head, u8 index, u8 value);
u8 NVReadVgaGr(struct nouveau_device *, int head, u8 index);
void NVSetOwner(struct nouveau_device *, int owner);
void NVBlankScreen(struct nouveau_device *, int head, bool blank);
void nouveau_hw_setpll(struct nouveau_device *, u32 reg1,
		       struct nouveau_pll_vals *pv);
int nouveau_hw_get_pllvals(struct nouveau_device *, enum pll_types plltype,
			   struct nouveau_pll_vals *pllvals);
int nouveau_hw_pllvals_to_clk(struct nouveau_pll_vals *pllvals);
int nouveau_hw_get_clock(struct nouveau_device *, enum pll_types plltype);
void nouveau_hw_save_vga_fonts(struct nouveau_device *, bool save);
void nouveau_hw_save_state(struct nouveau_device *, int head,
			   struct nv04_mode_state *state);
void nouveau_hw_load_state(struct nouveau_device *, int head,
			   struct nv04_mode_state *state);
void nouveau_hw_load_state_palette(struct nouveau_device *, int head,
				   struct nv04_mode_state *state);

/* nouveau_calc.c */
void nouveau_calc_arb(struct nouveau_device *, int vclk, int bpp,
		      int *burst, int *lwm);
int nouveau_calc_pll_mnp(struct nouveau_device *, struct pll_lims *pll_lim,
			 int clk, struct nouveau_pll_vals *pv);


static inline u32
NVReadCRTC(struct nouveau_device *ndev, int head, u32 reg)
{
	u32 val;
	if (head)
		reg += NV_PCRTC0_SIZE;
	val = nv_rd32(ndev, reg);
	return val;
}

static inline void
NVWriteCRTC(struct nouveau_device *ndev, int head, u32 reg, u32 val)
{
	if (head)
		reg += NV_PCRTC0_SIZE;
	nv_wr32(ndev, reg, val);
}

static inline u32
NVReadRAMDAC(struct nouveau_device *ndev, int head, u32 reg)
{
	u32 val;
	if (head)
		reg += NV_PRAMDAC0_SIZE;
	val = nv_rd32(ndev, reg);
	return val;
}

static inline void
NVWriteRAMDAC(struct nouveau_device *ndev, int head, u32 reg, u32 val)
{
	if (head)
		reg += NV_PRAMDAC0_SIZE;
	nv_wr32(ndev, reg, val);
}

static inline u8
nv_read_tmds(struct nouveau_device *ndev, int or, int dl, u8 address)
{
	int ramdac = (or & OUTPUT_C) >> 2;

	NVWriteRAMDAC(ndev, ramdac, NV_PRAMDAC_FP_TMDS_CONTROL + dl * 8,
	NV_PRAMDAC_FP_TMDS_CONTROL_WRITE_DISABLE | address);
	return NVReadRAMDAC(ndev, ramdac, NV_PRAMDAC_FP_TMDS_DATA + dl * 8);
}

static inline void
nv_write_tmds(struct nouveau_device *ndev, int or, int dl, u8 address, u8 data)
{
	int ramdac = (or & OUTPUT_C) >> 2;

	NVWriteRAMDAC(ndev, ramdac, NV_PRAMDAC_FP_TMDS_DATA + dl * 8, data);
	NVWriteRAMDAC(ndev, ramdac, NV_PRAMDAC_FP_TMDS_CONTROL + dl * 8, address);
}

static inline void
NVWriteVgaCrtc(struct nouveau_device *ndev, int head, u8 index, u8 value)
{
	nv_wr08(ndev, NV_PRMCIO_CRX__COLOR + head * NV_PRMCIO_SIZE, index);
	nv_wr08(ndev, NV_PRMCIO_CR__COLOR + head * NV_PRMCIO_SIZE, value);
}

static inline u8
NVReadVgaCrtc(struct nouveau_device *ndev, int head, u8 index)
{
	u8 val;
	nv_wr08(ndev, NV_PRMCIO_CRX__COLOR + head * NV_PRMCIO_SIZE, index);
	val = nv_rd08(ndev, NV_PRMCIO_CR__COLOR + head * NV_PRMCIO_SIZE);
	return val;
}

/* CR57 and CR58 are a fun pair of regs. CR57 provides an index (0-0xf) for CR58
 * I suspect they in fact do nothing, but are merely a way to carry useful
 * per-head variables around
 *
 * Known uses:
 * CR57		CR58
 * 0x00		index to the appropriate dcb entry (or 7f for inactive)
 * 0x02		dcb entry's "or" value (or 00 for inactive)
 * 0x03		bit0 set for dual link (LVDS, possibly elsewhere too)
 * 0x08 or 0x09	pxclk in MHz
 * 0x0f		laptop panel info -	low nibble for PEXTDEV_BOOT_0 strap
 * 					high nibble for xlat strap value
 */

static inline void
NVWriteVgaCrtc5758(struct nouveau_device *ndev, int head, u8 index, u8 value)
{
	NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_57, index);
	NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_58, value);
}

static inline u8
NVReadVgaCrtc5758(struct nouveau_device *ndev, int head, u8 index)
{
	NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_57, index);
	return NVReadVgaCrtc(ndev, head, NV_CIO_CRE_58);
}

static inline u8
NVReadPRMVIO(struct nouveau_device *ndev, int head, u32 reg)
{
	u8 val;

	/* Only NV4x have two pvio ranges; other twoHeads cards MUST call
	 * NVSetOwner for the relevant head to be programmed */
	if (head && ndev->card_type == NV_40)
		reg += NV_PRMVIO_SIZE;

	val = nv_rd08(ndev, reg);
	return val;
}

static inline void
NVWritePRMVIO(struct nouveau_device *ndev, int head, u32 reg, u8 value)
{
	/* Only NV4x have two pvio ranges; other twoHeads cards MUST call
	 * NVSetOwner for the relevant head to be programmed */
	if (head && ndev->card_type == NV_40)
		reg += NV_PRMVIO_SIZE;

	nv_wr08(ndev, reg, value);
}

static inline void
NVSetEnablePalette(struct nouveau_device *ndev, int head, bool enable)
{
	nv_rd08(ndev, NV_PRMCIO_INP0__COLOR + head * NV_PRMCIO_SIZE);
	nv_wr08(ndev, NV_PRMCIO_ARX + head * NV_PRMCIO_SIZE, enable ? 0 : 0x20);
}

static inline bool
NVGetEnablePalette(struct nouveau_device *ndev, int head)
{
	nv_rd08(ndev, NV_PRMCIO_INP0__COLOR + head * NV_PRMCIO_SIZE);
	return !(nv_rd08(ndev, NV_PRMCIO_ARX + head * NV_PRMCIO_SIZE) & 0x20);
}

static inline void
NVWriteVgaAttr(struct nouveau_device *ndev, int head, u8 index, u8 value)
{
	if (NVGetEnablePalette(ndev, head))
		index &= ~0x20;
	else
		index |= 0x20;

	nv_rd08(ndev, NV_PRMCIO_INP0__COLOR + head * NV_PRMCIO_SIZE);
	nv_wr08(ndev, NV_PRMCIO_ARX + head * NV_PRMCIO_SIZE, index);
	nv_wr08(ndev, NV_PRMCIO_AR__WRITE + head * NV_PRMCIO_SIZE, value);
}

static inline u8
NVReadVgaAttr(struct nouveau_device *ndev, int head, u8 index)
{
	u8 val;
	if (NVGetEnablePalette(ndev, head))
		index &= ~0x20;
	else
		index |= 0x20;

	nv_rd08(ndev, NV_PRMCIO_INP0__COLOR + head * NV_PRMCIO_SIZE);
	nv_wr08(ndev, NV_PRMCIO_ARX + head * NV_PRMCIO_SIZE, index);
	val = nv_rd08(ndev, NV_PRMCIO_AR__READ + head * NV_PRMCIO_SIZE);
	return val;
}

static inline void
NVVgaSeqReset(struct nouveau_device *ndev, int head, bool start)
{
	NVWriteVgaSeq(ndev, head, NV_VIO_SR_RESET_INDEX, start ? 0x1 : 0x3);
}

static inline void
NVVgaProtect(struct nouveau_device *ndev, int head, bool protect)
{
	u8 seq1 = NVReadVgaSeq(ndev, head, NV_VIO_SR_CLOCK_INDEX);

	if (protect) {
		NVVgaSeqReset(ndev, head, true);
		NVWriteVgaSeq(ndev, head, NV_VIO_SR_CLOCK_INDEX, seq1 | 0x20);
	} else {
		/* Reenable sequencer, then turn on screen */
		NVWriteVgaSeq(ndev, head, NV_VIO_SR_CLOCK_INDEX, seq1 & ~0x20);   /* reenable display */
		NVVgaSeqReset(ndev, head, false);
	}
	NVSetEnablePalette(ndev, head, protect);
}

static inline bool
nv_heads_tied(struct nouveau_device *ndev)
{
	if (ndev->chipset == 0x11)
		return !!(nv_rd32(ndev, NV_PBUS_DEBUG_1) & (1 << 28));

	return NVReadVgaCrtc(ndev, 0, NV_CIO_CRE_44) & 0x4;
}

/* makes cr0-7 on the specified head read-only */
static inline bool
nv_lock_vga_crtc_base(struct nouveau_device *ndev, int head, bool lock)
{
	u8 cr11 = NVReadVgaCrtc(ndev, head, NV_CIO_CR_VRE_INDEX);
	bool waslocked = cr11 & 0x80;

	if (lock)
		cr11 |= 0x80;
	else
		cr11 &= ~0x80;
	NVWriteVgaCrtc(ndev, head, NV_CIO_CR_VRE_INDEX, cr11);

	return waslocked;
}

static inline void
nv_lock_vga_crtc_shadow(struct nouveau_device *ndev, int head, int lock)
{
	/* shadow lock: connects 0x60?3d? regs to "real" 0x3d? regs
	 * bit7: unlocks HDT, HBS, HBE, HRS, HRE, HEB
	 * bit6: seems to have some effect on CR09 (double scan, VBS_9)
	 * bit5: unlocks HDE
	 * bit4: unlocks VDE
	 * bit3: unlocks VDT, OVL, VRS, ?VRE?, VBS, VBE, LSR, EBR
	 * bit2: same as bit 1 of 0x60?804
	 * bit0: same as bit 0 of 0x60?804
	 */

	u8 cr21 = lock;

	if (lock < 0)
		/* 0xfa is generic "unlock all" mask */
		cr21 = NVReadVgaCrtc(ndev, head, NV_CIO_CRE_21) | 0xfa;

	NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_21, cr21);
}

/* renders the extended crtc regs (cr19+) on all crtcs impervious:
 * immutable and unreadable
 */
static inline bool
NVLockVgaCrtcs(struct nouveau_device *ndev, bool lock)
{
	bool waslocked = !NVReadVgaCrtc(ndev, 0, NV_CIO_SR_LOCK_INDEX);

	NVWriteVgaCrtc(ndev, 0, NV_CIO_SR_LOCK_INDEX,
		       lock ? NV_CIO_SR_LOCK_VALUE : NV_CIO_SR_UNLOCK_RW_VALUE);
	/* NV11 has independently lockable extended crtcs, except when tied */
	if (ndev->chipset == 0x11 && !nv_heads_tied(ndev))
		NVWriteVgaCrtc(ndev, 1, NV_CIO_SR_LOCK_INDEX,
			       lock ? NV_CIO_SR_LOCK_VALUE :
				      NV_CIO_SR_UNLOCK_RW_VALUE);

	return waslocked;
}

/* nv04 cursor max dimensions of 32x32 (A1R5G5B5) */
#define NV04_CURSOR_SIZE 32
/* limit nv10 cursors to 64x64 (ARGB8) (we could go to 64x255) */
#define NV10_CURSOR_SIZE 64

static inline int
nv_cursor_width(struct nouveau_device *ndev)
{
	return ndev->card_type >= NV_10 ? NV10_CURSOR_SIZE : NV04_CURSOR_SIZE;
}

static inline void
nv_fix_nv40_hw_cursor(struct nouveau_device *ndev, int head)
{
	/* on some nv40 (such as the "true" (in the NV_PFB_BOOT_0 sense) nv40,
	 * the gf6800gt) a hardware bug requires a write to PRAMDAC_CURSOR_POS
	 * for changes to the CRTC CURCTL regs to take effect, whether changing
	 * the pixmap location, or just showing/hiding the cursor
	 */
	u32 curpos = NVReadRAMDAC(ndev, head, NV_PRAMDAC_CU_START_POS);
	NVWriteRAMDAC(ndev, head, NV_PRAMDAC_CU_START_POS, curpos);
}

static inline void
nv_set_crtc_base(struct nouveau_device *ndev, int head, u32 offset)
{
	NVWriteCRTC(ndev, head, NV_PCRTC_START, offset);

	if (ndev->card_type == NV_04) {
		/*
		 * Hilarious, the 24th bit doesn't want to stick to
		 * PCRTC_START...
		 */
		int cre_heb = NVReadVgaCrtc(ndev, head, NV_CIO_CRE_HEB__INDEX);

		NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_HEB__INDEX,
			       (cre_heb & ~0x40) | ((offset >> 18) & 0x40));
	}
}

static inline void
nv_show_cursor(struct nouveau_device *ndev, int head, bool show)
{
	u8 *curctl1 =
		&ndev->mode_reg.crtc_reg[head].CRTC[NV_CIO_CRE_HCUR_ADDR1_INDEX];

	if (show)
		*curctl1 |= MASK(NV_CIO_CRE_HCUR_ADDR1_ENABLE);
	else
		*curctl1 &= ~MASK(NV_CIO_CRE_HCUR_ADDR1_ENABLE);
	NVWriteVgaCrtc(ndev, head, NV_CIO_CRE_HCUR_ADDR1_INDEX, *curctl1);

	if (ndev->card_type == NV_40)
		nv_fix_nv40_hw_cursor(ndev, head);
}

static inline u32
nv_pitch_align(struct nouveau_device *ndev, u32 width, int bpp)
{
	int mask;

	if (bpp == 15)
		bpp = 16;
	if (bpp == 24)
		bpp = 8;

	/* Alignment requirements taken from the Haiku driver */
	if (ndev->card_type == NV_04)
		mask = 128 / bpp - 1;
	else
		mask = 512 / bpp - 1;

	return (width + mask) & ~mask;
}

#endif	/* __NOUVEAU_HW_H__ */
