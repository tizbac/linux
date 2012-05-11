#ifndef __NOUVEAU_FANCTL_H__
#define __NOUVEAU_FANCTL_H__

struct nouveau_fanctl {
	struct nouveau_subdev base;
	int (*get)(struct nouveau_fanctl *);
	int (*set)(struct nouveau_fanctl *, int percent);
	int (*sense)(struct nouveau_fanctl *);
};

int  nouveau_fanctl_create(struct nouveau_device *);

int  nv40_fanpwm_create(struct nouveau_device *, int subdev);
int  nv50_fanpwm_create(struct nouveau_device *, int subdev);

int  nv40_fanpwm_sense(struct nouveau_fanctl *);

#endif
