#ifndef __NOUVEAU_VP_H__
#define __NOUVEAU_VP_H__

struct nouveau_vp_priv {
	struct nouveau_engine base;
};

int nv84_vp_create(struct nouveau_device *, int engine);

#endif
