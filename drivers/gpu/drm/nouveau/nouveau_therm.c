/*
 * Copyright 2010 PathScale inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Martin Peres
 *          Ben Skeggs
 */

#include <linux/module.h>

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"
#include "nouveau_therm.h"
#include "nouveau_timer.h"
#include "nouveau_fanctl.h"

#define MIN(a,b)                   (((a)<(b))?(a):(b))
#define MAX(a,b)                   (((a)>(b))?(a):(b))

/* no vbios have more than 6 */
#define NOUVEAU_TEMP_FAN_TRIP_MAX 10

struct nouveau_therm_sensor_constants {
	u16 offset_constant;
	s16 offset_mult;
	s16 offset_div;
	s16 slope_mult;
	s16 slope_div;
};

struct nouveau_therm_trip_point {
	int fan_duty;
	int temp;
	int hysteresis;
};

struct nouveau_therm_priv {
	struct nouveau_therm base;
	struct nouveau_therm_sensor_constants sensor_constants;

	/* fan */
	struct nouveau_alarm fan_alarm;
	struct nouveau_therm_trip_point *last_trip;
	struct nouveau_therm_trip_point fan_trip[NOUVEAU_TEMP_FAN_TRIP_MAX];
	size_t nr_fan_trip;
};

static int
nv40_sensor_setup(struct nouveau_therm *ptherm)
{
	struct nouveau_therm_priv *priv = (struct nouveau_therm_priv *) ptherm;
	struct nouveau_device *ndev = ptherm->base.device;
	struct nouveau_therm_sensor_constants *sensor =
		&priv->sensor_constants;
	s32 offset, sensor_calibration;

	offset = sensor->offset_mult / sensor->offset_div;

	/* set up the sensors */
	sensor_calibration = 120 - offset - sensor->offset_constant;
	sensor_calibration = sensor_calibration * sensor->slope_div /
				sensor->slope_mult;

	if (ndev->chipset >= 0x46)
		sensor_calibration |= 0x80000000;
	else
		sensor_calibration |= 0x10000000;

	nv_wr32(ndev, 0x0015b0, sensor_calibration);

	/* Wait for the sensor to update */
	msleep(5);

	/* read */
	return nv_rd32(ndev, 0x0015b4) & 0x1fff;
}

static int
nv40_therm_temp_get(struct nouveau_therm *ptherm)
{
	struct nouveau_therm_priv *priv = (struct nouveau_therm_priv *) ptherm;
	struct nouveau_device *ndev = ptherm->base.device;
	struct nouveau_therm_sensor_constants *sensor =
		&priv->sensor_constants;
	int offset, core_temp;

	offset = sensor->offset_mult / sensor->offset_div;

	if (ndev->card_type >= NV_50) {
		core_temp = nv_rd32(ndev, 0x020008);
	} else {
		core_temp = nv_rd32(ndev, 0x0015b4) & 0x1fff;
		/* Setup the sensor if the temperature is 0 */
		if (core_temp == 0)
			core_temp = nv40_sensor_setup(ptherm);
	}

	core_temp = core_temp * sensor->slope_mult / sensor->slope_div;
	core_temp = core_temp + offset + sensor->offset_constant;

	return core_temp;
}

static int
nv84_therm_temp_get(struct nouveau_therm *ptherm)
{
	return nv_rd32(ptherm->base.device, 0x020400);
}

static int
nouveau_therm_calc_target_fan_duty(struct nouveau_therm *ptherm)
{
	struct nouveau_therm_priv *priv = (struct nouveau_therm_priv *) ptherm;
	struct nouveau_therm_threshold_temp *temps = &ptherm->threshold_temp;
	struct nouveau_device *ndev = ptherm->base.device;
	struct nouveau_therm_trip_point *cur_trip = NULL;
	int i, headerlen, recordlen, entries;
	int duty = -ENODEV, linear_min_temp = 0, linear_max_temp = 0;
	int cur_temp;
	struct bit_entry P;
	u8 *temp = NULL;

	uint8_t duty_lut[] =
		{ 0, 0, 25, 0, 40, 0, 50, 0, 75, 0, 85, 0, 100, 0, 100, 0 };

	if (bit_table(ndev, 'P', &P) == 0) {
		if (P.version == 1)
			temp = ROMPTR(ndev, P.data[12]);
		else
		if (P.version == 2)
			temp = ROMPTR(ndev, P.data[16]);
		else
			NV_WARN(ndev, "unknown thermal for BIT P %d\n",
				P.version);
	}

	if (!temp) {
		NV_DEBUG(ndev, "thermal table pointer invalid\n");
		return -ENODEV;
	}

	cur_temp = ptherm->temp_get(ptherm);

	linear_min_temp = 40;
	linear_max_temp = 85;

	headerlen = temp[1];
	recordlen = temp[2];
	entries = temp[3];
	temp = temp + headerlen;

	/* Read the entries from the table */
	priv->nr_fan_trip = 0;
	for (i = 0; i < entries; i++) {
		s16 value = ROM16(temp[1]);

		switch (temp[0]) {
		case 0x21:
			ptherm->fan.type = FAN_TOGGLE_OR_PWM;
			duty = 0;
		case 0x26:
			ptherm->fan.pwm_freq = value;
			break;
		case 0x22:
			ptherm->fan.min_duty = temp[1];
			ptherm->fan.max_duty = temp[2];
			break;
		case 0x24:
			priv->nr_fan_trip++;
			cur_trip = &priv->fan_trip[priv->nr_fan_trip - 1];
			cur_trip->hysteresis = value & 0xf;
			cur_trip->temp = (value & 0xff0) >> 4;
			cur_trip->fan_duty = duty_lut[(value & 0xf000) >> 12];
			break;
		case 0x25:
			cur_trip = &priv->fan_trip[priv->nr_fan_trip - 1];
			cur_trip->fan_duty = value;
			break;
		case 0x3b:
			ptherm->fan.bump_period = value;
			break;
		case 0x3c:
			ptherm->fan.slow_down_period = value;
			break;
		case 0x46:
			linear_min_temp = temp[1];
			linear_max_temp = temp[2];
			break;
		}
		temp += recordlen;
	}

	/* if no 0x21 entry has been found, then abort */
	if (duty < 0)
		return duty;

	/* look for the trip point corresponding to the current temperature */
	cur_trip = NULL;
	for (i = 0; i < priv->nr_fan_trip; i++) {
		if (cur_temp >= priv->fan_trip[i].temp)
			cur_trip = &priv->fan_trip[i];
	}

	/* either use trip-points or linear fan management */
	if (priv->nr_fan_trip) {
		/* account for the hysteresis cycle */
		if (priv->last_trip &&
			cur_temp <= (priv->last_trip->temp) &&
			cur_temp > (priv->last_trip->temp - priv->last_trip->hysteresis))
			cur_trip = priv->last_trip;

		if (cur_trip) {
			duty = cur_trip->fan_duty;
			priv->last_trip = cur_trip;
		} else {
			duty = 0;
			priv->last_trip = NULL;
		}
	} else {
		duty  = (cur_temp - linear_min_temp);
		duty *= (ptherm->fan.max_duty - ptherm->fan.min_duty);
		duty /= (linear_max_temp - linear_min_temp);
		duty += ptherm->fan.min_duty;
	}

	/* check the fan min/max settings */
	if (ptherm->fan.min_duty < 10)
		ptherm->fan.min_duty = 10;
	if (ptherm->fan.max_duty > 100)
		ptherm->fan.max_duty = 100;
	if (ptherm->fan.max_duty < ptherm->fan.min_duty)
		ptherm->fan.max_duty = ptherm->fan.min_duty;

	/* bound duty between min_duty and max_duty */
	if (duty > ptherm->fan.max_duty)
		duty = ptherm->fan.max_duty;
	if (duty < ptherm->fan.min_duty)
		duty = ptherm->fan.min_duty;

	/* check if temperature is over some thresholds */
	if (temps->fan_boost && cur_temp >= temps->fan_boost)
		duty = 100;
	else if (temps->down_clock && cur_temp >= temps->down_clock)
		duty = 100;
	else if (temps->critical && cur_temp >= temps->critical)
		duty = 100;

	return duty;
}

void
nouveau_therm_safety_checks(struct nouveau_therm *ptherm)
{
	struct nouveau_therm_threshold_temp *temps = &ptherm->threshold_temp;

	if (temps->critical > 120)
		temps->critical = 120;
	else if (temps->critical < 80)
		temps->critical = 80;

	if (temps->down_clock > 110)
		temps->down_clock = 110;
	else if (temps->down_clock < 60)
		temps->down_clock = 60;

	if (temps->fan_boost > 100)
		temps->fan_boost = 100;
	else if (temps->fan_boost < 40)
		temps->fan_boost = 40;
}

static void
nouveau_therm_diode_setup(struct nouveau_therm *ptherm, u8 *temp)
{
	struct nouveau_therm_priv *priv = (struct nouveau_therm_priv *) ptherm;
	struct nouveau_therm_sensor_constants *sensor = &priv->sensor_constants;
	struct nouveau_therm_threshold_temp *temps = &ptherm->threshold_temp;
	struct nouveau_device *ndev = ptherm->base.device;
	int i, headerlen, recordlen, entries;

	if (!temp) {
		NV_DEBUG(ndev, "temperature table pointer invalid\n");
		return;
	}

	/* Set the default sensor's contants */
	sensor->offset_constant = 0;
	sensor->offset_mult = 0;
	sensor->offset_div = 1;
	sensor->slope_mult = 1;
	sensor->slope_div = 1;

	/* Set the default temperature thresholds */
	temps->critical = 110;
	temps->down_clock = 100;
	temps->fan_boost = 90;

	/* Set the known default values to setup the temperature sensor */
	if (ndev->card_type >= NV_40) {
		switch (ndev->chipset) {
		case 0x43:
			sensor->offset_mult = 32060;
			sensor->offset_div = 1000;
			sensor->slope_mult = 792;
			sensor->slope_div = 1000;
			break;

		case 0x44:
		case 0x47:
		case 0x4a:
			sensor->offset_mult = 27839;
			sensor->offset_div = 1000;
			sensor->slope_mult = 780;
			sensor->slope_div = 1000;
			break;

		case 0x46:
			sensor->offset_mult = -24775;
			sensor->offset_div = 100;
			sensor->slope_mult = 467;
			sensor->slope_div = 10000;
			break;

		case 0x49:
			sensor->offset_mult = -25051;
			sensor->offset_div = 100;
			sensor->slope_mult = 458;
			sensor->slope_div = 10000;
			break;

		case 0x4b:
			sensor->offset_mult = -24088;
			sensor->offset_div = 100;
			sensor->slope_mult = 442;
			sensor->slope_div = 10000;
			break;

		case 0x50:
			sensor->offset_mult = -22749;
			sensor->offset_div = 100;
			sensor->slope_mult = 431;
			sensor->slope_div = 10000;
			break;

		case 0x67:
			sensor->offset_mult = -26149;
			sensor->offset_div = 100;
			sensor->slope_mult = 484;
			sensor->slope_div = 10000;
			break;
		}
	}

	headerlen = temp[1];
	recordlen = temp[2];
	entries = temp[3];
	temp = temp + headerlen;

	/* Read the entries from the table */
	for (i = 0; i < entries; i++) {
		s16 value = ROM16(temp[1]);

		switch (temp[0]) {
		case 0x01:
			if ((value & 0x8f) == 0)
				sensor->offset_constant = (value >> 9) & 0x7f;
			break;

		case 0x04:
			if ((value & 0xf00f) == 0xa000) /* core */
				temps->critical = (value&0x0ff0) >> 4;
			break;

		case 0x07:
			if ((value & 0xf00f) == 0xa000) /* core */
				temps->down_clock = (value&0x0ff0) >> 4;
			break;

		case 0x08:
			if ((value & 0xf00f) == 0xa000) /* core */
				temps->fan_boost = (value&0x0ff0) >> 4;
			break;

		case 0x10:
			sensor->offset_mult = value;
			break;

		case 0x11:
			sensor->offset_div = value;
			break;

		case 0x12:
			sensor->slope_mult = value;
			break;

		case 0x13:
			sensor->slope_div = value;
			break;
		}
		temp += recordlen;
	}

	nouveau_therm_safety_checks(ptherm);
}

static bool
probe_monitoring_device(struct nouveau_i2c_chan *i2c,
			struct i2c_board_info *info)
{
	struct nouveau_device *ndev = i2c->device;
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct i2c_client *client;

	request_module("%s%s", I2C_MODULE_PREFIX, info->type);

	client = i2c_new_device(&i2c->adapter, info);
	if (!client)
		return false;

	if (!client->driver || client->driver->detect(client, info)) {
		i2c_unregister_device(client);
		return false;
	}

	/* if the i2c device can drive a fan */
	if (strcmp(info->type, "w83781d") == 0 ||
	    strcmp(info->type, "adt7473") == 0 ||
	    strcmp(info->type, "f75375") == 0 ||
	    strcmp(info->type, "lm63") == 0) {
		ptherm->fan.type = FAN_I2C;
		ptherm->fan.i2c_fan = client;
	}

	return true;
}

static void
nouveau_therm_probe_i2c(struct nouveau_device *ndev)
{
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("w83l785ts", 0x2d) },
		{ I2C_BOARD_INFO("w83781d", 0x2d) },
		{ I2C_BOARD_INFO("adt7473", 0x2e) },
		{ I2C_BOARD_INFO("f75375", 0x2e) },
		{ I2C_BOARD_INFO("lm99", 0x4c) },
		{ I2C_BOARD_INFO("lm90", 0x4c) },
		{ I2C_BOARD_INFO("lm90", 0x4d) },
		{ I2C_BOARD_INFO("adm1021", 0x18) },
		{ I2C_BOARD_INFO("adm1021", 0x19) },
		{ I2C_BOARD_INFO("adm1021", 0x1a) },
		{ I2C_BOARD_INFO("adm1021", 0x29) },
		{ I2C_BOARD_INFO("adm1021", 0x2a) },
		{ I2C_BOARD_INFO("adm1021", 0x2b) },
		{ I2C_BOARD_INFO("adm1021", 0x2b) },
		{ I2C_BOARD_INFO("adm1021", 0x4c) },
		{ I2C_BOARD_INFO("adm1021", 0x4d) },
		{ I2C_BOARD_INFO("adm1021", 0x4e) },
		{ I2C_BOARD_INFO("lm63", 0x18) },
		{ I2C_BOARD_INFO("lm63", 0x4e) },
		{ }
	};

	nouveau_i2c_identify(ndev, "monitoring device", info,
			     probe_monitoring_device, NV_I2C_DEFAULT(0));
}

static void
nouveau_fan_timer_callback(struct nouveau_alarm *alarm)
{
	struct nouveau_therm_priv *priv =
		container_of(alarm, struct nouveau_therm_priv, fan_alarm);
	struct nouveau_therm *ptherm = (struct nouveau_therm *) priv;
	struct nouveau_device *ndev = priv->base.base.device;
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	u16 target_duty;
	u64 period;
	int duty, hysteresis;

	NV_INFO(ndev, "IRQ served!\n");

	/* TODO: make use of PTHERM's IRQs and follow bump_period and
	 * slow_down_period more closely
	 */

	/* schedule the next update */
	period = MIN(ptherm->fan.bump_period, ptherm->fan.slow_down_period);
	if (ptimer)
		ptimer->alarm(ptimer, period * 1000 * 1000, &priv->fan_alarm);

	/* calculate the target fan speed */
	target_duty = nouveau_therm_calc_target_fan_duty(ptherm);

	/* calculate the actual fan speed to be set */
	if (!pfan)
		return;
	duty = pfan->get(pfan);
	if (duty >= 0) {
		/* the constant "3" is roughly taken from nvidia's behaviour.
		 * it is meant to bump the fan speed more incrementally
		 */
		if (duty < target_duty)
			pfan->set(pfan, MIN(duty + 3, target_duty));
		else
			pfan->set(pfan, MAX(duty - 3, target_duty));
	} else
		pfan->set(pfan, target_duty);

	NV_INFO(ndev, "nouveau_fan_timer_callback: period = %i, duty = %i, target_duty = %i (temp = %i), new_speed = %i\n",
		period, duty, target_duty, ptherm->temp_get(ptherm), pfan->get(pfan));
}

int
nouveau_therm_create(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_therm_priv *priv;
	struct nouveau_therm *ptherm;
	struct bit_entry P;
	u8 *temp = NULL;
	int ret;

	ret = nouveau_subdev_create(ndev, subdev, "THERM", "thermal", &priv);
	if (ret)
		return ret;
	ptherm = (struct nouveau_therm *)priv;

	if (ndev->chipset >= 0x40 && ndev->chipset < 0x84)
		ptherm->temp_get = nv40_therm_temp_get;
	else if (ndev->chipset <= 0xd9)
		ptherm->temp_get = nv84_therm_temp_get;

	if (bit_table(ndev, 'P', &P) == 0) {
		if (P.version == 1)
			temp = ROMPTR(ndev, P.data[12]);
		else
		if (P.version == 2)
			temp = ROMPTR(ndev, P.data[16]);
		else
			NV_WARN(ndev, "unknown temp for BIT P %d\n", P.version);

		nouveau_therm_diode_setup(ptherm, temp);
	}

	nouveau_therm_probe_i2c(ndev);

	/* parse the fan part of the therm table */
	if (nouveau_therm_calc_target_fan_duty(ptherm) >= 0) {
		ptherm->calc_fan_duty = nouveau_therm_calc_target_fan_duty;

		/* start fan management on cards PDAEMON-less cards */
		if (ndev->chipset < 0xa3) {
			priv->fan_alarm.func = nouveau_fan_timer_callback;
			nouveau_fan_timer_callback(&priv->fan_alarm);
		}
	}

	/* set the default range for the pwm fan */
	ptherm->fan.min_duty = 30;
	ptherm->fan.max_duty = 100;

	/* set the default fan bump and slow-down periods */
	ptherm->fan.bump_period = 500;
	ptherm->fan.slow_down_period = 2000;

	return nouveau_subdev_init(ndev, subdev, ret);
}
