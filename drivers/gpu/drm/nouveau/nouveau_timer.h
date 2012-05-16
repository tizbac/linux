#ifndef __NOUVEAU_TIMER_H__
#define __NOUVEAU_TIMER_H__

struct nouveau_alarm {
	struct list_head head;
	u64 timestamp;
	void (*func)(struct nouveau_alarm *);
};

struct nouveau_timer {
	struct nouveau_subdev base;
	u64  (*read)(struct nouveau_timer *);
	void (*alarm)(struct nouveau_timer *, u32 time, struct nouveau_alarm *);
};

int nv04_timer_create(struct nouveau_device *, int subdev);

static inline void
nouveau_timer_alarm(struct nouveau_device *ndev, u32 nsec,
		    struct nouveau_alarm *alarm)
{
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	ptimer->alarm(ptimer, nsec, alarm);
}

#endif
