#ifndef __NOUVEAU_MPEG_H__
#define __NOUVEAU_MPEG_H__

struct nouveau_mpeg_priv {
	struct nouveau_engine base;
};


int nv31_mpeg_create(struct nouveau_device *, int engine);
int nv50_mpeg_create(struct nouveau_device *, int engine);

#endif
