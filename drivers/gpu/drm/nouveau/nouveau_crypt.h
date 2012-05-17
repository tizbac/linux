#ifndef __NOUVEAU_CRYPT_H__
#define __NOUVEAU_CRYPT_H__

struct nouveau_crypt_priv {
	struct nouveau_engine base;
};

int nv84_crypt_create(struct nouveau_device *, int engine);
int nv98_crypt_create(struct nouveau_device *, int engine);

#endif
