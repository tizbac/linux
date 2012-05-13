#ifndef __NOUVEAU_INSTMEM_H__
#define __NOUVEAU_INSTMEM_H__

struct nouveau_instmem {
	struct nouveau_subdev base;

	int  (*get)(struct nouveau_instmem *, struct nouveau_gpuobj *,
		    struct nouveau_vm *, u32 size, u32 align);
	void (*put)(struct nouveau_instmem *, struct nouveau_gpuobj *);
	int  (*map)(struct nouveau_instmem *, struct nouveau_gpuobj *);
	void (*unmap)(struct nouveau_instmem *, struct nouveau_gpuobj *);
	void (*flush)(struct nouveau_instmem *);
};

static inline void
nouveau_instmem_flush(struct nouveau_device *ndev)
{
	struct nouveau_instmem *pimem = nv_subdev(ndev, NVDEV_SUBDEV_INSTMEM);
	pimem->flush(pimem);
}

int nv04_instmem_create(struct nouveau_device *, int subdev);
int nv50_instmem_create(struct nouveau_device *, int subdev);
int nvc0_instmem_create(struct nouveau_device *, int subdev);

void nv50_instmem_destroy(struct nouveau_device *, int subdev);
int  nv50_instmem_fini(struct nouveau_device *, int subdev, bool suspend);
int  nv50_instmem_get(struct nouveau_instmem *, struct nouveau_gpuobj *,
		      struct nouveau_vm *, u32, u32);
void nv50_instmem_put(struct nouveau_instmem *, struct nouveau_gpuobj *);
void nv84_instmem_flush(struct nouveau_instmem *);

#endif
