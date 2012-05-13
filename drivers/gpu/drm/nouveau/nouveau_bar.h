#ifndef __NOUVEAU_BAR_H__
#define __NOUVEAU_BAR_H__

struct nouveau_bar {
	struct nouveau_subdev base;
	int  (*map)(struct nouveau_bar *, struct nouveau_mem *);
	void (*unmap)(struct nouveau_bar *, struct nouveau_mem *);
};

int nv50_bar_create(struct nouveau_device *, int subdev);
int nvc0_bar_create(struct nouveau_device *, int subdev);

#endif
