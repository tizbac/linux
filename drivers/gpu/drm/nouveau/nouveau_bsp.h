#ifndef __NOUVEAU_BSP_H__
#define __NOUVEAU_BSP_H__

struct nouveau_bsp_priv {
	struct nouveau_engine base;
};

int nv84_bsp_create(struct nouveau_device *, int engine);

#endif
