#ifndef __NOUVEAU_COPY_H__
#define __NOUVEAU_COPY_H__

struct nouveau_copy_priv {
	struct nouveau_engine base;
};

int nva3_copy_create(struct nouveau_device *, int engine);
int nvc0_copy_create(struct nouveau_device *, int engine);

#endif
