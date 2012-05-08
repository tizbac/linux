#ifndef __NOUVEAU_ABI16_H__
#define __NOUVEAU_ABI16_H__

#define ABI16_IOCTL_ARGS                                                       \
	struct drm_device *dev, void *data, struct drm_file *file_priv
int nouveau_abi16_ioctl_getparam(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_setparam(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_channel_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_channel_free(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_grobj_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_notifierobj_alloc(ABI16_IOCTL_ARGS);
int nouveau_abi16_ioctl_gpuobj_free(ABI16_IOCTL_ARGS);

struct drm_nouveau_channel_alloc {
	u32 fb_ctxdma_handle;
	u32 tt_ctxdma_handle;

	int channel;
	u32 pushbuf_domains;

	/* Notifier memory */
	u32 notifier_handle;

	/* DRM-enforced subchannel assignments */
	struct {
		u32 handle;
		u32 grclass;
	} subchan[8];
	u32 nr_subchan;
};

struct drm_nouveau_channel_free {
	int channel;
};

struct drm_nouveau_grobj_alloc {
	int channel;
	u32 handle;
	int class;
};

struct drm_nouveau_notifierobj_alloc {
	u32 channel;
	u32 handle;
	u32 size;
	u32 offset;
};

struct drm_nouveau_gpuobj_free {
	int channel;
	u32 handle;
};

#define NOUVEAU_GETPARAM_PCI_VENDOR      3
#define NOUVEAU_GETPARAM_PCI_DEVICE      4
#define NOUVEAU_GETPARAM_BUS_TYPE        5
#define NOUVEAU_GETPARAM_FB_SIZE         8
#define NOUVEAU_GETPARAM_AGP_SIZE        9
#define NOUVEAU_GETPARAM_CHIPSET_ID      11
#define NOUVEAU_GETPARAM_VM_VRAM_BASE    12
#define NOUVEAU_GETPARAM_GRAPH_UNITS     13
#define NOUVEAU_GETPARAM_PTIMER_TIME     14
#define NOUVEAU_GETPARAM_HAS_BO_USAGE    15
#define NOUVEAU_GETPARAM_HAS_PAGEFLIP    16
struct drm_nouveau_getparam {
	u64 param;
	u64 value;
};

struct drm_nouveau_setparam {
	u64 param;
	u64 value;
};

#define DRM_IOCTL_NOUVEAU_GETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_GETPARAM, struct drm_nouveau_getparam)
#define DRM_IOCTL_NOUVEAU_SETPARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_SETPARAM, struct drm_nouveau_setparam)
#define DRM_IOCTL_NOUVEAU_CHANNEL_ALLOC      DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_ALLOC, struct drm_nouveau_channel_alloc)
#define DRM_IOCTL_NOUVEAU_CHANNEL_FREE       DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_CHANNEL_FREE, struct drm_nouveau_channel_free)
#define DRM_IOCTL_NOUVEAU_GROBJ_ALLOC        DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_GROBJ_ALLOC, struct drm_nouveau_grobj_alloc)
#define DRM_IOCTL_NOUVEAU_NOTIFIEROBJ_ALLOC  DRM_IOWR(DRM_COMMAND_BASE + DRM_NOUVEAU_NOTIFIEROBJ_ALLOC, struct drm_nouveau_notifierobj_alloc)
#define DRM_IOCTL_NOUVEAU_GPUOBJ_FREE        DRM_IOW (DRM_COMMAND_BASE + DRM_NOUVEAU_GPUOBJ_FREE, struct drm_nouveau_gpuobj_free)

#endif
