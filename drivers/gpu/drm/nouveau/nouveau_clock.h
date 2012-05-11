#ifndef __NOUVEAU_CLOCK_H__
#define __NOUVEAU_CLOCK_H__

struct nouveau_clock {
	struct nouveau_subdev base;
	int   (*perf_get)(struct nouveau_clock *, struct nouveau_pm_level *);
	void *(*perf_pre)(struct nouveau_clock *, struct nouveau_pm_level *);
	int   (*perf_set)(struct nouveau_clock *, void *);
};

int nv04_clock_create(struct nouveau_device *, int subdev);
int nv40_clock_create(struct nouveau_device *, int subdev);
int nv50_clock_create(struct nouveau_device *, int subdev);
int nva3_clock_create(struct nouveau_device *, int subdev);
int nvc0_clock_create(struct nouveau_device *, int subdev);

#endif
