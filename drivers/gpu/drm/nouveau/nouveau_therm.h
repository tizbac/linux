#ifndef __NOUVEAU_THERM_H__
#define __NOUVEAU_THERM_H__

enum nouveau_pm_fan_type {
	FAN_NONE = 0,
	FAN_TOGGLE_OR_PWM,
	FAN_I2C
};

struct nouveau_therm_fan {
	/* physical connection */
	enum nouveau_pm_fan_type type;
	struct i2c_client *i2c_fan;

	/* pwm settings */
	u32 pwm_freq;
	u32 min_duty;
	u32 max_duty;

	u16 bump_period;
	u16 slow_down_period;
};

struct nouveau_therm_threshold_temp {
	s16 critical;
	s16 down_clock;
	s16 fan_boost;
};

struct nouveau_therm {
	struct nouveau_subdev base;
	struct nouveau_therm_threshold_temp threshold_temp;
	struct nouveau_therm_fan fan;
	int (*temp_get)(struct nouveau_therm *);
	int (*calc_fan_duty)(struct nouveau_therm *);
};

int  nouveau_therm_create(struct nouveau_device *, int subdev);
void nouveau_therm_safety_checks(struct nouveau_therm *);
#endif
