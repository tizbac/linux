#ifndef __NOUVEAU_FIFO_H__
#define __NOUVEAU_FIFO_H__

struct nouveau_fifo_priv {
	struct nouveau_engine base;
	u32 channels;
};

struct nouveau_fifo_chan {
};

bool nv04_fifo_cache_pull(struct nouveau_device *, bool);
void nv04_fifo_context_del(struct nouveau_channel *, int);
int  nv04_fifo_fini(struct nouveau_device *, int, bool);
int  nv04_fifo_init(struct nouveau_device *, int);
void nv04_fifo_isr(struct nouveau_device *);
void nv04_fifo_destroy(struct nouveau_device *, int);
/*XXX: hack for the moment */
void nv04_fifo_ramht(struct nouveau_device *, struct nouveau_ramht **);

void nv50_fifo_playlist_update(struct nouveau_device *);
void nv50_fifo_destroy(struct nouveau_device *, int);
void nv50_fifo_tlb_flush(struct nouveau_device *, int);

int  nv04_fifo_create(struct nouveau_device *, int engine);
int  nv10_fifo_create(struct nouveau_device *, int engine);
int  nv17_fifo_create(struct nouveau_device *, int engine);
int  nv40_fifo_create(struct nouveau_device *, int engine);
int  nv50_fifo_create(struct nouveau_device *, int engine);
int  nv84_fifo_create(struct nouveau_device *, int engine);
int  nvc0_fifo_create(struct nouveau_device *, int engine);
int  nve0_fifo_create(struct nouveau_device *, int engine);

#endif
