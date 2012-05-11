#ifndef __NOUVEAU_VOLT_H__
#define __NOUVEAU_VOLT_H__

struct nouveau_voltage {
	u32 voltage;
	u8  vid;
};

struct nouveau_volt {
	struct nouveau_subdev base;
	u8 version;

	int (*iduv)(struct nouveau_volt *, int id);
	int (*uvid)(struct nouveau_volt *, int uv);
	int (*get)(struct nouveau_volt *);
	int (*set)(struct nouveau_volt *, int uv);

	u32 vid_mask;
	int nr_level;
	struct nouveau_voltage level[];
};

int nouveau_volt_create(struct nouveau_device *, int subdev);

#endif
