/*
 * Copyright 2010 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_pm.h"
#include "nouveau_gpio.h"
#include "nouveau_timer.h"
#include "nouveau_volt.h"
#include "nouveau_fanctl.h"
#include "nouveau_clock.h"
#include "nouveau_therm.h"
#include "nouveau_hwmon.h"

#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#endif
#include <linux/power_supply.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

static int
nouveau_pm_perflvl_aux(struct nouveau_device *ndev, struct nouveau_pm_level *perflvl,
		       struct nouveau_pm_level *a, struct nouveau_pm_level *b)
{
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	struct nouveau_volt *pvolt = nv_subdev(ndev, NVDEV_SUBDEV_VOLT);
	int ret;

	/*XXX: not on all boards, we should control based on temperature
	 *     on recent boards..  or maybe on some other factor we don't
	 *     know about?
	 */
	if (pfan && a->fanspeed && b->fanspeed && b->fanspeed > a->fanspeed) {
		ret = pfan->set(pfan, perflvl->fanspeed);
		if (ret) {
			NV_ERROR(ndev, "fanspeed set failed: %d\n", ret);
			return ret;
		}
	}

	if (pvolt) {
		if (perflvl->volt_min && b->volt_min > a->volt_min) {
			ret = pvolt->set(pvolt, perflvl->volt_min);
			if (ret) {
				NV_ERROR(ndev, "voltage set failed: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static int
nouveau_pm_perflvl_set(struct nouveau_device *ndev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_clock *pclk = nv_subdev(ndev, NVDEV_SUBDEV_CLOCK);
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	void *state;
	int ret;

	if (perflvl == pm->cur)
		return 0;

	ret = nouveau_pm_perflvl_aux(ndev, perflvl, pm->cur, perflvl);
	if (ret)
		return ret;

	if (pclk) {
		state = pclk->perf_pre(pclk, perflvl);
		if (IS_ERR(state)) {
			ret = PTR_ERR(state);
			goto error;
		}

		ret = pclk->perf_set(pclk, state);
		if (ret)
			goto error;
	}

	ret = nouveau_pm_perflvl_aux(ndev, perflvl, perflvl, pm->cur);
	if (ret)
		return ret;

	pm->cur = perflvl;
	return 0;

error:
	/* restore the fan speed and voltage before leaving */
	nouveau_pm_perflvl_aux(ndev, perflvl, perflvl, pm->cur);
	return ret;
}

void
nouveau_pm_trigger(struct nouveau_device *ndev)
{
	struct nouveau_timer *ptimer = nv_subdev(ndev, NVDEV_SUBDEV_TIMER);
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_profile *profile = NULL;
	struct nouveau_pm_level *perflvl = NULL;
	int ret;

	/* select power profile based on current power source */
	if (power_supply_is_system_supplied())
		profile = pm->profile_ac;
	else
		profile = pm->profile_dc;

	if (profile != pm->profile) {
		pm->profile->func->fini(pm->profile);
		pm->profile = profile;
		pm->profile->func->init(pm->profile);
	}

	/* select performance level based on profile */
	perflvl = profile->func->select(profile);

	/* change perflvl, if necessary */
	if (perflvl != pm->cur) {
		u64 time0 = ptimer->read(ptimer);

		NV_INFO(ndev, "setting performance level: %d", perflvl->id);
		ret = nouveau_pm_perflvl_set(ndev, perflvl);
		if (ret)
			NV_INFO(ndev, "> reclocking failed: %d\n\n", ret);

		NV_INFO(ndev, "> reclocking took %lluns\n\n",
			     ptimer->read(ptimer) - time0);
	}
}

static struct nouveau_pm_profile *
profile_find(struct nouveau_device *ndev, const char *string)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_profile *profile;

	list_for_each_entry(profile, &pm->profiles, head) {
		if (!strncmp(profile->name, string, sizeof(profile->name)))
			return profile;
	}

	return NULL;
}

static int
nouveau_pm_profile_set(struct nouveau_device *ndev, const char *profile)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_profile *ac = NULL, *dc = NULL;
	char string[16], *cur = string, *ptr;

	/* safety precaution, for now */
	if (nouveau_perflvl_wr != 7777)
		return -EPERM;

	strncpy(string, profile, sizeof(string));
	string[sizeof(string) - 1] = 0;
	if ((ptr = strchr(string, '\n')))
		*ptr = '\0';

	ptr = strsep(&cur, ",");
	if (ptr)
		ac = profile_find(ndev, ptr);

	ptr = strsep(&cur, ",");
	if (ptr)
		dc = profile_find(ndev, ptr);
	else
		dc = ac;

	if (ac == NULL || dc == NULL)
		return -EINVAL;

	pm->profile_ac = ac;
	pm->profile_dc = dc;
	nouveau_pm_trigger(ndev);
	return 0;
}

static void
nouveau_pm_static_dummy(struct nouveau_pm_profile *profile)
{
}

static struct nouveau_pm_level *
nouveau_pm_static_select(struct nouveau_pm_profile *profile)
{
	return container_of(profile, struct nouveau_pm_level, profile);
}

const struct nouveau_pm_profile_func nouveau_pm_static_profile_func = {
	.destroy = nouveau_pm_static_dummy,
	.init = nouveau_pm_static_dummy,
	.fini = nouveau_pm_static_dummy,
	.select = nouveau_pm_static_select,
};

static int
nouveau_pm_perflvl_get(struct nouveau_device *ndev, struct nouveau_pm_level *perflvl)
{
	struct nouveau_fanctl *pfan = nv_subdev(ndev, NVDEV_SUBDEV_FAN0);
	struct nouveau_clock *pclk = nv_subdev(ndev, NVDEV_SUBDEV_CLOCK);
	struct nouveau_volt *pvolt = nv_subdev(ndev, NVDEV_SUBDEV_VOLT);
	int ret;

	memset(perflvl, 0, sizeof(*perflvl));

	if (pclk) {
		ret = pclk->perf_get(pclk, perflvl);
		if (ret)
			return ret;
	}

	if (pvolt) {
		ret = pvolt->get(pvolt);
		if (ret > 0) {
			perflvl->volt_min = ret;
			perflvl->volt_max = ret;
		}
	}

	if (pfan) {
		ret = pfan->get(pfan);
		if (ret > 0)
			perflvl->fanspeed = ret;
	}

	nouveau_mem_timing_read(ndev, &perflvl->timing);
	return 0;
}

static void
nouveau_pm_perflvl_info(struct nouveau_pm_level *perflvl, char *ptr, int len)
{
	char c[16], s[16], v[32], f[16], m[16];

	c[0] = '\0';
	if (perflvl->core)
		snprintf(c, sizeof(c), " core %dMHz", perflvl->core / 1000);

	s[0] = '\0';
	if (perflvl->shader)
		snprintf(s, sizeof(s), " shader %dMHz", perflvl->shader / 1000);

	m[0] = '\0';
	if (perflvl->memory)
		snprintf(m, sizeof(m), " memory %dMHz", perflvl->memory / 1000);

	v[0] = '\0';
	if (perflvl->volt_min && perflvl->volt_min != perflvl->volt_max) {
		snprintf(v, sizeof(v), " voltage %dmV-%dmV",
			 perflvl->volt_min / 1000, perflvl->volt_max / 1000);
	} else
	if (perflvl->volt_min) {
		snprintf(v, sizeof(v), " voltage %dmV",
			 perflvl->volt_min / 1000);
	}

	f[0] = '\0';
	if (perflvl->fanspeed)
		snprintf(f, sizeof(f), " fanspeed %d%%", perflvl->fanspeed);

	snprintf(ptr, len, "%s%s%s%s%s\n", c, s, m, v, f);
}

static ssize_t
nouveau_pm_get_perflvl_info(struct device *d,
			    struct device_attribute *a, char *buf)
{
	struct nouveau_pm_level *perflvl =
		container_of(a, struct nouveau_pm_level, dev_attr);
	char *ptr = buf;
	int len = PAGE_SIZE;

	snprintf(ptr, len, "%d:", perflvl->id);
	ptr += strlen(buf);
	len -= strlen(buf);

	nouveau_pm_perflvl_info(perflvl, ptr, len);
	return strlen(buf);
}

static ssize_t
nouveau_pm_get_perflvl(struct device *d, struct device_attribute *a, char *buf)
{
	struct drm_device *dev = pci_get_drvdata(to_pci_dev(d));
	struct nouveau_device *ndev = nouveau_device(dev);
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_level cur;
	int len = PAGE_SIZE, ret;
	char *ptr = buf;

	snprintf(ptr, len, "profile: %s, %s\nc:",
		 pm->profile_ac->name, pm->profile_dc->name);
	ptr += strlen(buf);
	len -= strlen(buf);

	ret = nouveau_pm_perflvl_get(ndev, &cur);
	if (ret == 0)
		nouveau_pm_perflvl_info(&cur, ptr, len);
	return strlen(buf);
}

static ssize_t
nouveau_pm_set_perflvl(struct device *d, struct device_attribute *a,
		       const char *buf, size_t count)
{
	struct drm_device *dev = pci_get_drvdata(to_pci_dev(d));
	struct nouveau_device *ndev = nouveau_device(dev);
	int ret;

	ret = nouveau_pm_profile_set(ndev, buf);
	if (ret)
		return ret;
	return strlen(buf);
}

static DEVICE_ATTR(performance_level, S_IRUGO | S_IWUSR,
		   nouveau_pm_get_perflvl, nouveau_pm_set_perflvl);

static int
nouveau_sysfs_init(struct nouveau_device *ndev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct device *d = &ndev->dev->pdev->dev;
	int ret, i;

	ret = device_create_file(d, &dev_attr_performance_level);
	if (ret)
		return ret;

	for (i = 0; i < pm->nr_perflvl; i++) {
		struct nouveau_pm_level *perflvl = &pm->perflvl[i];

		perflvl->dev_attr.attr.name = perflvl->name;
		perflvl->dev_attr.attr.mode = S_IRUGO;
		perflvl->dev_attr.show = nouveau_pm_get_perflvl_info;
		perflvl->dev_attr.store = NULL;
		sysfs_attr_init(&perflvl->dev_attr.attr);

		ret = device_create_file(d, &perflvl->dev_attr);
		if (ret) {
			NV_ERROR(ndev, "failed pervlvl %d sysfs: %d\n",
				 perflvl->id, i);
			perflvl->dev_attr.attr.name = NULL;
			nouveau_pm_fini(ndev);
			return ret;
		}
	}

	return 0;
}

static void
nouveau_sysfs_fini(struct nouveau_device *ndev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct device *d = &ndev->dev->pdev->dev;
	int i;

	device_remove_file(d, &dev_attr_performance_level);
	for (i = 0; i < pm->nr_perflvl; i++) {
		struct nouveau_pm_level *pl = &pm->perflvl[i];

		if (!pl->dev_attr.attr.name)
			break;

		device_remove_file(d, &pl->dev_attr);
	}
}

#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
static int
nouveau_pm_acpi_event(struct notifier_block *nb, unsigned long val, void *data)
{
	struct nouveau_device *ndev =
		container_of(nb, struct nouveau_device, subsys.pm.acpi_nb);
	struct acpi_bus_event *entry = (struct acpi_bus_event *)data;

	if (strcmp(entry->device_class, "ac_adapter") == 0) {
		bool ac = power_supply_is_system_supplied();

		NV_DEBUG(ndev, "power supply changed: %s\n", ac ? "AC" : "DC");
		nouveau_pm_trigger(ndev);
	}

	return NOTIFY_OK;
}
#endif

int
nouveau_pm_init(struct nouveau_device *ndev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	char info[256];
	int ret, i;

	/* determine current ("boot") performance level */
	ret = nouveau_pm_perflvl_get(ndev, &pm->boot);
	if (ret) {
		NV_ERROR(ndev, "failed to determine boot perflvl\n");
		return ret;
	}

	strncpy(pm->boot.name, "boot", 4);
	strncpy(pm->boot.profile.name, "boot", 4);
	pm->boot.profile.func = &nouveau_pm_static_profile_func;

	INIT_LIST_HEAD(&pm->profiles);
	list_add(&pm->boot.profile.head, &pm->profiles);

	pm->profile_ac = &pm->boot.profile;
	pm->profile_dc = &pm->boot.profile;
	pm->profile = &pm->boot.profile;
	pm->cur = &pm->boot;

	/* add performance levels from vbios */
	nouveau_perf_init(ndev);

	/* display available performance levels */
	NV_INFO(ndev, "%d available performance level(s)\n", pm->nr_perflvl);
	for (i = 0; i < pm->nr_perflvl; i++) {
		nouveau_pm_perflvl_info(&pm->perflvl[i], info, sizeof(info));
		NV_INFO(ndev, "%d:%s", pm->perflvl[i].id, info);
	}

	nouveau_pm_perflvl_info(&pm->boot, info, sizeof(info));
	NV_INFO(ndev, "c:%s", info);

	/* switch performance levels now if requested */
	if (nouveau_perflvl != NULL)
		nouveau_pm_profile_set(ndev, nouveau_perflvl);

	nouveau_sysfs_init(ndev);
	nouveau_hwmon_init(ndev);
#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
	pm->acpi_nb.notifier_call = nouveau_pm_acpi_event;
	register_acpi_notifier(&pm->acpi_nb);
#endif

	return 0;
}

void
nouveau_pm_fini(struct nouveau_device *ndev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_profile *profile, *tmp;

	list_for_each_entry_safe(profile, tmp, &pm->profiles, head) {
		list_del(&profile->head);
		profile->func->destroy(profile);
	}

	if (pm->cur != &pm->boot)
		nouveau_pm_perflvl_set(ndev, &pm->boot);

	nouveau_perf_fini(ndev);

#if defined(CONFIG_ACPI) && defined(CONFIG_POWER_SUPPLY)
	unregister_acpi_notifier(&pm->acpi_nb);
#endif
	nouveau_hwmon_fini(ndev);
	nouveau_sysfs_fini(ndev);
}

void
nouveau_pm_resume(struct nouveau_device *ndev)
{
	struct nouveau_pm_engine *pm = &ndev->subsys.pm;
	struct nouveau_pm_level *perflvl;

	if (!pm->cur || pm->cur == &pm->boot)
		return;

	perflvl = pm->cur;
	pm->cur = &pm->boot;
	nouveau_pm_perflvl_set(ndev, perflvl);
}
