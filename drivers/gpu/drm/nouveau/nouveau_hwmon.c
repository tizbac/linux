/*
 * Copyright 2012 Nouveau community
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
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_therm.h"
#include "nouveau_fanctl.h"
#include "nouveau_hwmon.h"
#include "nouveau_drv.h"

#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))

static ssize_t
nouveau_hwmon_show_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);

	return snprintf(buf, PAGE_SIZE, "%d\n", ptherm->temp_get(ptherm)*1000);
}
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, nouveau_hwmon_show_temp,
						  NULL, 0);

static ssize_t
nouveau_hwmon_max_temp(struct device *d, struct device_attribute *a, char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct nouveau_therm_threshold_temp *temp = &ptherm->threshold_temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp->down_clock * 1000);
}
static ssize_t
nouveau_hwmon_set_max_temp(struct device *d, struct device_attribute *a,
						const char *buf, size_t count)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	ptherm->threshold_temp.down_clock = value / 1000;
	nouveau_therm_safety_checks(ptherm);
	return count;
}
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR, nouveau_hwmon_max_temp,
						  nouveau_hwmon_set_max_temp,
						  0);

static ssize_t
nouveau_hwmon_critical_temp(struct device *d, struct device_attribute *a,
							char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct nouveau_therm_threshold_temp *temp = &ptherm->threshold_temp;

	return snprintf(buf, PAGE_SIZE, "%d\n", temp->critical * 1000);
}
static ssize_t
nouveau_hwmon_set_critical_temp(struct device *d, struct device_attribute *a,
							    const char *buf,
								size_t count)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return count;

	ptherm->threshold_temp.critical = value / 1000;
	nouveau_therm_safety_checks(ptherm);
	return count;
}
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO | S_IWUSR,
						nouveau_hwmon_critical_temp,
						nouveau_hwmon_set_critical_temp,
						0);

static ssize_t nouveau_hwmon_show_name(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "nouveau\n");
}
static SENSOR_DEVICE_ATTR(name, S_IRUGO, nouveau_hwmon_show_name, NULL, 0);

static ssize_t nouveau_hwmon_show_update_rate(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "1000\n");
}
static SENSOR_DEVICE_ATTR(update_rate, S_IRUGO,
						nouveau_hwmon_show_update_rate,
						NULL, 0);

static ssize_t
nouveau_hwmon_show_fan0_input(struct device *d, struct device_attribute *attr,
			      char *buf)
{

	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	int ret;

	ret = pfan->sense(pfan);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}
static SENSOR_DEVICE_ATTR(fan0_input, S_IRUGO, nouveau_hwmon_show_fan0_input,
			  NULL, 0);

static ssize_t
nouveau_hwmon_get_pwm0(struct device *d, struct device_attribute *a, char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	int ret;

	ret = pfan->get(pfan);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%i\n", ret);
}

static ssize_t
nouveau_hwmon_set_pwm0(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	int ret = -ENODEV;
	long value;

	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < ptherm->fan.min_duty)
		value = ptherm->fan.min_duty;
	if (value > ptherm->fan.max_duty)
		value = ptherm->fan.max_duty;

	ret = pfan->set(pfan, value);
	if (ret)
		return ret;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0,
			  nouveau_hwmon_set_pwm0, 0);

static ssize_t
nouveau_hwmon_get_pwm0_min(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);

	return sprintf(buf, "%i\n", ptherm->fan.min_duty);
}

static ssize_t
nouveau_hwmon_set_pwm0_min(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < 0)
		value = 0;

	if (ptherm->fan.max_duty - value < 10)
		value = ptherm->fan.max_duty - 10;

	if (value < 10)
		ptherm->fan.min_duty = 10;
	else
		ptherm->fan.min_duty = value;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0_min, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0_min,
			  nouveau_hwmon_set_pwm0_min, 0);

static ssize_t
nouveau_hwmon_get_pwm0_max(struct device *d,
			   struct device_attribute *a, char *buf)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);

	return sprintf(buf, "%i\n", ptherm->fan.max_duty);
}

static ssize_t
nouveau_hwmon_set_pwm0_max(struct device *d, struct device_attribute *a,
			   const char *buf, size_t count)
{
	struct nouveau_device *ndev = nouveau_device(dev_get_drvdata(d));
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	long value;

	if (kstrtol(buf, 10, &value) == -EINVAL)
		return -EINVAL;

	if (value < 0)
		value = 0;

	if (value - ptherm->fan.min_duty < 10)
		value = ptherm->fan.min_duty + 10;

	if (value > 100)
		ptherm->fan.max_duty = 100;
	else
		ptherm->fan.max_duty = value;

	return count;
}

static SENSOR_DEVICE_ATTR(pwm0_max, S_IRUGO | S_IWUSR,
			  nouveau_hwmon_get_pwm0_max,
			  nouveau_hwmon_set_pwm0_max, 0);

static struct attribute *hwmon_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_name.dev_attr.attr,
	&sensor_dev_attr_update_rate.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_fan_rpm_attributes[] = {
	&sensor_dev_attr_fan0_input.dev_attr.attr,
	NULL
};
static struct attribute *hwmon_pwm_fan_attributes[] = {
	&sensor_dev_attr_pwm0.dev_attr.attr,
	&sensor_dev_attr_pwm0_min.dev_attr.attr,
	&sensor_dev_attr_pwm0_max.dev_attr.attr,
	NULL
};

static const struct attribute_group hwmon_attrgroup = {
	.attrs = hwmon_attributes,
};
static const struct attribute_group hwmon_fan_rpm_attrgroup = {
	.attrs = hwmon_fan_rpm_attributes,
};
static const struct attribute_group hwmon_pwm_fan_attrgroup = {
	.attrs = hwmon_pwm_fan_attributes,
};
#endif

void
nouveau_hwmon_fini(struct nouveau_device *ndev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct drm_device *dev = ndev->dev;

	if (pm->hwmon_dev) {
		sysfs_remove_group(&dev->pdev->dev.kobj, &hwmon_attrgroup);
		sysfs_remove_group(&dev->pdev->dev.kobj,
				   &hwmon_pwm_fan_attrgroup);
		sysfs_remove_group(&dev->pdev->dev.kobj,
				   &hwmon_fan_rpm_attrgroup);

		hwmon_device_unregister(pm->hwmon_dev);
	}
#endif
}

int
nouveau_hwmon_init(struct nouveau_device *ndev)
{
#if defined(CONFIG_HWMON) || (defined(MODULE) && defined(CONFIG_HWMON_MODULE))
	struct nouveau_therm *ptherm = nv_subdev(ndev, NVDEV_SUBDEV_THERM);
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct drm_device *dev = ndev->dev;
	struct device *hwmon_dev;
	int ret = 0;

	hwmon_dev = hwmon_device_register(&dev->pdev->dev);
	if (IS_ERR(hwmon_dev)) {
		ret = PTR_ERR(hwmon_dev);
		NV_ERROR(ndev,
			"Unable to register hwmon device: %d\n", ret);
		return ret;
	}
	dev_set_drvdata(hwmon_dev, ndev);

	/* if we can read the temperature */
	if (ptherm && ptherm->temp_get(ptherm) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_attrgroup);
		if (ret) {
			if (ret)
				goto error;
		}
	}

	/* if the card has a drivable fan */
	if (pfan && pfan->get(pfan) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_pwm_fan_attrgroup);
		if (ret)
			goto error;
	}

	/* if the card can read the fan rpm */
	if (pfan && pfan->sense(pfan) >= 0) {
		ret = sysfs_create_group(&dev->pdev->dev.kobj,
					 &hwmon_fan_rpm_attrgroup);
		if (ret)
			goto error;
	}

	pm->hwmon_dev = hwmon_dev;

	return 0;

error:
	NV_ERROR(ndev, "Unable to create some hwmon sysfs files: %d\n", ret);
	hwmon_device_unregister(hwmon_dev);
	return ret;
#else
	return -ENODEV;
#endif
}

