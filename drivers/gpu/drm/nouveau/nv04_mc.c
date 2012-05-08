#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"

int
nv04_mc_init(struct nouveau_device *ndev)
{
	/* Power up everything, resetting each individual unit will
	 * be done later if needed.
	 */

	nv_wr32(ndev, NV03_PMC_ENABLE, 0xFFFFFFFF);

	/* Disable PROM access. */
	nv_wr32(ndev, NV_PBUS_PCI_NV_20, NV_PBUS_PCI_NV_20_ROM_SHADOW_ENABLED);

	return 0;
}

void
nv04_mc_takedown(struct nouveau_device *ndev)
{
}
