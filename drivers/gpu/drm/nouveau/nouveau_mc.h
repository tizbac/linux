#ifndef __NOUVEAU_MC_H__
#define __NOUVEAU_MC_H__

struct nouveau_mc {
	struct nouveau_subdev base;
};

int nv04_mc_create(struct nouveau_device *, int subdev);
int nv40_mc_create(struct nouveau_device *, int subdev);
int nv50_mc_create(struct nouveau_device *, int subdev);

#endif
