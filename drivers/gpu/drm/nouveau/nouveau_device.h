#ifndef __NOUVEAU_DEVICE_H__
#define __NOUVEAU_DEVICE_H__

int  nouveau_device_init(struct nouveau_device *);
int  nouveau_device_fini(struct nouveau_device *, bool suspend);
void nouveau_device_destroy(struct nouveau_device *);
int  nouveau_device_create(struct nouveau_device *);

#endif
