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
#include "nouveau_volt.h"

static const enum dcb_gpio_tag vidtag[] = { 0x04, 0x05, 0x06, 0x1a, 0x73 };
static int nr_vidtag = sizeof(vidtag) / sizeof(vidtag[0]);

static int
nouveau_volt_iduv(struct nouveau_volt *pvolt, int vid)
{
	int i;

	for (i = 0; i < pvolt->nr_level; i++) {
		if (pvolt->level[i].vid == vid)
			return pvolt->level[i].voltage;
	}

	return -ENOENT;
}

static int
nouveau_volt_uvid(struct nouveau_volt *pvolt, int voltage)
{
	int i;

	for (i = 0; i < pvolt->nr_level; i++) {
		if (pvolt->level[i].voltage == voltage)
			return pvolt->level[i].vid;
	}

	return -ENOENT;
}

static int
nouveau_volt_get(struct nouveau_volt *pvolt)
{
	struct nouveau_device *ndev = pvolt->base.device;
	u8 vid = 0;
	int i;

	for (i = 0; i < nr_vidtag; i++) {
		if (!(pvolt->vid_mask & (1 << i)))
			continue;

		vid |= nouveau_gpio_func_get(ndev, vidtag[i]) << i;
	}

	return pvolt->iduv(pvolt, vid);
}

static int
nouveau_volt_set(struct nouveau_volt *pvolt, int voltage)
{
	struct nouveau_device *ndev = pvolt->base.device;
	int vid, i;

	vid = pvolt->uvid(pvolt, voltage);
	if (vid < 0)
		return vid;

	for (i = 0; i < nr_vidtag; i++) {
		if (!(pvolt->vid_mask & (1 << i)))
			continue;

		nouveau_gpio_func_set(ndev, vidtag[i], !!(vid & (1 << i)));
	}

	return 0;
}

int
nouveau_volt_create(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_bios *bios = nv_subdev(ndev, NVDEV_SUBDEV_VBIOS);
	struct nouveau_volt *pvolt;
	struct bit_entry P;
	int headerlen, recordlen, entries, vidmask, vidshift;
	int ret, i;
	u8 *volt = NULL;

	if (bios->type == NVBIOS_BIT) {
		if (bit_table(ndev, 'P', &P))
			return 0;

		if (P.version == 1)
			volt = ROMPTR(ndev, P.data[16]);
		else
		if (P.version == 2)
			volt = ROMPTR(ndev, P.data[12]);
		else {
			NV_WARN(ndev, "unknown volt for BIT P %d\n", P.version);
		}
	} else {
		if (bios->data[bios->offset + 6] < 0x27) {
			NV_DEBUG(ndev, "BMP version too old for voltage\n");
			return 0;
		}

		volt = ROMPTR(ndev, bios->data[bios->offset + 0x98]);
	}

	if (!volt) {
		NV_DEBUG(ndev, "voltage table pointer invalid\n");
		return 0;
	}

	switch (volt[0]) {
	case 0x10:
	case 0x11:
	case 0x12:
		headerlen = 5;
		recordlen = volt[1];
		entries   = volt[2];
		vidshift  = 0;
		vidmask   = volt[4];
		break;
	case 0x20:
		headerlen = volt[1];
		recordlen = volt[3];
		entries   = volt[2];
		vidshift  = 0; /* could be vidshift like 0x30? */
		vidmask   = volt[5];
		break;
	case 0x30:
		headerlen = volt[1];
		recordlen = volt[2];
		entries   = volt[3];
		vidmask   = volt[4];
		/* no longer certain what volt[5] is, if it's related to
		 * the vid shift then it's definitely not a function of
		 * how many bits are set.
		 *
		 * after looking at a number of nva3+ vbios images, they
		 * all seem likely to have a static shift of 2.. lets
		 * go with that for now until proven otherwise.
		 */
		vidshift  = 2;
		break;
	case 0x40:
		headerlen = volt[1];
		recordlen = volt[2];
		entries   = volt[3]; /* not a clue what the entries are for.. */
		vidmask   = volt[11]; /* guess.. */
		vidshift  = 0;
		break;
	default:
		NV_WARN(ndev, "voltage table 0x%02x unknown\n", volt[0]);
		return 0;
	}

	/* validate vid mask */
	if (!vidmask || (vidmask & ~((1 << nr_vidtag) - 1))) {
		NV_DEBUG(ndev, "unsupported vid mask 0x%x\n", vidmask);
		return 0;
	}

	for (i = 0; i < nr_vidtag; i++) {
		if (vidmask & (1 << i)) {
			if (!nouveau_gpio_func_valid(ndev, vidtag[i])) {
				NV_DEBUG(ndev, "no gpio for vid bit %d\n", i);
				return 0;
			}
		}
	}

	/* parse vbios entries into common format */
	if (volt[0] >= 0x40)
		entries = vidmask + 1;

	ret = nouveau_subdev_create_(ndev, subdev, sizeof(*pvolt) +
				     sizeof(pvolt->level[0]) * entries,
				     "VOLT", "voltage", (void **)&pvolt);
	if (ret)
		return ret;

	pvolt->version = volt[0];
	pvolt->iduv = nouveau_volt_iduv;
	pvolt->uvid = nouveau_volt_uvid;
	pvolt->get = nouveau_volt_get;
	pvolt->set = nouveau_volt_set;
	pvolt->vid_mask = vidmask;
	pvolt->nr_level = entries;

	if (volt[0] < 0x40) {
		u8 *entry = volt + headerlen;
		for (i = 0; i < entries; i++, entry += recordlen) {
			pvolt->level[i].voltage = entry[0] * 10000;
			pvolt->level[i].vid     = entry[1] >> vidshift;
		}
	} else {
		u32 volt_uv = ROM32(volt[4]);
		s16 step_uv = ROM16(volt[8]);
		u8 vid;

		for (vid = 0; vid <= pvolt->vid_mask; vid++) {
			pvolt->level[vid].voltage = volt_uv;
			pvolt->level[vid].vid = vid;
			volt_uv += step_uv;
		}
	}

	return nouveau_subdev_init(ndev, subdev, ret);
}
