/*
 * Copyright 2007-2008 Nouveau Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_BIOS_H__
#define __NOUVEAU_BIOS_H__

#include "nvreg.h"
#include "nouveau_i2c.h"

#define DCB_MAX_NUM_ENTRIES 16
#define DCB_MAX_NUM_I2C_ENTRIES 16
#define DCB_MAX_NUM_GPIO_ENTRIES 32
#define DCB_MAX_NUM_CONNECTOR_ENTRIES 16

#define DCB_LOC_ON_CHIP 0

#define ROM16(x) le16_to_cpu(*(u16 *)&(x))
#define ROM32(x) le32_to_cpu(*(u32 *)&(x))
#define ROM48(x) ({ u8 *p = &(x); (u64)ROM16(p[4]) << 32 | ROM32(p[0]); })
#define ROM64(x) le64_to_cpu(*(u64 *)&(x))
#define ROMPTR(ndev,x) ({            \
	ROM16(x) ? &(ndev)->vbios.data[ROM16(x)] : NULL; \
})

struct bit_entry {
	u8  id;
	u8  version;
	u16 length;
	u16 offset;
	u8 *data;
};

int bit_table(struct nouveau_device *, u8 id, struct bit_entry *);

enum dcb_gpio_tag {
	DCB_GPIO_PANEL_POWER = 0x01,
	DCB_GPIO_TVDAC0 = 0x0c,
	DCB_GPIO_TVDAC1 = 0x2d,
	DCB_GPIO_PWM_FAN = 0x09,
	DCB_GPIO_FAN_SENSE = 0x3d,
	DCB_GPIO_UNUSED = 0xff
};

enum dcb_connector_type {
	DCB_CONNECTOR_VGA = 0x00,
	DCB_CONNECTOR_TV_0 = 0x10,
	DCB_CONNECTOR_TV_1 = 0x11,
	DCB_CONNECTOR_TV_3 = 0x13,
	DCB_CONNECTOR_DVI_I = 0x30,
	DCB_CONNECTOR_DVI_D = 0x31,
	DCB_CONNECTOR_DMS59_0 = 0x38,
	DCB_CONNECTOR_DMS59_1 = 0x39,
	DCB_CONNECTOR_LVDS = 0x40,
	DCB_CONNECTOR_LVDS_SPWG = 0x41,
	DCB_CONNECTOR_DP = 0x46,
	DCB_CONNECTOR_eDP = 0x47,
	DCB_CONNECTOR_HDMI_0 = 0x60,
	DCB_CONNECTOR_HDMI_1 = 0x61,
	DCB_CONNECTOR_DMS59_DP0 = 0x64,
	DCB_CONNECTOR_DMS59_DP1 = 0x65,
	DCB_CONNECTOR_NONE = 0xff
};

enum dcb_type {
	OUTPUT_ANALOG = 0,
	OUTPUT_TV = 1,
	OUTPUT_TMDS = 2,
	OUTPUT_LVDS = 3,
	OUTPUT_DP = 6,
	OUTPUT_EOL = 14, /* DCB 4.0+, appears to be end-of-list */
	OUTPUT_UNUSED = 15,
	OUTPUT_ANY = -1
};

struct dcb_entry {
	int index;	/* may not be raw dcb index if merging has happened */
	enum dcb_type type;
	u8 i2c_index;
	u8 heads;
	u8 connector;
	u8 bus;
	u8 location;
	u8 or;
	bool duallink_possible;
	union {
		struct sor_conf {
			int link;
		} sorconf;
		struct {
			int maxfreq;
		} crtconf;
		struct {
			struct sor_conf sor;
			bool use_straps_for_mode;
			bool use_acpi_for_edid;
			bool use_power_scripts;
		} lvdsconf;
		struct {
			bool has_component_output;
		} tvconf;
		struct {
			struct sor_conf sor;
			int link_nr;
			int link_bw;
		} dpconf;
		struct {
			struct sor_conf sor;
			int slave_addr;
		} tmdsconf;
	};
	bool i2c_upper_default;
};

struct dcb_table {
	u8 version;
	int entries;
	struct dcb_entry entry[DCB_MAX_NUM_ENTRIES];
};

enum nouveau_or {
	OUTPUT_A = (1 << 0),
	OUTPUT_B = (1 << 1),
	OUTPUT_C = (1 << 2)
};

enum LVDS_script {
	/* Order *does* matter here */
	LVDS_INIT = 1,
	LVDS_RESET,
	LVDS_BACKLIGHT_ON,
	LVDS_BACKLIGHT_OFF,
	LVDS_PANEL_ON,
	LVDS_PANEL_OFF
};

/* these match types in pll limits table version 0x40,
 * nouveau uses them on all chipsets internally where a
 * specific pll needs to be referenced, but the exact
 * register isn't known.
 */
enum pll_types {
	PLL_CORE   = 0x01,
	PLL_SHADER = 0x02,
	PLL_UNK03  = 0x03,
	PLL_MEMORY = 0x04,
	PLL_VDEC   = 0x05,
	PLL_UNK40  = 0x40,
	PLL_UNK41  = 0x41,
	PLL_UNK42  = 0x42,
	PLL_VPLL0  = 0x80,
	PLL_VPLL1  = 0x81,
	PLL_MAX    = 0xff
};

struct pll_lims {
	u32 reg;

	struct {
		int minfreq;
		int maxfreq;
		int min_inputfreq;
		int max_inputfreq;

		u8 min_m;
		u8 max_m;
		u8 min_n;
		u8 max_n;
	} vco1, vco2;

	u8 max_log2p;
	/*
	 * for most pre nv50 cards setting a log2P of 7 (the common max_log2p
	 * value) is no different to 6 (at least for vplls) so allowing the MNP
	 * calc to use 7 causes the generated clock to be out by a factor of 2.
	 * however, max_log2p cannot be fixed-up during parsing as the
	 * unmodified max_log2p value is still needed for setting mplls, hence
	 * an additional max_usable_log2p member
	 */
	u8 max_usable_log2p;
	u8 log2p_bias;

	u8 min_p;
	u8 max_p;

	int refclk;
};

struct nvbios {
	struct nouveau_device *device;
	enum {
		NVBIOS_BMP,
		NVBIOS_BIT
	} type;
	u16 offset;
	u32 length;
	u8 *data;

	u8 chip_version;

	u32 dactestval;
	u32 tvdactestval;
	u8 digital_min_front_porch;
	bool fp_no_ddc;

	spinlock_t lock;

	bool execute;

	u8 major_version;
	u8 feature_byte;
	bool is_mobile;

	u32 fmaxvco, fminvco;

	bool old_style_init;
	u16 init_script_tbls_ptr;
	u16 extra_init_script_tbl_ptr;
	u16 macro_index_tbl_ptr;
	u16 macro_tbl_ptr;
	u16 condition_tbl_ptr;
	u16 io_condition_tbl_ptr;
	u16 io_flag_condition_tbl_ptr;
	u16 init_function_tbl_ptr;

	u16 pll_limit_tbl_ptr;
	u16 ram_restrict_tbl_ptr;
	u8 ram_restrict_group_count;

	u16 some_script_ptr; /* BIT I + 14 */
	u16 init96_tbl_ptr; /* BIT I + 16 */

	struct dcb_table dcb;

	struct {
		int crtchead;
	} state;

	struct {
		struct dcb_entry *output;
		int crtc;
		u16 script_table_ptr;
	} display;

	struct {
		u16 fptablepointer;	/* also used by tmds */
		u16 fpxlatetableptr;
		int xlatwidth;
		u16 lvdsmanufacturerpointer;
		u16 fpxlatemanufacturertableptr;
		u16 mode_ptr;
		u16 xlated_entry;
		bool power_off_for_reset;
		bool reset_after_pclk_change;
		bool dual_link;
		bool link_c_increment;
		bool if_is_24bit;
		int duallink_transition_clk;
		u8 strapless_is_24bit;
		u8 *edid;

		/* will need resetting after suspend */
		int last_script_invoc;
		bool lvds_init_run;
	} fp;

	struct {
		u16 output0_script_ptr;
		u16 output1_script_ptr;
	} tmds;

	struct {
		u16 mem_init_tbl_ptr;
		u16 sdr_seq_tbl_ptr;
		u16 ddr_seq_tbl_ptr;

		struct {
			u8 crt, tv, panel;
		} i2c_indices;

		u16 lvds_single_a_script_ptr;
	} legacy;
};

void *dcb_table(struct nouveau_device *);
void *dcb_outp(struct nouveau_device *, u8 idx);
int dcb_outp_foreach(struct nouveau_device *, void *data,
		     int (*)(struct nouveau_device *, void *, int idx, u8 *outp));
u8 *dcb_conntab(struct nouveau_device *);
u8 *dcb_conn(struct nouveau_device *, u8 idx);

#endif
