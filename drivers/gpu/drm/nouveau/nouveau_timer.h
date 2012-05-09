#ifndef __NOUVEAU_TIMER_H__
#define __NOUVEAU_TIMER_H__

struct nouveau_timer {
	struct nouveau_subdev base;
	u64 (*read)(struct nouveau_timer *);
};

int nv04_timer_create(struct nouveau_device *, int subdev);

#endif
