#ifndef __NOUVEAU_GRAPH_H__
#define __NOUVEAU_GRAPH_H__

struct nouveau_graph_priv {
	struct nouveau_engine base;
};

int nv04_graph_create(struct nouveau_device *, int engine);
int nv10_graph_create(struct nouveau_device *, int engine);
int nv20_graph_create(struct nouveau_device *, int engine);
int nv40_graph_create(struct nouveau_device *, int engine);
int nv50_graph_create(struct nouveau_device *, int engine);
int nvc0_graph_create(struct nouveau_device *, int engine);
int nve0_graph_create(struct nouveau_device *, int engine);


int  nv04_graph_object_new(struct nouveau_channel *, int, u32, u16);
int  nv04_graph_mthd_page_flip(struct nouveau_channel *chan,
				      u32 class, u32 mthd, u32 data);
extern struct nouveau_bitfield nv04_graph_nsource[];

struct nouveau_channel *nv10_graph_channel(struct nouveau_device *);
extern struct nouveau_bitfield nv10_graph_intr[];
extern struct nouveau_bitfield nv10_graph_nstatus[];

void nv40_grctx_init(struct nouveau_device *, u32 *size);
void nv40_grctx_fill(struct nouveau_device *, struct nouveau_gpuobj *);

extern struct nouveau_enum nv50_data_error_names[];
int  nv50_graph_isr_chid(struct nouveau_device *, u64 inst);
int  nv50_grctx_init(struct nouveau_device *, u32 *, u32, u32 *, u32 *);
void nv50_grctx_fill(struct nouveau_device *, struct nouveau_gpuobj *);

int  nvc0_graph_isr_chid(struct nouveau_device *, u64 inst);

#endif
