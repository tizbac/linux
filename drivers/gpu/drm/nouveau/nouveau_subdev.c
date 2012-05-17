/*
 * Copyright 2012 Red Hat Inc.
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

int
nouveau_subdev_init(struct nouveau_device *ndev, int subdev, int ret)
{
	struct nouveau_subdev *sdev = nv_subdev(ndev, subdev);

	if (!sdev)
		return ret;

	switch (sdev->state) {
	case NVDEV_SUBDEV_CREATED:
		if (ret < 0) {
			NV_ERROR(ndev, "%s: failed to create subsystem, %d\n",
				 sdev->name, ret);
			nouveau_subdev_destroy(ndev, subdev);
			return ret;
		}

		NV_INFO(ndev, "%s: created\n", sdev->name);
		sdev->refcount = 1;

	case NVDEV_SUBDEV_SUSPEND:
		nouveau_subdev_fini(ndev, subdev, false);
		if (ret & 1)
			break;

	case NVDEV_SUBDEV_STOPPED:
		if (sdev->refcount) {
			if (sdev->init)
				ret = sdev->init(ndev, subdev);

			if (ret) {
				NV_ERROR(ndev, "%s: failed to initialise, %d\n",
					 sdev->name, ret);
				return ret;
			}

			NV_INFO(ndev, "%s: initialised\n", sdev->name);
			sdev->state = NVDEV_SUBDEV_RUNNING;
		}
		break;
	default:
		break;
	}

	return 0;
}

int
nouveau_subdev_fini(struct nouveau_device *ndev, int subdev, bool suspend)
{
	struct nouveau_subdev *sdev = nv_subdev(ndev, subdev);
	int ret = 0;

	if (sdev && sdev->state < NVDEV_SUBDEV_STOPPED) {
		if (sdev->fini)
			ret = sdev->fini(ndev, subdev, suspend);

		if (sdev->unit) {
			nv_mask(ndev, 0x000200, sdev->unit, 0x00000000);
			nv_mask(ndev, 0x000200, sdev->unit, sdev->unit);
		}

		if (ret) {
			NV_ERROR(ndev, "%s: failed to suspend, %d\n",
				 sdev->name, ret);
			return ret;
		}

		if (suspend) {
			NV_INFO(ndev, "%s: suspended\n", sdev->name);
			sdev->state = NVDEV_SUBDEV_SUSPEND;
		} else {
			NV_INFO(ndev, "%s: stopped\n", sdev->name);
			sdev->state = NVDEV_SUBDEV_STOPPED;
		}
	}

	return ret;
}

void
nouveau_subdev_destroy(struct nouveau_device *ndev, int subdev)
{
	struct nouveau_subdev *sdev = nv_subdev(ndev, subdev);
	if (sdev) {
		if (sdev->destroy)
			sdev->destroy(ndev, subdev);
		if (sdev->state != NVDEV_SUBDEV_CREATED)
			NV_INFO(ndev, "%s: destroyed\n", sdev->name);
		ndev->subdev[subdev] = NULL;
		kfree(sdev);
	}
}

int
nouveau_subdev_create_(struct nouveau_device *ndev, int subdev, int length,
		       const char *subname, const char *sysname, void **data)
{
	struct nouveau_subdev *sdev;

	sdev = *data = kzalloc(length, GFP_KERNEL);
	if (!sdev) {
		NV_ERROR(ndev, "%s: unable to allocate subsystem\n", subname);
		return -ENOMEM;
	}

	sdev->device = ndev;
	sdev->name = subname;
	sdev->state = NVDEV_SUBDEV_CREATED;
	sdev->handle = subdev;
	sdev->oclass = NVDEV_SUBDEV_CLASS_SUBDEV;
	mutex_init(&sdev->mutex);

	ndev->subdev[subdev] = sdev;
	return 0;
}
