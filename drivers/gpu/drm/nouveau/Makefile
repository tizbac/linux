#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
nouveau-y := nouveau_device.o nouveau_subdev.o \
             nouveau_drv.o nouveau_state.o nouveau_channel.o nouveau_mem.o \
             nouveau_gpuobj.o nouveau_irq.o nouveau_notifier.o \
             nouveau_sgdma.o nouveau_dma.o nouveau_util.o \
             nouveau_bo.o nouveau_fence.o nouveau_gem.o nouveau_ttm.o \
             nouveau_hw.o nouveau_calc.o nouveau_bios.o nouveau_i2c.o \
             nouveau_display.o nouveau_connector.o nouveau_fbcon.o \
             nouveau_hdmi.o nouveau_dp.o nouveau_ramht.o \
	     nouveau_pm.o nouveau_volt.o nouveau_perf.o nouveau_therm.o \
	     nouveau_mm.o nouveau_vm.o nouveau_mxm.o nouveau_gpio.o \
	     nouveau_fanctl.o nouveau_abi16.o nouveau_prime.o \
             nv04_timer.o \
             nv04_mc.o nv40_mc.o nv50_mc.o \
             nv04_fb.o nv10_fb.o nv20_fb.o nv30_fb.o nv40_fb.o \
             nv50_fb.o nvc0_fb.o \
             nv04_fifo.o nv10_fifo.o nv17_fifo.o nv40_fifo.o nv50_fifo.o \
             nv84_fifo.o nvc0_fifo.o nve0_fifo.o \
             nv04_fence.o nv10_fence.o nv84_fence.o nvc0_fence.o \
             nv04_software.o nv50_software.o nvc0_software.o \
             nv04_graph.o nv10_graph.o nv20_graph.o \
             nv40_graph.o nv50_graph.o nvc0_graph.o nve0_graph.o \
             nv40_grctx.o nv50_grctx.o nvc0_grctx.o nve0_grctx.o \
             nv84_crypt.o nv98_crypt.o \
             nva3_copy.o nvc0_copy.o \
             nv31_mpeg.o nv50_mpeg.o \
             nv84_bsp.o \
             nv84_vp.o \
             nv98_ppp.o \
             nv04_instmem.o nv50_instmem.o nvc0_instmem.o \
             nv04_dac.o nv04_dfp.o nv04_tv.o nv17_tv.o nv17_tv_modes.o \
             nv04_crtc.o nv04_display.o nv04_cursor.o \
             nv50_evo.o nv50_crtc.o nv50_dac.o nv50_sor.o \
             nv50_cursor.o nv50_display.o \
             nvd0_display.o \
             nv04_fbcon.o nv50_fbcon.o nvc0_fbcon.o \
             nv10_gpio.o nv50_gpio.o nvd0_gpio.o \
	     nv50_calc.o \
	     nv04_clock.o nv40_clock.o nv50_clock.o nva3_clock.o nvc0_clock.o \
	     nv40_fanpwm.o nv50_fanpwm.o \
	     nv50_vm.o nvc0_vm.o

nouveau-$(CONFIG_DRM_NOUVEAU_DEBUG) += nouveau_debugfs.o
nouveau-$(CONFIG_COMPAT) += nouveau_ioc32.o
nouveau-$(CONFIG_DRM_NOUVEAU_BACKLIGHT) += nouveau_backlight.o
nouveau-$(CONFIG_ACPI) += nouveau_acpi.o

obj-$(CONFIG_DRM_NOUVEAU)+= nouveau.o
