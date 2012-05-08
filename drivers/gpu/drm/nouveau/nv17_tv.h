/*
 * Copyright (C) 2009 Francisco Jerez.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __NV17_TV_H__
#define __NV17_TV_H__

struct nv17_tv_state {
	u8 tv_enc[0x40];

	u32 hfilter[4][7];
	u32 hfilter2[4][7];
	u32 vfilter[4][7];

	u32 ptv_200;
	u32 ptv_204;
	u32 ptv_208;
	u32 ptv_20c;
	u32 ptv_304;
	u32 ptv_500;
	u32 ptv_504;
	u32 ptv_508;
	u32 ptv_600;
	u32 ptv_604;
	u32 ptv_608;
	u32 ptv_60c;
	u32 ptv_610;
	u32 ptv_614;
};

enum nv17_tv_norm{
	TV_NORM_PAL,
	TV_NORM_PAL_M,
	TV_NORM_PAL_N,
	TV_NORM_PAL_NC,
	TV_NORM_NTSC_M,
	TV_NORM_NTSC_J,
	NUM_LD_TV_NORMS,
	TV_NORM_HD480I = NUM_LD_TV_NORMS,
	TV_NORM_HD480P,
	TV_NORM_HD576I,
	TV_NORM_HD576P,
	TV_NORM_HD720P,
	TV_NORM_HD1080I,
	NUM_TV_NORMS
};

struct nv17_tv_encoder {
	struct nouveau_encoder base;

	struct nv17_tv_state state;
	struct nv17_tv_state saved_state;

	int overscan;
	int flicker;
	int saturation;
	int hue;
	enum nv17_tv_norm tv_norm;
	int subconnector;
	int select_subconnector;
	u32 pin_mask;
};
#define to_tv_enc(x) container_of(nouveau_encoder(x),		\
				  struct nv17_tv_encoder, base)

extern char *nv17_tv_norm_names[NUM_TV_NORMS];

extern struct nv17_tv_norm_params {
	enum {
		TV_ENC_MODE,
		CTV_ENC_MODE,
	} kind;

	union {
		struct {
			int hdisplay;
			int vdisplay;
			int vrefresh; /* mHz */

			u8 tv_enc[0x40];
		} tv_enc_mode;

		struct {
			struct drm_display_mode mode;

			u32 ctv_regs[38];
		} ctv_enc_mode;
	};

} nv17_tv_norms[NUM_TV_NORMS];
#define get_tv_norm(enc) (&nv17_tv_norms[to_tv_enc(enc)->tv_norm])

extern const struct drm_display_mode nv17_tv_modes[];

static inline int interpolate(int y0, int y1, int y2, int x)
{
	return y1 + (x < 50 ? y1 - y0 : y2 - y1) * (x - 50) / 50;
}

void nv17_tv_state_save(struct nouveau_device *, struct nv17_tv_state *state);
void nv17_tv_state_load(struct nouveau_device *, struct nv17_tv_state *state);
void nv17_tv_update_properties(struct drm_encoder *encoder);
void nv17_tv_update_rescaler(struct drm_encoder *encoder);
void nv17_ctv_update_rescaler(struct drm_encoder *encoder);

/* TV hardware access functions */

static inline void nv_write_ptv(struct nouveau_device *ndev, u32 reg, u32 val)
{
	nv_wr32(ndev, reg, val);
}

static inline u32 nv_read_ptv(struct nouveau_device *ndev, u32 reg)
{
	return nv_rd32(ndev, reg);
}

static inline void nv_write_tv_enc(struct nouveau_device *ndev, u8 reg, u8 val)
{
	nv_write_ptv(ndev, NV_PTV_TV_INDEX, reg);
	nv_write_ptv(ndev, NV_PTV_TV_DATA, val);
}

static inline u8 nv_read_tv_enc(struct nouveau_device *ndev, u8 reg)
{
	nv_write_ptv(ndev, NV_PTV_TV_INDEX, reg);
	return nv_read_ptv(ndev, NV_PTV_TV_DATA);
}

#define nv_load_ptv(dev, state, reg) \
	nv_write_ptv(dev, NV_PTV_OFFSET + 0x##reg, state->ptv_##reg)
#define nv_save_ptv(dev, state, reg) \
	state->ptv_##reg = nv_read_ptv(dev, NV_PTV_OFFSET + 0x##reg)
#define nv_load_tv_enc(dev, state, reg) \
	nv_write_tv_enc(dev, 0x##reg, state->tv_enc[0x##reg])

#endif
