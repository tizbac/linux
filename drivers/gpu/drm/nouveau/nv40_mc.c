#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv40_mc_init(struct nouveau_device *ndev)
{
	/* Power up everything, resetting each individual unit will
	 * be done later if needed.
	 */
	nv_wr32(ndev, NV03_PMC_ENABLE, 0xFFFFFFFF);

	if (nv44_graph_class(ndev)) {
		u32 tmp = nv_rd32(ndev, NV04_PFB_FIFO_DATA);
		nv_wr32(ndev, NV40_PMC_1700, tmp);
		nv_wr32(ndev, NV40_PMC_1704, 0);
		nv_wr32(ndev, NV40_PMC_1708, 0);
		nv_wr32(ndev, NV40_PMC_170C, tmp);
	}

	return 0;
}

void
nv40_mc_takedown(struct nouveau_device *ndev)
{
}
