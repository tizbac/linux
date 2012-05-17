#ifndef __NOUVEAU_PPP_H__
#define __NOUVEAU_PPP_H__

struct nouveau_ppp_priv {
	struct nouveau_engine base;
};

int nv98_ppp_create(struct nouveau_device *, int engine);

#endif
