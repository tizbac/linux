/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_DRV_H__
#define __NOUVEAU_DRV_H__

#define DRIVER_AUTHOR		"Stephane Marchesin"
#define DRIVER_EMAIL		"nouveau@lists.freedesktop.org"

#define DRIVER_NAME		"nouveau"
#define DRIVER_DESC		"nVidia Riva/TNT/GeForce"
#define DRIVER_DATE		"20120316"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define NOUVEAU_FAMILY   0x0000FFFF
#define NOUVEAU_FLAGS    0xFFFF0000

#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include "ttm/ttm_memory.h"
#include "ttm/ttm_module.h"

struct nouveau_fpriv {
	spinlock_t lock;
	struct list_head channels;
	struct nouveau_vm *vm;
};

static inline struct nouveau_fpriv *
nouveau_fpriv(struct drm_file *file_priv)
{
	return file_priv ? file_priv->driver_priv : NULL;
}

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#define NVDEV_SUBDEV_VBIOS 0
#define NVDEV_SUBDEV_MC    1
#define NVDEV_SUBDEV_TIMER 2
#define NVDEV_SUBDEV_FB    3
#define NVDEV_SUBDEV_GPIO  4
#define NVDEV_SUBDEV_NR    32
struct nouveau_device;
struct nouveau_subdev {
	struct nouveau_device *device;
	const char *name;
	enum {
		NVDEV_SUBDEV_CREATED,
		NVDEV_SUBDEV_SUSPEND,
		NVDEV_SUBDEV_STOPPED,
		NVDEV_SUBDEV_RUNNING,
	} state;

	void (*destroy)(struct nouveau_device *, int subdev);
	int  (*init)(struct nouveau_device *, int subdev);
	int  (*fini)(struct nouveau_device *, int subdev, bool suspend);
};

#define nouveau_subdev_create(ndev,sdev,sstr,fstr,data)                        \
	nouveau_subdev_create_((ndev), (sdev), sizeof(**data),                 \
			       (sstr), (fstr), (void **)data)

int  nouveau_subdev_create_(struct nouveau_device *, int subdev, int length,
			    const char *sname, const char *fname, void **);
void nouveau_subdev_destroy(struct nouveau_device *, int subdev);
int  nouveau_subdev_init(struct nouveau_device *, int subdev, int ret);
int  nouveau_subdev_fini(struct nouveau_device *, int subdev, bool suspend);

#include "nouveau_drm.h"
#include "nouveau_reg.h"
#include "nouveau_util.h"
#include "nouveau_bios.h"

struct nouveau_grctx;
struct nouveau_mem;
#include "nouveau_vm.h"

#define MAX_NUM_DCB_ENTRIES 16

#define NOUVEAU_MAX_CHANNEL_NR 4096
#define NOUVEAU_MAX_TILE_NR 15

struct nouveau_mem {
	struct nouveau_device *device;

	struct nouveau_vma bar_vma;
	struct nouveau_vma vma[2];
	u8  page_shift;

	struct drm_mm_node *tag;
	struct list_head regions;
	dma_addr_t *pages;
	u32 memtype;
	u64 offset;
	u64 size;
	struct sg_table *sg;
};

struct nouveau_tile_reg {
	bool used;
	u32 addr;
	u32 limit;
	u32 pitch;
	u32 zcomp;
	struct drm_mm_node *tag_mem;
	struct nouveau_fence *fence;
};

struct nouveau_bo {
	struct ttm_buffer_object bo;
	struct ttm_placement placement;
	u32 valid_domains;
	u32 placements[3];
	u32 busy_placements[3];
	struct ttm_bo_kmap_obj kmap;
	struct list_head head;

	/* protected by ttm_bo_reserve() */
	struct drm_file *reserved_by;
	struct list_head entry;
	int pbbo_index;
	bool validate_mapped;

	struct list_head vma_list;
	unsigned page_shift;

	u32 tile_mode;
	u32 tile_flags;
	struct nouveau_tile_reg *tile;

	struct drm_gem_object *gem;
	int pin_refcnt;

	struct ttm_bo_kmap_obj dma_buf_vmap;
	int vmapping_count;
};

#define nouveau_bo_tile_layout(nvbo)				\
	((nvbo)->tile_flags & NOUVEAU_GEM_TILE_LAYOUT_MASK)

static inline struct nouveau_bo *
nouveau_bo(struct ttm_buffer_object *bo)
{
	return container_of(bo, struct nouveau_bo, bo);
}

static inline struct nouveau_bo *
nouveau_gem_object(struct drm_gem_object *gem)
{
	return gem ? gem->driver_private : NULL;
}

/* TODO: submit equivalent to TTM generic API upstream? */
static inline void __iomem *
nvbo_kmap_obj_iovirtual(struct nouveau_bo *nvbo)
{
	bool is_iomem;
	void __iomem *ioptr = (void __force __iomem *)ttm_kmap_obj_virtual(
						&nvbo->kmap, &is_iomem);
	WARN_ON_ONCE(ioptr && !is_iomem);
	return ioptr;
}

enum nouveau_flags {
	NV_NFORCE   = 0x10000000,
	NV_NFORCE2  = 0x20000000
};

#define NVOBJ_ENGINE_SW		0
#define NVOBJ_ENGINE_GR		1
#define NVOBJ_ENGINE_CRYPT	2
#define NVOBJ_ENGINE_COPY0	3
#define NVOBJ_ENGINE_COPY1	4
#define NVOBJ_ENGINE_MPEG	5
#define NVOBJ_ENGINE_PPP	NVOBJ_ENGINE_MPEG
#define NVOBJ_ENGINE_BSP	6
#define NVOBJ_ENGINE_VP		7
#define NVOBJ_ENGINE_FIFO	14
#define NVOBJ_ENGINE_FENCE	15
#define NVOBJ_ENGINE_NR		16
#define NVOBJ_ENGINE_DISPLAY	(NVOBJ_ENGINE_NR + 0) /*XXX*/

#define NVOBJ_FLAG_DONT_MAP             (1 << 0)
#define NVOBJ_FLAG_ZERO_ALLOC		(1 << 1)
#define NVOBJ_FLAG_ZERO_FREE		(1 << 2)
#define NVOBJ_FLAG_VM			(1 << 3)
#define NVOBJ_FLAG_VM_USER		(1 << 4)

#define NVOBJ_CINST_GLOBAL	0xdeadbeef

struct nouveau_gpuobj {
	struct nouveau_device *device;
	struct kref refcount;
	struct list_head list;

	void *node;
	u32 *suspend;

	u32 flags;

	u32 size;
	u32 pinst;	/* PRAMIN BAR offset */
	u32 cinst;	/* Channel offset */
	u64 vinst;	/* VRAM address */
	u64 linst;	/* VM address */

	u32 engine;
	u32 class;

	void (*dtor)(struct nouveau_device *, struct nouveau_gpuobj *);
	void *priv;
};

struct nouveau_page_flip_state {
	struct list_head head;
	struct drm_pending_vblank_event *event;
	int crtc, bpp, pitch, x, y;
	u64 offset;
};

enum nouveau_channel_mutex_class {
	NOUVEAU_UCHANNEL_MUTEX,
	NOUVEAU_KCHANNEL_MUTEX
};

struct nouveau_channel {
	struct nouveau_device *device;
	struct list_head list;
	int id;

	/* references to the channel data structure */
	struct kref ref;
	/* users of the hardware channel resources, the hardware
	 * context will be kicked off when it reaches zero. */
	atomic_t users;
	struct mutex mutex;

	/* owner of this fifo */
	struct drm_file *file_priv;
	/* mapping of the fifo itself */
	struct drm_local_map *map;

	/* mapping of the regs controlling the fifo */
	void __iomem *user;
	u32 user_get;
	u32 user_get_hi;
	u32 user_put;

	/* DMA push buffer */
	struct nouveau_gpuobj *pushbuf;
	struct nouveau_bo     *pushbuf_bo;
	struct nouveau_vma     pushbuf_vma;
	u64               pushbuf_base;

	/* Notifier memory */
	struct nouveau_bo *notifier_bo;
	struct nouveau_vma notifier_vma;
	struct drm_mm notifier_heap;

	/* PFIFO context */
	struct nouveau_gpuobj *ramfc;

	/* Execution engine contexts */
	void *engctx[NVOBJ_ENGINE_NR];

	/* NV50 VM */
	struct nouveau_vm     *vm;
	struct nouveau_gpuobj *vm_pd;

	/* Objects */
	struct nouveau_gpuobj *ramin; /* Private instmem */
	struct drm_mm          ramin_heap; /* Private PRAMIN heap */
	struct nouveau_ramht  *ramht; /* Hash table */

	/* GPU object info for stuff used in-kernel (mm_enabled) */
	u32 m2mf_ntfy;
	u32 vram_handle;
	u32 gart_handle;
	bool accel_done;

	/* Push buffer state (only for drm's channel on !mm_enabled) */
	struct {
		int max;
		int free;
		int cur;
		int put;
		/* access via pushbuf_bo */

		int ib_base;
		int ib_max;
		int ib_free;
		int ib_put;
	} dma;

	struct {
		bool active;
		char name[32];
		struct drm_info_list info;
	} debugfs;
};

struct nouveau_engine {
	void (*destroy)(struct nouveau_device *, int engine);
	int  (*init)(struct nouveau_device *, int engine);
	int  (*fini)(struct nouveau_device *, int engine, bool suspend);
	int  (*context_new)(struct nouveau_channel *, int engine);
	void (*context_del)(struct nouveau_channel *, int engine);
	int  (*object_new)(struct nouveau_channel *, int engine,
			   u32 handle, u16 class);
	void (*set_tile_region)(struct nouveau_device *, int i);
	void (*tlb_flush)(struct nouveau_device *, int engine);
};

struct nouveau_instmem_engine {
	void	*priv;

	int	(*init)(struct nouveau_device *);
	void	(*takedown)(struct nouveau_device *);
	int	(*suspend)(struct nouveau_device *);
	void	(*resume)(struct nouveau_device *);

	int	(*get)(struct nouveau_gpuobj *, struct nouveau_channel *,
		       u32 size, u32 align);
	void	(*put)(struct nouveau_gpuobj *);
	int	(*map)(struct nouveau_gpuobj *);
	void	(*unmap)(struct nouveau_gpuobj *);

	void	(*flush)(struct nouveau_device *);
};

struct nouveau_display_engine {
	void *priv;
	int (*early_init)(struct nouveau_device *);
	void (*late_takedown)(struct nouveau_device *);
	int (*create)(struct nouveau_device *);
	void (*destroy)(struct nouveau_device *);
	int (*init)(struct nouveau_device *);
	void (*fini)(struct nouveau_device *);

	struct drm_property *dithering_mode;
	struct drm_property *dithering_depth;
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* not really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;
};

struct nouveau_pm_voltage_level {
	u32 voltage; /* microvolts */
	u8  vid;
};

struct nouveau_pm_voltage {
	bool supported;
	u8 version;
	u8 vid_mask;

	struct nouveau_pm_voltage_level *level;
	int nr_level;
};

/* Exclusive upper limits */
#define NV_MEM_CL_DDR2_MAX 8
#define NV_MEM_WR_DDR2_MAX 9
#define NV_MEM_CL_DDR3_MAX 17
#define NV_MEM_WR_DDR3_MAX 17
#define NV_MEM_CL_GDDR3_MAX 16
#define NV_MEM_WR_GDDR3_MAX 18
#define NV_MEM_CL_GDDR5_MAX 21
#define NV_MEM_WR_GDDR5_MAX 20

struct nouveau_pm_memtiming {
	int id;

	u32 reg[9];
	u32 mr[4];

	u8 tCWL;

	u8 odt;
	u8 drive_strength;
};

struct nouveau_pm_tbl_header {
	u8 version;
	u8 header_len;
	u8 entry_cnt;
	u8 entry_len;
};

struct nouveau_pm_tbl_entry {
	u8 tWR;
	u8 tWTR;
	u8 tCL;
	u8 tRC;
	u8 empty_4;
	u8 tRFC;	/* Byte 5 */
	u8 empty_6;
	u8 tRAS;	/* Byte 7 */
	u8 empty_8;
	u8 tRP;		/* Byte 9 */
	u8 tRCDRD;
	u8 tRCDWR;
	u8 tRRD;
	u8 tUNK_13;
	u8 RAM_FT1;		/* 14, a bitmask of random RAM features */
	u8 empty_15;
	u8 tUNK_16;
	u8 empty_17;
	u8 tUNK_18;
	u8 tCWL;
	u8 tUNK_20, tUNK_21;
};

struct nouveau_pm_profile;
struct nouveau_pm_profile_func {
	void (*destroy)(struct nouveau_pm_profile *);
	void (*init)(struct nouveau_pm_profile *);
	void (*fini)(struct nouveau_pm_profile *);
	struct nouveau_pm_level *(*select)(struct nouveau_pm_profile *);
};

struct nouveau_pm_profile {
	const struct nouveau_pm_profile_func *func;
	struct list_head head;
	char name[8];
};

#define NOUVEAU_PM_MAX_LEVEL 8
struct nouveau_pm_level {
	struct nouveau_pm_profile profile;
	struct device_attribute dev_attr;
	char name[32];
	int id;

	struct nouveau_pm_memtiming timing;
	u32 memory;
	u16 memscript;

	u32 core;
	u32 shader;
	u32 rop;
	u32 copy;
	u32 daemon;
	u32 vdec;
	u32 dom6;
	u32 unka0;	/* nva3:nvc0 */
	u32 hub01;	/* nvc0- */
	u32 hub06;	/* nvc0- */
	u32 hub07;	/* nvc0- */

	u32 volt_min; /* microvolts */
	u32 volt_max;
	u8  fanspeed;
};

struct nouveau_pm_temp_sensor_constants {
	u16 offset_constant;
	s16 offset_mult;
	s16 offset_div;
	s16 slope_mult;
	s16 slope_div;
};

struct nouveau_pm_threshold_temp {
	s16 critical;
	s16 down_clock;
	s16 fan_boost;
};

struct nouveau_pm_fan {
	u32 percent;
	u32 min_duty;
	u32 max_duty;
	u32 pwm_freq;
	u32 pwm_divisor;
};

struct nouveau_pm_engine {
	struct nouveau_pm_voltage voltage;
	struct nouveau_pm_level perflvl[NOUVEAU_PM_MAX_LEVEL];
	int nr_perflvl;
	struct nouveau_pm_temp_sensor_constants sensor_constants;
	struct nouveau_pm_threshold_temp threshold_temp;
	struct nouveau_pm_fan fan;

	struct nouveau_pm_profile *profile_ac;
	struct nouveau_pm_profile *profile_dc;
	struct nouveau_pm_profile *profile;
	struct list_head profiles;

	struct nouveau_pm_level boot;
	struct nouveau_pm_level *cur;

	struct device *hwmon;
	struct notifier_block acpi_nb;

	int  (*clocks_get)(struct nouveau_device *, struct nouveau_pm_level *);
	void *(*clocks_pre)(struct nouveau_device *, struct nouveau_pm_level *);
	int (*clocks_set)(struct nouveau_device *, void *);

	int (*voltage_get)(struct nouveau_device *);
	int (*voltage_set)(struct nouveau_device *, int voltage);
	int (*pwm_get)(struct nouveau_device *, int line, u32*, u32*);
	int (*pwm_set)(struct nouveau_device *, int line, u32, u32);
	int (*temp_get)(struct nouveau_device *);
};

struct nouveau_subsys {
	struct nouveau_instmem_engine instmem;
	struct nouveau_display_engine display;
	struct nouveau_pm_engine      pm;
};

struct nouveau_pll_vals {
	union {
		struct {
#ifdef __BIG_ENDIAN
			u8 N1, M1, N2, M2;
#else
			u8 M1, N1, M2, N2;
#endif
		};
		struct {
			u16 NM1, NM2;
		} __attribute__((packed));
	};
	int log2P;

	int refclk;
};

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

enum nouveau_card_type {
	NV_04      = 0x04,
	NV_10      = 0x10,
	NV_20      = 0x20,
	NV_30      = 0x30,
	NV_40      = 0x40,
	NV_50      = 0x50,
	NV_C0      = 0xc0,
	NV_D0      = 0xd0,
	NV_E0      = 0xe0,
};

struct nouveau_device {
	struct drm_device *dev;
	bool noaccel;

	/* the card type, takes NV_* as values */
	enum nouveau_card_type card_type;
	/* exact chipset, derived from NV_PMC_BOOT_0 */
	int chipset;
	int flags;
	u32 crystal;

	void __iomem *mmio;

	struct nouveau_subdev *subdev[NVDEV_SUBDEV_NR];

	spinlock_t ramin_lock;
	void __iomem *ramin;
	u32 ramin_size;
	u32 ramin_base;
	bool ramin_available;
	struct drm_mm ramin_heap;
	struct nouveau_engine *engine[NVOBJ_ENGINE_NR];
	struct list_head gpuobj_list;
	struct list_head classes;

	struct nouveau_bo *vga_ram;

	/* interrupt handling */
	void (*irq_handler[32])(struct nouveau_device *);
	bool msi_enabled;

	struct list_head vbl_waiting;

	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
		atomic_t validate_sequence;
		int (*move)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_mem_reg *, struct ttm_mem_reg *);
	} ttm;

	struct {
		spinlock_t lock;
		struct drm_mm heap;
		struct nouveau_bo *bo;
	} fence;

	struct {
		spinlock_t lock;
		struct nouveau_channel *ptr[NOUVEAU_MAX_CHANNEL_NR];
	} channels;

	struct nouveau_subsys subsys;
	struct nouveau_channel *channel;

	/* For PFIFO and PGRAPH. */
	spinlock_t context_switch_lock;

	/* VM/PRAMIN flush, legacy PRAMIN aperture */
	spinlock_t vm_lock;

	/* RAMIN configuration, RAMFC, RAMHT and RAMRO offsets */
	struct nouveau_ramht  *ramht;
	struct nouveau_gpuobj *ramfc;
	struct nouveau_gpuobj *ramro;

	u32 ramin_rsvd_vram;

	struct {
		enum {
			NOUVEAU_GART_NONE = 0,
			NOUVEAU_GART_AGP,	/* AGP */
			NOUVEAU_GART_PDMA,	/* paged dma object */
			NOUVEAU_GART_HW		/* on-chip gart/vm */
		} type;
		u64 aper_base;
		u64 aper_size;
		u64 aper_free;

		struct ttm_backend_func *func;

		struct {
			struct page *page;
			dma_addr_t   addr;
		} dummy;

		struct nouveau_gpuobj *sg_ctxdma;
	} gart_info;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_tile_reg reg[NOUVEAU_MAX_TILE_NR];
		spinlock_t lock;
	} tile;

	/* VRAM/fb configuration */
	enum {
		NV_MEM_TYPE_UNKNOWN = 0,
		NV_MEM_TYPE_STOLEN,
		NV_MEM_TYPE_SGRAM,
		NV_MEM_TYPE_SDRAM,
		NV_MEM_TYPE_DDR1,
		NV_MEM_TYPE_DDR2,
		NV_MEM_TYPE_DDR3,
		NV_MEM_TYPE_GDDR2,
		NV_MEM_TYPE_GDDR3,
		NV_MEM_TYPE_GDDR4,
		NV_MEM_TYPE_GDDR5
	} vram_type;
	u64 vram_size;
	u64 vram_sys_base;
	bool vram_rank_B;

	u64 fb_available_size;
	u64 fb_mappable_pages;
	u64 fb_aper_free;
	int fb_mtrr;

	/* BAR control (NV50-) */
	struct nouveau_vm *bar1_vm;
	struct nouveau_vm *bar3_vm;

	/* G8x/G9x virtual address space */
	struct nouveau_vm *chan_vm;

	u8 *mxms;
	struct list_head i2c_ports;

	struct nv04_mode_state mode_reg;
	struct nv04_mode_state saved_reg;
	u32 saved_vga_font[4][16384];
	u32 crtc_owner;
	u32 dac_users[4];

	struct backlight_device *backlight;

	struct {
		struct dentry *channel_root;
	} debugfs;

	struct nouveau_fbdev *nfbdev;
	struct apertures_struct *apertures;
};

static inline struct nouveau_device *
nouveau_device(struct drm_device *dev)
{
	return dev->dev_private;
}

static inline struct nouveau_device *
nouveau_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct nouveau_device, ttm.bdev);
}

static inline void *
nv_subdev(struct nouveau_device *ndev, int subdev)
{
	return (void *)ndev->subdev[subdev];
}

static inline int
nouveau_bo_ref(struct nouveau_bo *ref, struct nouveau_bo **pnvbo)
{
	struct nouveau_bo *prev;

	if (!pnvbo)
		return -EINVAL;
	prev = *pnvbo;

	*pnvbo = ref ? nouveau_bo(ttm_bo_reference(&ref->bo)) : NULL;
	if (prev) {
		struct ttm_buffer_object *bo = &prev->bo;

		ttm_bo_unref(&bo);
	}

	return 0;
}

/* nouveau_drv.c */
extern int nouveau_modeset;
extern int nouveau_agpmode;
extern int nouveau_duallink;
extern int nouveau_uscript_lvds;
extern int nouveau_uscript_tmds;
extern int nouveau_vram_pushbuf;
extern int nouveau_vram_notify;
extern char *nouveau_vram_type;
extern int nouveau_fbpercrtc;
extern int nouveau_tv_disable;
extern char *nouveau_tv_norm;
extern int nouveau_reg_debug;
extern char *nouveau_vbios;
extern int nouveau_ignorelid;
extern int nouveau_nofbaccel;
extern int nouveau_noaccel;
extern int nouveau_force_post;
extern int nouveau_override_conntype;
extern char *nouveau_perflvl;
extern int nouveau_perflvl_wr;
extern int nouveau_msi;
extern int nouveau_ctxfw;
extern int nouveau_mxmdcb;

int nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state);
int nouveau_pci_resume(struct pci_dev *pdev);

/* nouveau_state.c */
int  nouveau_open(struct drm_device *, struct drm_file *);
void nouveau_preclose(struct drm_device *, struct drm_file *);
void nouveau_postclose(struct drm_device *, struct drm_file *);
int  nouveau_load(struct drm_device *, unsigned long flags);
int  nouveau_firstopen(struct drm_device *);
void nouveau_lastclose(struct drm_device *);
int  nouveau_unload(struct drm_device *);
bool nouveau_wait_eq(struct nouveau_device *, u64 timeout,
			    u32 reg, u32 mask, u32 val);
bool nouveau_wait_ne(struct nouveau_device *, u64 timeout,
			    u32 reg, u32 mask, u32 val);
bool nouveau_wait_cb(struct nouveau_device *, u64 timeout,
			    bool (*cond)(void *), void *);
bool nouveau_wait_for_idle(struct nouveau_device *);
int  nouveau_card_init(struct nouveau_device *);

/* nouveau_mem.c */
int  nouveau_mem_vram_init(struct nouveau_device *);
void nouveau_mem_vram_fini(struct nouveau_device *);
int  nouveau_mem_gart_init(struct nouveau_device *);
void nouveau_mem_gart_fini(struct nouveau_device *);
int  nouveau_mem_init_agp(struct nouveau_device *);
int  nouveau_mem_reset_agp(struct nouveau_device *);
void nouveau_mem_close(struct nouveau_device *);
int  nouveau_mem_timing_calc(struct nouveau_device *, u32 freq,
				    struct nouveau_pm_memtiming *);
void nouveau_mem_timing_read(struct nouveau_device *,
				    struct nouveau_pm_memtiming *);
int nouveau_mem_vbios_type(struct nouveau_device *);
struct nouveau_tile_reg *nv10_mem_set_tiling(
	struct nouveau_device *, u32 addr, u32 size,
	u32 pitch, u32 flags);
void nv10_mem_put_tile_region(struct nouveau_device *,
				     struct nouveau_tile_reg *tile,
				     struct nouveau_fence *fence);
extern const struct ttm_mem_type_manager_func nouveau_vram_manager;
extern const struct ttm_mem_type_manager_func nouveau_gart_manager;

/* nouveau_notifier.c */
int  nouveau_notifier_init_channel(struct nouveau_channel *);
void nouveau_notifier_takedown_channel(struct nouveau_channel *);
int  nouveau_notifier_alloc(struct nouveau_channel *, u32 handle,
				   int cout, u32 start, u32 end,
				   u32 *offset);

/* nouveau_channel.c */
void nouveau_channel_cleanup(struct nouveau_device *, struct drm_file *);
int  nouveau_channel_alloc(struct nouveau_device *,
				  struct nouveau_channel **chan,
				  struct drm_file *file_priv,
				  u32 fb_ctxdma, u32 tt_ctxdma);
struct nouveau_channel *
nouveau_channel_get_unlocked(struct nouveau_channel *);
struct nouveau_channel *
nouveau_channel_get(struct drm_file *, int id);
void nouveau_channel_put_unlocked(struct nouveau_channel **);
void nouveau_channel_put(struct nouveau_channel **);
void nouveau_channel_ref(struct nouveau_channel *chan,
				struct nouveau_channel **pchan);
int  nouveau_channel_idle(struct nouveau_channel *chan);

/* nouveau_gpuobj.c */
#define NVOBJ_ENGINE_ADD(ndev, e, p) do {                                      \
	(ndev)->engine[NVOBJ_ENGINE_##e] = (p);                                \
} while (0)

#define NVOBJ_ENGINE_DEL(ndev, e) do {                                         \
	(ndev)->engine[NVOBJ_ENGINE_##e] = NULL;                               \
} while (0)

#define NVOBJ_CLASS(d, c, e) do {                                              \
	int ret = nouveau_gpuobj_class_new((d), (c), NVOBJ_ENGINE_##e);        \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)

#define NVOBJ_MTHD(d, c, m, e) do {                                            \
	int ret = nouveau_gpuobj_mthd_new((d), (c), (m), (e));                 \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)

int  nouveau_gpuobj_early_init(struct nouveau_device *);
int  nouveau_gpuobj_init(struct nouveau_device *);
void nouveau_gpuobj_takedown(struct nouveau_device *);
int  nouveau_gpuobj_suspend(struct nouveau_device *);
void nouveau_gpuobj_resume(struct nouveau_device *);
int  nouveau_gpuobj_class_new(struct nouveau_device *, u32 class, u32 eng);
int  nouveau_gpuobj_mthd_new(struct nouveau_device *, u32 class, u32 mthd,
				    int (*exec)(struct nouveau_channel *,
						u32 class, u32 mthd, u32 data));
int  nouveau_gpuobj_mthd_call(struct nouveau_channel *, u32, u32, u32);
int  nouveau_gpuobj_mthd_call2(struct nouveau_device *, int, u32, u32, u32);
int nouveau_gpuobj_channel_init(struct nouveau_channel *,
				       u32 vram_h, u32 tt_h);
void nouveau_gpuobj_channel_takedown(struct nouveau_channel *);
int nouveau_gpuobj_new(struct nouveau_device *, struct nouveau_channel *,
			      u32 size, int align, u32 flags,
			      struct nouveau_gpuobj **);
void nouveau_gpuobj_ref(struct nouveau_gpuobj *,
			       struct nouveau_gpuobj **);
int nouveau_gpuobj_new_fake(struct nouveau_device *, u32 pinst, u64 vinst,
				   u32 size, u32 flags,
				   struct nouveau_gpuobj **);
int nouveau_gpuobj_dma_new(struct nouveau_channel *, int class,
				  u64 offset, u64 size, int access,
				  int target, struct nouveau_gpuobj **);
int nouveau_gpuobj_gr_new(struct nouveau_channel *, u32 handle, int class);
int nv50_gpuobj_dma_new(struct nouveau_channel *, int class, u64 base,
			       u64 size, int target, int access, u32 type,
			       u32 comp, struct nouveau_gpuobj **pobj);
void nv50_gpuobj_dma_init(struct nouveau_gpuobj *, u32 offset,
				 int class, u64 base, u64 size, int target,
				 int access, u32 type, u32 comp);

/* nouveau_irq.c */
int         nouveau_irq_init(struct nouveau_device *);
void        nouveau_irq_fini(struct nouveau_device *);
irqreturn_t nouveau_irq_handler(DRM_IRQ_ARGS);
void        nouveau_irq_register(struct nouveau_device *, int status_bit,
					void (*)(struct nouveau_device *));
void        nouveau_irq_unregister(struct nouveau_device *, int status_bit);
void        nouveau_irq_preinstall(struct drm_device *);
int         nouveau_irq_postinstall(struct drm_device *);
void        nouveau_irq_uninstall(struct drm_device *);

/* nouveau_sgdma.c */
int nouveau_sgdma_init(struct nouveau_device *);
void nouveau_sgdma_takedown(struct nouveau_device *);
u32 nouveau_sgdma_get_physical(struct nouveau_device *,
					   u32 offset);
struct ttm_tt *nouveau_sgdma_create_ttm(struct ttm_bo_device *bdev,
					       unsigned long size,
					       u32 page_flags,
					       struct page *dummy_read_page);

/* nouveau_debugfs.c */
#if defined(CONFIG_DRM_NOUVEAU_DEBUG)
int  nouveau_debugfs_init(struct drm_minor *);
void nouveau_debugfs_takedown(struct drm_minor *);
int  nouveau_debugfs_channel_init(struct nouveau_channel *);
void nouveau_debugfs_channel_fini(struct nouveau_channel *);
#else
static inline int
nouveau_debugfs_init(struct drm_minor *minor)
{
	return 0;
}

static inline void nouveau_debugfs_takedown(struct drm_minor *minor)
{
}

static inline int
nouveau_debugfs_channel_init(struct nouveau_channel *chan)
{
	return 0;
}

static inline void
nouveau_debugfs_channel_fini(struct nouveau_channel *chan)
{
}
#endif

/* nouveau_dma.c */
void nouveau_dma_init(struct nouveau_channel *);
int  nouveau_dma_wait(struct nouveau_channel *, int slots, int size);

/* nouveau_acpi.c */
#define ROM_BIOS_PAGE 4096
#if defined(CONFIG_ACPI)
void nouveau_register_dsm_handler(void);
void nouveau_unregister_dsm_handler(void);
void nouveau_switcheroo_optimus_dsm(void);
int nouveau_acpi_get_bios_chunk(u8 *bios, int offset, int len);
bool nouveau_acpi_rom_supported(struct pci_dev *pdev);
int nouveau_acpi_edid(struct nouveau_device *, struct drm_connector *);
#else
static inline void nouveau_register_dsm_handler(void) {}
static inline void nouveau_unregister_dsm_handler(void) {}
static inline void nouveau_switcheroo_optimus_dsm(void) {}
static inline bool nouveau_acpi_rom_supported(struct pci_dev *pdev) { return false; }
static inline int nouveau_acpi_get_bios_chunk(u8 *bios, int offset, int len) { return -EINVAL; }
static inline int nouveau_acpi_edid(struct nouveau_device *, struct drm_connector *connector) { return -EINVAL; }
#endif

/* nouveau_backlight.c */
#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
int nouveau_backlight_init(struct nouveau_device *);
void nouveau_backlight_exit(struct nouveau_device *);
#else
static inline int nouveau_backlight_init(struct nouveau_device *)
{
	return 0;
}

static inline void nouveau_backlight_exit(struct nouveau_device *) { }
#endif

/* nouveau_mxm.c */
int  nouveau_mxm_init(struct nouveau_device *);
void nouveau_mxm_fini(struct nouveau_device *);

/* nouveau_ttm.c */
int nouveau_ttm_global_init(struct nouveau_device *);
void nouveau_ttm_global_release(struct nouveau_device *);
int nouveau_ttm_mmap(struct file *, struct vm_area_struct *);

/* nouveau_hdmi.c */
void nouveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

/* nv04_graph.c */
int  nv04_graph_create(struct nouveau_device *);
int  nv04_graph_object_new(struct nouveau_channel *, int, u32, u16);
int  nv04_graph_mthd_page_flip(struct nouveau_channel *chan,
				      u32 class, u32 mthd, u32 data);
extern struct nouveau_bitfield nv04_graph_nsource[];

/* nv10_graph.c */
int  nv10_graph_create(struct nouveau_device *);
struct nouveau_channel *nv10_graph_channel(struct nouveau_device *);
extern struct nouveau_bitfield nv10_graph_intr[];
extern struct nouveau_bitfield nv10_graph_nstatus[];

/* nv20_graph.c */
int  nv20_graph_create(struct nouveau_device *);

/* nv40_graph.c */
int  nv40_graph_create(struct nouveau_device *);
void nv40_grctx_init(struct nouveau_device *, u32 *size);
void nv40_grctx_fill(struct nouveau_device *, struct nouveau_gpuobj *);

/* nv50_graph.c */
int  nv50_graph_create(struct nouveau_device *);
extern struct nouveau_enum nv50_data_error_names[];
int  nv50_graph_isr_chid(struct nouveau_device *, u64 inst);
int  nv50_grctx_init(struct nouveau_device *, u32 *, u32, u32 *, u32 *);
void nv50_grctx_fill(struct nouveau_device *, struct nouveau_gpuobj *);

/* nvc0_graph.c */
int  nvc0_graph_create(struct nouveau_device *);
int  nvc0_graph_isr_chid(struct nouveau_device *, u64 inst);

/* nve0_graph.c */
int  nve0_graph_create(struct nouveau_device *);

/* nv84_crypt.c */
int  nv84_crypt_create(struct nouveau_device *);

/* nv98_crypt.c */
int  nv98_crypt_create(struct nouveau_device *);

/* nva3_copy.c */
int  nva3_copy_create(struct nouveau_device *);

/* nvc0_copy.c */
int  nvc0_copy_create(struct nouveau_device *, int engine);

/* nv31_mpeg.c */
int  nv31_mpeg_create(struct nouveau_device *);

/* nv50_mpeg.c */
int  nv50_mpeg_create(struct nouveau_device *);

/* nv84_bsp.c */
/* nv98_bsp.c */
int  nv84_bsp_create(struct nouveau_device *);

/* nv84_vp.c */
/* nv98_vp.c */
int  nv84_vp_create(struct nouveau_device *);

/* nv98_ppp.c */
int  nv98_ppp_create(struct nouveau_device *);

/* nv04_instmem.c */
int  nv04_instmem_init(struct nouveau_device *);
void nv04_instmem_takedown(struct nouveau_device *);
int  nv04_instmem_suspend(struct nouveau_device *);
void nv04_instmem_resume(struct nouveau_device *);
int  nv04_instmem_get(struct nouveau_gpuobj *, struct nouveau_channel *,
			     u32 size, u32 align);
void nv04_instmem_put(struct nouveau_gpuobj *);
int  nv04_instmem_map(struct nouveau_gpuobj *);
void nv04_instmem_unmap(struct nouveau_gpuobj *);
void nv04_instmem_flush(struct nouveau_device *);

/* nv50_instmem.c */
int  nv50_instmem_init(struct nouveau_device *);
void nv50_instmem_takedown(struct nouveau_device *);
int  nv50_instmem_suspend(struct nouveau_device *);
void nv50_instmem_resume(struct nouveau_device *);
int  nv50_instmem_get(struct nouveau_gpuobj *, struct nouveau_channel *,
			     u32 size, u32 align);
void nv50_instmem_put(struct nouveau_gpuobj *);
int  nv50_instmem_map(struct nouveau_gpuobj *);
void nv50_instmem_unmap(struct nouveau_gpuobj *);
void nv50_instmem_flush(struct nouveau_device *);
void nv84_instmem_flush(struct nouveau_device *);

/* nvc0_instmem.c */
int  nvc0_instmem_init(struct nouveau_device *);
void nvc0_instmem_takedown(struct nouveau_device *);
int  nvc0_instmem_suspend(struct nouveau_device *);
void nvc0_instmem_resume(struct nouveau_device *);

long nouveau_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg);

/* nv04_dac.c */
int nv04_dac_create(struct drm_connector *, struct dcb_entry *);
u32 nv17_dac_sample_load(struct drm_encoder *encoder);
int nv04_dac_output_offset(struct drm_encoder *encoder);
void nv04_dac_update_dacclk(struct drm_encoder *encoder, bool enable);
bool nv04_dac_in_use(struct drm_encoder *encoder);

/* nv04_dfp.c */
int nv04_dfp_create(struct drm_connector *, struct dcb_entry *);
int nv04_dfp_get_bound_head(struct nouveau_device *, struct dcb_entry *dcbent);
void nv04_dfp_bind_head(struct nouveau_device *, struct dcb_entry *dcbent,
			       int head, bool dl);
void nv04_dfp_disable(struct nouveau_device *, int head);
void nv04_dfp_update_fp_control(struct drm_encoder *encoder, int mode);

/* nv04_tv.c */
int nv04_tv_identify(struct nouveau_device *, int i2c_index);
int nv04_tv_create(struct drm_connector *, struct dcb_entry *);

/* nv17_tv.c */
int nv17_tv_create(struct drm_connector *, struct dcb_entry *);

/* nv04_display.c */
int nv04_display_early_init(struct nouveau_device *);
void nv04_display_late_takedown(struct nouveau_device *);
int nv04_display_create(struct nouveau_device *);
void nv04_display_destroy(struct nouveau_device *);
int nv04_display_init(struct nouveau_device *);
void nv04_display_fini(struct nouveau_device *);

/* nvd0_display.c */
int nvd0_display_create(struct nouveau_device *);
void nvd0_display_destroy(struct nouveau_device *);
int nvd0_display_init(struct nouveau_device *);
void nvd0_display_fini(struct nouveau_device *);
struct nouveau_bo *nvd0_display_crtc_sema(struct nouveau_device *, int crtc);
void nvd0_display_flip_stop(struct drm_crtc *);
int nvd0_display_flip_next(struct drm_crtc *, struct drm_framebuffer *,
			   struct nouveau_channel *, u32 swap_interval);

/* nv04_crtc.c */
int nv04_crtc_create(struct nouveau_device *, int index);

/* nouveau_bo.c */
extern struct ttm_bo_driver nouveau_bo_driver;
void nouveau_bo_move_init(struct nouveau_channel *);
int nouveau_bo_new(struct nouveau_device *, int size, int align,
			  u32 flags, u32 tile_mode,
			  u32 tile_flags, struct sg_table *sg,
			  struct nouveau_bo **);
int nouveau_bo_pin(struct nouveau_bo *, u32 flags);
int nouveau_bo_unpin(struct nouveau_bo *);
int nouveau_bo_map(struct nouveau_bo *);
void nouveau_bo_unmap(struct nouveau_bo *);
void nouveau_bo_placement_set(struct nouveau_bo *, u32 type,
				     u32 busy);
u16 nouveau_bo_rd16(struct nouveau_bo *nvbo, unsigned index);
void nouveau_bo_wr16(struct nouveau_bo *nvbo, unsigned index, u16 val);
u32 nouveau_bo_rd32(struct nouveau_bo *nvbo, unsigned index);
void nouveau_bo_wr32(struct nouveau_bo *nvbo, unsigned index, u32 val);
void nouveau_bo_fence(struct nouveau_bo *, struct nouveau_fence *);
int nouveau_bo_validate(struct nouveau_bo *, bool interruptible,
			       bool no_wait_reserve, bool no_wait_gpu);

struct nouveau_vma *
nouveau_bo_vma_find(struct nouveau_bo *, struct nouveau_vm *);
int  nouveau_bo_vma_add(struct nouveau_bo *, struct nouveau_vm *,
			       struct nouveau_vma *);
void nouveau_bo_vma_del(struct nouveau_bo *, struct nouveau_vma *);

/* nouveau_gem.c */
int nouveau_gem_new(struct nouveau_device *, int size, int align,
			   u32 domain, u32 tile_mode,
			   u32 tile_flags, struct nouveau_bo **);
int nouveau_gem_object_new(struct drm_gem_object *);
void nouveau_gem_object_del(struct drm_gem_object *);
int nouveau_gem_object_open(struct drm_gem_object *, struct drm_file *);
void nouveau_gem_object_close(struct drm_gem_object *,
				     struct drm_file *);
int nouveau_gem_ioctl_new(struct drm_device *, void *,
				 struct drm_file *);
int nouveau_gem_ioctl_pushbuf(struct drm_device *, void *,
				     struct drm_file *);
int nouveau_gem_ioctl_cpu_prep(struct drm_device *, void *,
				      struct drm_file *);
int nouveau_gem_ioctl_cpu_fini(struct drm_device *, void *,
				      struct drm_file *);
int nouveau_gem_ioctl_info(struct drm_device *, void *,
				  struct drm_file *);

extern struct dma_buf *nouveau_gem_prime_export(struct drm_device *dev,
				struct drm_gem_object *obj, int flags);
extern struct drm_gem_object *nouveau_gem_prime_import(struct drm_device *dev,
				struct dma_buf *dma_buf);

/* nouveau_display.c */
int nouveau_display_create(struct nouveau_device *);
void nouveau_display_destroy(struct nouveau_device *);
int nouveau_display_init(struct nouveau_device *);
void nouveau_display_fini(struct nouveau_device *);
int nouveau_vblank_enable(struct drm_device *, int crtc);
void nouveau_vblank_disable(struct drm_device *, int crtc);
int nouveau_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   struct drm_pending_vblank_event *event);
int nouveau_finish_page_flip(struct nouveau_channel *,
			     struct nouveau_page_flip_state *);
int nouveau_display_dumb_create(struct drm_file *, struct drm_device *,
				struct drm_mode_create_dumb *args);
int nouveau_display_dumb_map_offset(struct drm_file *, struct drm_device *,
				    u32 handle, u64 *offset);
int nouveau_display_dumb_destroy(struct drm_file *, struct drm_device *,
				 u32 handle);

/* nv50_calc.c */
int nv50_calc_pll(struct nouveau_device *, struct pll_lims *, int clk,
		  int *N1, int *M1, int *N2, int *M2, int *P);
int nva3_calc_pll(struct nouveau_device *, struct pll_lims *,
		  int clk, int *N, int *fN, int *M, int *P);

#ifndef ioread32_native
#ifdef __BIG_ENDIAN
#define ioread16_native ioread16be
#define iowrite16_native iowrite16be
#define ioread32_native  ioread32be
#define iowrite32_native iowrite32be
#else /* def __BIG_ENDIAN */
#define ioread16_native ioread16
#define iowrite16_native iowrite16
#define ioread32_native  ioread32
#define iowrite32_native iowrite32
#endif /* def __BIG_ENDIAN else */
#endif /* !ioread32_native */

/* channel control reg access */
static inline u32 nvchan_rd32(struct nouveau_channel *chan, unsigned reg)
{
	return ioread32_native(chan->user + reg);
}

static inline void nvchan_wr32(struct nouveau_channel *chan,
							unsigned reg, u32 val)
{
	iowrite32_native(val, chan->user + reg);
}

/* register access */
static inline u32 nv_rd32(struct nouveau_device *ndev, unsigned reg)
{
	return ioread32_native(ndev->mmio + reg);
}

static inline void nv_wr32(struct nouveau_device *ndev, unsigned reg, u32 val)
{
	iowrite32_native(val, ndev->mmio + reg);
}

static inline u32 nv_mask(struct nouveau_device *ndev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = nv_rd32(ndev, reg);
	nv_wr32(ndev, reg, (tmp & ~mask) | val);
	return tmp;
}

static inline u8 nv_rd08(struct nouveau_device *ndev, unsigned reg)
{
	return ioread8(ndev->mmio + reg);
}

static inline void nv_wr08(struct nouveau_device *ndev, unsigned reg, u8 val)
{
	iowrite8(val, ndev->mmio + reg);
}

#define nv_wait(dev, reg, mask, val) \
	nouveau_wait_eq(dev, 2000000000ULL, (reg), (mask), (val))
#define nv_wait_ne(dev, reg, mask, val) \
	nouveau_wait_ne(dev, 2000000000ULL, (reg), (mask), (val))
#define nv_wait_cb(dev, func, data) \
	nouveau_wait_cb(dev, 2000000000ULL, (func), (data))

/* PRAMIN access */
static inline u32 nv_ri32(struct nouveau_device *ndev, unsigned offset)
{
	return ioread32_native(ndev->ramin + offset);
}

static inline void nv_wi32(struct nouveau_device *ndev, unsigned offset, u32 val)
{
	iowrite32_native(val, ndev->ramin + offset);
}

/* object access */
u32 nv_ro32(struct nouveau_gpuobj *, u32 offset);
void nv_wo32(struct nouveau_gpuobj *, u32 offset, u32 val);

/*
 * Logging
 * Argument d is (struct nouveau_device *).
 */
#define NV_PRINTK(level, d, fmt, arg...) \
	printk(level "[" DRM_NAME "] " DRIVER_NAME " %s: " fmt, \
					pci_name(d->dev->pdev), ##arg)
#ifndef NV_DEBUG_NOTRACE
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER) {                                       \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS) {                                          \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#else
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER)                                         \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS)                                            \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#endif
#define NV_ERROR(d, fmt, arg...) NV_PRINTK(KERN_ERR, d, fmt, ##arg)
#define NV_INFO(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_TRACEWARN(d, fmt, arg...) NV_PRINTK(KERN_NOTICE, d, fmt, ##arg)
#define NV_TRACE(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_WARN(d, fmt, arg...) NV_PRINTK(KERN_WARNING, d, fmt, ##arg)
#define NV_WARNONCE(d, fmt, arg...) do {                                       \
	static int _warned = 0;                                                \
	if (!_warned) {                                                        \
		NV_WARN(d, fmt, ##arg);                                        \
		_warned = 1;                                                   \
	}                                                                      \
} while(0)

/* nouveau_reg_debug bitmask */
enum {
	NOUVEAU_REG_DEBUG_MC             = 0x1,
	NOUVEAU_REG_DEBUG_VIDEO          = 0x2,
	NOUVEAU_REG_DEBUG_FB             = 0x4,
	NOUVEAU_REG_DEBUG_EXTDEV         = 0x8,
	NOUVEAU_REG_DEBUG_CRTC           = 0x10,
	NOUVEAU_REG_DEBUG_RAMDAC         = 0x20,
	NOUVEAU_REG_DEBUG_VGACRTC        = 0x40,
	NOUVEAU_REG_DEBUG_RMVIO          = 0x80,
	NOUVEAU_REG_DEBUG_VGAATTR        = 0x100,
	NOUVEAU_REG_DEBUG_EVO            = 0x200,
	NOUVEAU_REG_DEBUG_AUXCH          = 0x400
};

#define NV_REG_DEBUG(type, dev, fmt, arg...) do { \
	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_##type) \
		NV_PRINTK(KERN_DEBUG, dev, "%s: " fmt, __func__, ##arg); \
} while (0)

static inline bool
nv_two_heads(struct nouveau_device *ndev)
{
	const int impl = ndev->dev->pci_device & 0x0ff0;

	if (ndev->card_type >= NV_10 && impl != 0x0100 &&
	    impl != 0x0150 && impl != 0x01a0 && impl != 0x0200)
		return true;

	return false;
}

static inline bool
nv_gf4_disp_arch(struct nouveau_device *ndev)
{
	return nv_two_heads(ndev) && (ndev->dev->pci_device & 0x0ff0) != 0x0110;
}

static inline bool
nv_two_reg_pll(struct nouveau_device *ndev)
{
	const int impl = ndev->dev->pci_device & 0x0ff0;

	if (impl == 0x0310 || impl == 0x0340 || ndev->card_type >= NV_40)
		return true;
	return false;
}

static inline bool
nv_match_device(struct nouveau_device *ndev, unsigned device,
		unsigned sub_vendor, unsigned sub_device)
{
	return ndev->dev->pdev->device == device &&
	       ndev->dev->pdev->subsystem_vendor == sub_vendor &&
	       ndev->dev->pdev->subsystem_device == sub_device;
}

static inline void *
nv_engine(struct nouveau_device *ndev, int engine)
{
	return (void *)ndev->engine[engine];
}

/* returns 1 if device is one of the nv4x using the 0x4497 object class,
 * helpful to determine a number of other hardware features
 */
static inline int
nv44_graph_class(struct nouveau_device *ndev)
{
	if ((ndev->chipset & 0xf0) == 0x60)
		return 1;

	return !(0x0baf & (1 << (ndev->chipset & 0x0f)));
}

/* memory type/access flags, do not match hardware values */
#define NV_MEM_ACCESS_RO  1
#define NV_MEM_ACCESS_WO  2
#define NV_MEM_ACCESS_RW (NV_MEM_ACCESS_RO | NV_MEM_ACCESS_WO)
#define NV_MEM_ACCESS_SYS 4
#define NV_MEM_ACCESS_VM  8
#define NV_MEM_ACCESS_NOSNOOP 16

#define NV_MEM_TARGET_VRAM        0
#define NV_MEM_TARGET_PCI         1
#define NV_MEM_TARGET_PCI_NOSNOOP 2
#define NV_MEM_TARGET_VM          3
#define NV_MEM_TARGET_GART        4

#define NV_MEM_TYPE_VM 0x7f
#define NV_MEM_COMP_VM 0x03

/* FIFO methods */
#define NV01_SUBCHAN_OBJECT                                          0x00000000
#define NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH                          0x00000010
#define NV84_SUBCHAN_SEMAPHORE_ADDRESS_LOW                           0x00000014
#define NV84_SUBCHAN_SEMAPHORE_SEQUENCE                              0x00000018
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER                               0x0000001c
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_EQUAL                 0x00000001
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG                    0x00000002
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL                0x00000004
#define NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD                         0x00001000
#define NV84_SUBCHAN_NOTIFY_INTR                                     0x00000020
#define NV84_SUBCHAN_WRCACHE_FLUSH                                   0x00000024
#define NV10_SUBCHAN_REF_CNT                                         0x00000050
#define NVSW_SUBCHAN_PAGE_FLIP                                       0x00000054
#define NV11_SUBCHAN_DMA_SEMAPHORE                                   0x00000060
#define NV11_SUBCHAN_SEMAPHORE_OFFSET                                0x00000064
#define NV11_SUBCHAN_SEMAPHORE_ACQUIRE                               0x00000068
#define NV11_SUBCHAN_SEMAPHORE_RELEASE                               0x0000006c
#define NV40_SUBCHAN_YIELD                                           0x00000080

/* NV_SW object class */
#define NV_SW                                                        0x0000506e
#define NV_SW_DMA_VBLSEM                                             0x0000018c
#define NV_SW_VBLSEM_OFFSET                                          0x00000400
#define NV_SW_VBLSEM_RELEASE_VALUE                                   0x00000404
#define NV_SW_VBLSEM_RELEASE                                         0x00000408
#define NV_SW_PAGE_FLIP                                              0x00000500

#endif /* __NOUVEAU_DRV_H__ */
