#ifndef __NOUVEAU_THERM_H__
#define __NOUVEAU_THERM_H__

struct nouveau_therm_sensor_constants {
	u16 offset_constant;
	s16 offset_mult;
	s16 offset_div;
	s16 slope_mult;
	s16 slope_div;
};

struct nouveau_therm_threshold_temp {
	s16 critical;
	s16 down_clock;
	s16 fan_boost;
};

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
};

struct nouveau_therm {
	struct nouveau_subdev base;
	struct nouveau_therm_sensor_constants sensor_constants;
	struct nouveau_therm_threshold_temp threshold_temp;
	struct nouveau_therm_fan fan;
	int (*temp_get)(struct nouveau_therm *);
};

int  nouveau_therm_create(struct nouveau_device *, int subdev);
void nouveau_therm_safety_checks(struct nouveau_therm *);

#endif
