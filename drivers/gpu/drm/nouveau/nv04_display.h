#ifndef __NV04_DISPLAY_H__
#define __NV04_DISPLAY_H__

enum nv04_fp_display_regs {
	FP_DISPLAY_END,
	FP_TOTAL,
	FP_CRTC,
	FP_SYNC_START,
	FP_SYNC_END,
	FP_VALID_START,
	FP_VALID_END
};

struct nv04_crtc_reg {
	unsigned char MiscOutReg;
	u8 CRTC[0xa0];
	u8 CR58[0x10];
	u8 Sequencer[5];
	u8 Graphics[9];
	u8 Attribute[21];
	unsigned char DAC[768];

	/* PCRTC regs */
	u32 fb_start;
	u32 crtc_cfg;
	u32 cursor_cfg;
	u32 gpio_ext;
	u32 crtc_830;
	u32 crtc_834;
	u32 crtc_850;
	u32 crtc_eng_ctrl;

	/* PRAMDAC regs */
	u32 nv10_cursync;
	struct nouveau_pll_vals pllvals;
	u32 ramdac_gen_ctrl;
	u32 ramdac_630;
	u32 ramdac_634;
	u32 tv_setup;
	u32 tv_vtotal;
	u32 tv_vskew;
	u32 tv_vsync_delay;
	u32 tv_htotal;
	u32 tv_hskew;
	u32 tv_hsync_delay;
	u32 tv_hsync_delay2;
	u32 fp_horiz_regs[7];
	u32 fp_vert_regs[7];
	u32 dither;
	u32 fp_control;
	u32 dither_regs[6];
	u32 fp_debug_0;
	u32 fp_debug_1;
	u32 fp_debug_2;
	u32 fp_margin_color;
	u32 ramdac_8c0;
	u32 ramdac_a20;
	u32 ramdac_a24;
	u32 ramdac_a34;
	u32 ctv_regs[38];
};

struct nv04_output_reg {
	u32 output;
	int head;
};

struct nv04_mode_state {
	struct nv04_crtc_reg crtc_reg[2];
	u32 pllsel;
	u32 sel_clk;
};

struct nv04_display {
	struct nv04_mode_state mode_reg;
	struct nv04_mode_state saved_reg;
	u32 saved_vga_font[4][16384];
	u32 crtc_owner;
	u32 dac_users[4];
};

static inline struct nv04_display *
nv04_display(struct nouveau_device *ndev)
{
	return ndev->subsys.display.priv;
}

#endif
