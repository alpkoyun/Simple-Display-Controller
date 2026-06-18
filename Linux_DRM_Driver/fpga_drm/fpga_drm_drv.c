// SPDX-License-Identifier: GPL-2.0
/*
 * Multi-mode DRM/KMS driver for the FPGA PCIe HDMI stream design.
 *
 * The FPGA accepts XDMA H2C AXI-stream packets. Each packet is one active
 * XRGB8888 scanline, so TLAST must occur once per line.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>

#include "libxdma_api.h"
#include "libxdma.h"

#define DRIVER_NAME	"fpga_drm"
#define DRIVER_DESC	"FPGA PCIe XDMA multi-mode DRM driver"
#define DRIVER_MAJOR	0
#define DRIVER_MINOR	2

#define FPGA_DRM_MAX_WIDTH	1920
#define FPGA_DRM_MAX_HEIGHT	1080
#define FPGA_DRM_BPP		4
#define FPGA_DRM_MAX_LINE_BYTES	(FPGA_DRM_MAX_WIDTH * FPGA_DRM_BPP)
#define FPGA_DRM_MAX_FRAME_BYTES	\
	(FPGA_DRM_MAX_LINE_BYTES * FPGA_DRM_MAX_HEIGHT)
#define FPGA_DRM_TIMEOUT_MS	1000
#define FPGA_DRM_MAX_PIXEL_CLOCK_KHZ	148500

#define FPGA_HW_FRAME_COUNT		4U
#define FPGA_HW_FRAME_SPACING		(FPGA_DRM_MAX_FRAME_BYTES + 0x1000U)
#define FPGA_HW_FRAME_BASE		0x81000000ULL

#define FPGA_HW_COLOR_CONVERT_BASE	0x00000000ULL
#define FPGA_HW_PIXEL_UNPACK_BASE	0x00010000ULL
#define FPGA_HW_AXI_IIC_BASE		0x00020000ULL
#define FPGA_HW_VDMA_BASE		0x00040000ULL
#define FPGA_HW_VTC_BASE		0x00050000ULL
#define FPGA_HW_VIDEO_CLK_WIZ_BASE	0x00060000ULL
#define FPGA_HW_VIDEO_LOCK_GPIO_BASE	0x00070000ULL
#define FPGA_HW_DDR_BASE		0x00080000ULL

#define FPGA_HW_REG_WINDOW		0x10000ULL

#define HLS_PIXEL_UNPACK_MODE		0x10
#define HLS_COLOR_C1_C1		0x10
#define HLS_COLOR_C1_C2		0x18
#define HLS_COLOR_C1_C3		0x20
#define HLS_COLOR_C2_C1		0x28
#define HLS_COLOR_C2_C2		0x30
#define HLS_COLOR_C2_C3		0x38
#define HLS_COLOR_C3_C1		0x40
#define HLS_COLOR_C3_C2		0x48
#define HLS_COLOR_C3_C3		0x50
#define HLS_COLOR_BIAS_C1		0x58
#define HLS_COLOR_BIAS_C2		0x60
#define HLS_COLOR_BIAS_C3		0x68

#define AXI_VDMA_TX_OFFSET		0x000
#define AXI_VDMA_RX_OFFSET		0x030
#define AXI_VDMA_PARKPTR_OFFSET	0x028
#define AXI_VDMA_CR_OFFSET		0x000
#define AXI_VDMA_SR_OFFSET		0x004
#define AXI_VDMA_FRMSTORE_OFFSET	0x018
#define AXI_VDMA_MM2S_ADDR_OFFSET	0x050
#define AXI_VDMA_S2MM_ADDR_OFFSET	0x0A0
#define AXI_VDMA_VSIZE_OFFSET		0x000
#define AXI_VDMA_HSIZE_OFFSET		0x004
#define AXI_VDMA_STRD_FRMDLY_OFFSET	0x008
#define AXI_VDMA_START_ADDR_OFFSET	0x00C
#define AXI_VDMA_CR_RUNSTOP		BIT(0)
#define AXI_VDMA_CR_TAIL_EN		BIT(1)
#define AXI_VDMA_CR_RESET		BIT(2)
#define AXI_VDMA_CR_SYNC_EN		BIT(3)
#define AXI_VDMA_CR_FSYNC_TUSER	0x40
#define AXI_VDMA_CR_GENLOCK_INTERNAL	BIT(7)
#define AXI_VDMA_CR_GENLOCK_REPEAT	BIT(15)
#define AXI_VDMA_SR_HALTED		BIT(0)
#define AXI_VDMA_SR_IDLE		BIT(1)
#define AXI_VDMA_SR_ERR_ALL		0x00000FF0
#define AXI_VDMA_SR_IRQ_ALL		0x00007000

#define VTC_CTL			0x000
#define VTC_ISR			0x004
#define VTC_ERROR			0x008
#define VTC_VERSION			0x010
#define VTC_GASIZE			0x060
#define VTC_GTSTAT			0x064
#define VTC_GFENC			0x068
#define VTC_GPOL			0x06C
#define VTC_GHSIZE			0x070
#define VTC_GVSIZE			0x074
#define VTC_GHSYNC			0x078
#define VTC_GVBHOFF			0x07C
#define VTC_GVSYNC			0x080
#define VTC_GVSHOFF			0x084
#define VTC_GVBHOFF_F1		0x088
#define VTC_GVSYNC_F1			0x08C
#define VTC_GVSHOFF_F1		0x090
#define VTC_GASIZE_F1			0x094
#define VTC_CTL_ALLSS			0x03FDEF00
#define VTC_CTL_GE			BIT(2)
#define VTC_CTL_RU			BIT(1)
#define VTC_CTL_SW			BIT(0)
#define VTC_POL_VBLANK			BIT(0)
#define VTC_POL_HBLANK			BIT(1)
#define VTC_POL_VSYNC			BIT(2)
#define VTC_POL_HSYNC			BIT(3)
#define VTC_POL_ACTIVE_VIDEO		BIT(4)
#define VTC_POL_ACTIVE_CHROMA		BIT(5)
#define VTC_POL_FIELD_ID		BIT(6)
#define VTC_POL_BASE			(VTC_POL_VBLANK | VTC_POL_HBLANK | \
					 VTC_POL_ACTIVE_VIDEO | \
					 VTC_POL_ACTIVE_CHROMA | \
					 VTC_POL_FIELD_ID)

#define AXI_GPIO_DATA_CH1		0x00
#define AXI_GPIO_TRI_CH1		0x04
#define AXI_GPIO_DATA_CH2		0x08
#define AXI_GPIO_TRI_CH2		0x0C

#define CLK_WIZ_SR			0x004
#define CLK_WIZ_CFG0			0x200
#define CLK_WIZ_CFG1			0x204
#define CLK_WIZ_CFG2			0x208
#define CLK_WIZ_CFG3			0x20C
#define CLK_WIZ_CFG4			0x210
#define CLK_WIZ_CFG23			0x25C
#define CLK_WIZ_SR_LOCKED		BIT(0)
#define CLK_WIZ_CFG23_LOAD		BIT(0)
#define CLK_WIZ_CFG23_SADDR		BIT(1)
#define CLK_WIZ_DUTY_50		50000

#define AXI_IIC_RESETR			0x040
#define AXI_IIC_CR			0x100
#define AXI_IIC_SR			0x104
#define AXI_IIC_DTR			0x108
#define AXI_IIC_TFO			0x114
#define AXI_IIC_RESET			0x0000000A
#define AXI_IIC_CR_ENABLE		BIT(0)
#define AXI_IIC_CR_TX_FIFO_RESET	BIT(1)
#define AXI_IIC_SR_BUS_BUSY		BIT(2)
#define AXI_IIC_SR_TX_FIFO_FULL	BIT(4)
#define AXI_IIC_SR_TX_FIFO_EMPTY	BIT(7)
#define AXI_IIC_TX_DYN_START		0x00000100
#define AXI_IIC_TX_DYN_STOP		0x00000200

static bool connector_connected = true;
module_param(connector_connected, bool, 0644);
MODULE_PARM_DESC(connector_connected,
		 "Expose connector as connected. Default is true");

static bool connector_non_desktop = false;
module_param(connector_non_desktop, bool, 0644);
MODULE_PARM_DESC(connector_non_desktop,
		 "Mark connector as non-desktop. Default is false");

static bool enable_fbdev = false;
module_param(enable_fbdev, bool, 0644);
MODULE_PARM_DESC(enable_fbdev,
		 "Create fbdev client after DRM registration. Default is false.");

static unsigned int h2c_channel = 0;
module_param(h2c_channel, uint, 0644);
MODULE_PARM_DESC(h2c_channel, "XDMA H2C channel index. Default is 0.");

static unsigned long long stream_ep_addr;
module_param(stream_ep_addr, ullong, 0644);
MODULE_PARM_DESC(stream_ep_addr,
		 "XDMA endpoint address for stream submissions. Default is 0.");

static bool upload_full_frame = true;
module_param(upload_full_frame, bool, 0644);
MODULE_PARM_DESC(upload_full_frame,
		 "Upload full frame for every update. Default is true.");

static bool upload_enabled = true;
module_param(upload_enabled, bool, 0644);
MODULE_PARM_DESC(upload_enabled,
		 "Enable XDMA frame uploads after modeset. Default is true.");

static bool debug_logging;
module_param(debug_logging, bool, 0644);
MODULE_PARM_DESC(debug_logging,
		 "Enable extra connector, modeset, and upload logs. Default is false.");

static bool configure_pipeline = true;
module_param(configure_pipeline, bool, 0644);
MODULE_PARM_DESC(configure_pipeline,
		 "Configure FPGA video IPs through XDMA MMIO BAR during probe and modeset. Default is true.");

static bool enable_overlay;
module_param(enable_overlay, bool, 0644);
MODULE_PARM_DESC(enable_overlay,
		 "Expose one CPU-composited XRGB8888 overlay plane. Default is false.");

static char *composition_backend = "cpu";
module_param(composition_backend, charp, 0644);
MODULE_PARM_DESC(composition_backend,
		 "Composition backend: cpu. Value fpga is reserved for future hardware composition.");

unsigned int h2c_timeout = 10;
unsigned int c2h_timeout = 10;

struct fpga_video_mode;

struct fpga_plane_snapshot {
	bool enabled;
	struct drm_framebuffer *fb;
	struct drm_rect src;
	struct drm_rect dst;
	struct iosys_map map;
};

struct fpga_drm_device {
	struct drm_device drm;
	struct pci_dev *pdev;
	void *xdma;
	int user_max;
	int h2c_channel_max;
	int c2h_channel_max;
	void __iomem *mmio_bar;
	resource_size_t mmio_bar_len;
	int mmio_bar_idx;
	const char *mmio_bar_name;

	struct drm_crtc crtc;
	struct drm_plane primary_plane;
	struct drm_plane overlay_plane;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct work_struct upload_work;
	struct work_struct dma_complete_work;
	struct delayed_work dma_timeout_work;
	struct mutex upload_lock;
	struct mutex dma_lock;
	spinlock_t dma_state_lock;
	wait_queue_head_t dma_idle_wq;
	bool pipe_enabled;
	const struct fpga_video_mode *active_mode;
	struct fpga_plane_snapshot primary;
	struct fpga_plane_snapshot overlay;

	struct sg_table frame_sgt;
	struct sg_table active_frame_sgt;
	bool frame_sgt_ready;
	struct xdma_io_cb frame_cb;
	u8 *line_bufs[FPGA_DRM_MAX_HEIGHT];
	bool dma_inflight;
	bool dma_completion_pending;
	bool upload_pending;
	int dma_completion_err;
	u64 frames_queued;
	u64 frames_uploaded;
	u64 upload_failures;
	u64 atomic_commits;
	u64 atomic_rejects;
	u64 cpu_compositions;
};

static struct fpga_drm_device *to_fpga(struct drm_device *drm)
{
	return container_of(drm, struct fpga_drm_device, drm);
}

struct fpga_video_mode {
	struct drm_display_mode drm;
	u32 line_bytes;
	u32 frame_bytes;
	u32 frame_spacing;
	u32 clk_cfg0;
	u32 clk_cfg2;
	const char *name;
};

#define FPGA_CLK_CFG0(divclk, mult, frac_milli) \
	((divclk) | ((mult) << 8) | ((frac_milli) << 16))
#define FPGA_CLK_CFG2(div, frac_milli) \
	((div) | ((frac_milli) << 8))
#define FPGA_MODE(_name, _clock, _hd, _hss, _hse, _ht, _vd, _vss, _vse, _vt, \
		  _flags, _type, _cfg0, _cfg2) \
	{ \
		.drm = { \
			.clock = (_clock), \
			.hdisplay = (_hd), \
			.hsync_start = (_hss), \
			.hsync_end = (_hse), \
			.htotal = (_ht), \
			.vdisplay = (_vd), \
			.vsync_start = (_vss), \
			.vsync_end = (_vse), \
			.vtotal = (_vt), \
			.flags = (_flags), \
			.width_mm = 0, \
			.height_mm = 0, \
			.type = DRM_MODE_TYPE_DRIVER | (_type), \
		}, \
		.line_bytes = (_hd) * FPGA_DRM_BPP, \
		.frame_bytes = (_hd) * (_vd) * FPGA_DRM_BPP, \
		.frame_spacing = FPGA_HW_FRAME_SPACING, \
		.clk_cfg0 = (_cfg0), \
		.clk_cfg2 = (_cfg2), \
		.name = (_name), \
	}

static const struct fpga_video_mode fpga_video_modes[] = {
	FPGA_MODE("640x480@60", 25175,
		  640, 656, 752, 800, 480, 490, 492, 525,
		  DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, 0,
		  FPGA_CLK_CFG0(11, 36, 0), FPGA_CLK_CFG2(26, 0)),
	FPGA_MODE("640x480@30", 12587,
		  640, 656, 752, 800, 480, 490, 492, 525,
		  DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, 0,
		  FPGA_CLK_CFG0(11, 36, 0), FPGA_CLK_CFG2(52, 0)),
	FPGA_MODE("800x600@60", 40000,
		  800, 840, 968, 1056, 600, 601, 605, 628,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(1, 3, 0), FPGA_CLK_CFG2(15, 0)),
	FPGA_MODE("800x600@30", 20000,
		  800, 840, 968, 1056, 600, 601, 605, 628,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(1, 3, 0), FPGA_CLK_CFG2(30, 0)),
	FPGA_MODE("1024x768@60", 65000,
		  1024, 1048, 1184, 1344, 768, 771, 777, 806,
		  DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, 0,
		  FPGA_CLK_CFG0(4, 13, 0), FPGA_CLK_CFG2(10, 0)),
	FPGA_MODE("1024x768@30", 32500,
		  1024, 1048, 1184, 1344, 768, 771, 777, 806,
		  DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC, 0,
		  FPGA_CLK_CFG0(4, 13, 0), FPGA_CLK_CFG2(20, 0)),
	FPGA_MODE("1280x720@60", 74250,
		  1280, 1390, 1430, 1650, 720, 725, 730, 750,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
		  DRM_MODE_TYPE_PREFERRED,
		  FPGA_CLK_CFG0(10, 37, 125), FPGA_CLK_CFG2(10, 0)),
	FPGA_MODE("1280x720@30", 37125,
		  1280, 1390, 1430, 1650, 720, 725, 730, 750,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(10, 37, 125), FPGA_CLK_CFG2(20, 0)),
	FPGA_MODE("1280x1024@60", 108000,
		  1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(5, 27, 0), FPGA_CLK_CFG2(10, 0)),
	FPGA_MODE("1280x1024@30", 54000,
		  1280, 1328, 1440, 1688, 1024, 1025, 1028, 1066,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(5, 27, 0), FPGA_CLK_CFG2(20, 0)),
	FPGA_MODE("1920x1080@60", 148500,
		  1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(10, 37, 125), FPGA_CLK_CFG2(5, 0)),
	FPGA_MODE("1920x1080@30", 74250,
		  1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125,
		  DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC, 0,
		  FPGA_CLK_CFG0(10, 37, 125), FPGA_CLK_CFG2(10, 0)),
};

static const struct fpga_video_mode *fpga_drm_preferred_mode(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fpga_video_modes); i++) {
		if (fpga_video_modes[i].drm.type & DRM_MODE_TYPE_PREFERRED)
			return &fpga_video_modes[i];
	}

	return &fpga_video_modes[0];
}

static bool fpga_drm_modes_equal(const struct drm_display_mode *a,
				 const struct drm_display_mode *b)
{
	return a->clock == b->clock &&
	       a->hdisplay == b->hdisplay &&
	       a->hsync_start == b->hsync_start &&
	       a->hsync_end == b->hsync_end &&
	       a->htotal == b->htotal &&
	       a->vdisplay == b->vdisplay &&
	       a->vsync_start == b->vsync_start &&
	       a->vsync_end == b->vsync_end &&
	       a->vtotal == b->vtotal &&
	       (a->flags & DRM_MODE_FLAG_PHSYNC) ==
	       (b->flags & DRM_MODE_FLAG_PHSYNC) &&
	       (a->flags & DRM_MODE_FLAG_NHSYNC) ==
	       (b->flags & DRM_MODE_FLAG_NHSYNC) &&
	       (a->flags & DRM_MODE_FLAG_PVSYNC) ==
	       (b->flags & DRM_MODE_FLAG_PVSYNC) &&
	       (a->flags & DRM_MODE_FLAG_NVSYNC) ==
	       (b->flags & DRM_MODE_FLAG_NVSYNC);
}

static const struct fpga_video_mode *
fpga_drm_find_video_mode(const struct drm_display_mode *mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fpga_video_modes); i++) {
		if (fpga_drm_modes_equal(mode, &fpga_video_modes[i].drm))
			return &fpga_video_modes[i];
	}

	return NULL;
}

static const u64 fpga_hw_frame_addr[FPGA_HW_FRAME_COUNT] = {
	FPGA_HW_FRAME_BASE + 0 * FPGA_HW_FRAME_SPACING,
	FPGA_HW_FRAME_BASE + 1 * FPGA_HW_FRAME_SPACING,
	FPGA_HW_FRAME_BASE + 2 * FPGA_HW_FRAME_SPACING,
	FPGA_HW_FRAME_BASE + 3 * FPGA_HW_FRAME_SPACING,
};

struct fpga_i2c_lut_entry {
	u8 dev_addr8;
	u16 reg_addr;
	u8 reg_data;
};

static const struct fpga_i2c_lut_entry fpga_hdmi_lut[] = {
	{ 0x72, 0x0008, 0x35 },
	{ 0x7A, 0x002F, 0x00 },
	{ 0xFF, 0xFFFF, 0xFF },
};

static int fpga_drm_mmio_check(struct fpga_drm_device *fpga, u64 addr,
			       size_t size)
{
	if (!fpga->mmio_bar)
		return -ENODEV;

	if (addr & 3 || size != 4)
		return -EINVAL;

	if (addr > U64_MAX - size || addr + size > fpga->mmio_bar_len) {
		drm_err(&fpga->drm,
			"AXI-Lite address 0x%llx size=%zu outside %s BAR%d len=0x%llx\n",
			addr, size, fpga->mmio_bar_name, fpga->mmio_bar_idx,
			(unsigned long long)fpga->mmio_bar_len);
		return -ERANGE;
	}

	return 0;
}

static int fpga_drm_axi_write(struct fpga_drm_device *fpga, u64 addr, u32 val)
{
	int ret = fpga_drm_mmio_check(fpga, addr, sizeof(val));

	if (ret)
		return ret;

	iowrite32(val, fpga->mmio_bar + addr);
	return 0;
}

static int fpga_drm_axi_read(struct fpga_drm_device *fpga, u64 addr, u32 *val)
{
	int ret = fpga_drm_mmio_check(fpga, addr, sizeof(*val));

	if (ret)
		return ret;

	*val = ioread32(fpga->mmio_bar + addr);
	return 0;
}

static int fpga_drm_axi_update_bits(struct fpga_drm_device *fpga, u64 addr,
				    u32 mask, u32 val)
{
	u32 reg;
	int ret;

	ret = fpga_drm_axi_read(fpga, addr, &reg);
	if (ret)
		return ret;

	reg &= ~mask;
	reg |= val & mask;

	return fpga_drm_axi_write(fpga, addr, reg);
}

static int fpga_drm_require_range(struct fpga_drm_device *fpga,
				  const char *name, u64 base, u64 size)
{
	if (base > U64_MAX - size || base + size > fpga->mmio_bar_len) {
		drm_err(&fpga->drm,
			"%s AXI range 0x%llx-0x%llx outside %s BAR%d len=0x%llx\n",
			name, base, base + size - 1, fpga->mmio_bar_name,
			fpga->mmio_bar_idx,
			(unsigned long long)fpga->mmio_bar_len);
		return -ERANGE;
	}

	drm_info(&fpga->drm, "pipeline range %-18s 0x%08llx-0x%08llx\n",
		 name, base, base + size - 1);
	return 0;
}

static int fpga_drm_config_pixel_unpack(struct fpga_drm_device *fpga)
{
	int ret;
	u32 mode;

	ret = fpga_drm_axi_write(fpga,
				 FPGA_HW_PIXEL_UNPACK_BASE + HLS_PIXEL_UNPACK_MODE,
				 1);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga,
				FPGA_HW_PIXEL_UNPACK_BASE + HLS_PIXEL_UNPACK_MODE,
				&mode);
	if (ret)
		return ret;

	drm_info(&fpga->drm, "pixel unpack configured at 0x%08llx mode=%u\n",
		 FPGA_HW_PIXEL_UNPACK_BASE, mode);
	return 0;
}

static int fpga_drm_config_color_convert(struct fpga_drm_device *fpga)
{
	static const struct {
		u32 offset;
		u32 value;
	} coeffs[] = {
		{ HLS_COLOR_C1_C1, 256 }, { HLS_COLOR_C1_C2, 0 },
		{ HLS_COLOR_C1_C3, 0 },   { HLS_COLOR_C2_C1, 0 },
		{ HLS_COLOR_C2_C2, 256 }, { HLS_COLOR_C2_C3, 0 },
		{ HLS_COLOR_C3_C1, 0 },   { HLS_COLOR_C3_C2, 0 },
		{ HLS_COLOR_C3_C3, 256 }, { HLS_COLOR_BIAS_C1, 0 },
		{ HLS_COLOR_BIAS_C2, 0 }, { HLS_COLOR_BIAS_C3, 0 },
	};
	u32 c11, c22, c33;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(coeffs); i++) {
		ret = fpga_drm_axi_write(fpga,
					 FPGA_HW_COLOR_CONVERT_BASE + coeffs[i].offset,
					 coeffs[i].value);
		if (ret)
			return ret;
	}

	ret = fpga_drm_axi_read(fpga, FPGA_HW_COLOR_CONVERT_BASE + HLS_COLOR_C1_C1,
				&c11);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_COLOR_CONVERT_BASE + HLS_COLOR_C2_C2,
				&c22);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_COLOR_CONVERT_BASE + HLS_COLOR_C3_C3,
				&c33);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "color convert configured at 0x%08llx identity diag=%u,%u,%u\n",
		 FPGA_HW_COLOR_CONVERT_BASE, c11, c22, c33);
	return 0;
}

static int fpga_drm_reset_vdma_channel(struct fpga_drm_device *fpga, u64 chan)
{
	u32 cr;
	int ret;
	int i;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + chan +
				 AXI_VDMA_CR_OFFSET, AXI_VDMA_CR_RESET);
	if (ret)
		return ret;

	for (i = 0; i < 1000; i++) {
		ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + chan +
					AXI_VDMA_CR_OFFSET, &cr);
		if (ret)
			return ret;
		if (!(cr & AXI_VDMA_CR_RESET))
			return 0;
		udelay(10);
	}

	return -ETIMEDOUT;
}

static int fpga_drm_program_vdma_channel(struct fpga_drm_device *fpga,
					 const struct fpga_video_mode *mode,
					 const char *name, u64 chan,
					 u64 addr_base, u32 cr, u32 frame_delay)
{
	u32 status;
	unsigned int i;
	int ret;

	ret = fpga_drm_reset_vdma_channel(fpga, chan);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + chan +
				 AXI_VDMA_SR_OFFSET,
				 AXI_VDMA_SR_ERR_ALL | AXI_VDMA_SR_IRQ_ALL);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + chan +
				 AXI_VDMA_FRMSTORE_OFFSET,
				 FPGA_HW_FRAME_COUNT);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + chan +
				 AXI_VDMA_CR_OFFSET, cr);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + addr_base +
				 AXI_VDMA_HSIZE_OFFSET,
				 mode->line_bytes);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + addr_base +
				 AXI_VDMA_STRD_FRMDLY_OFFSET,
				 (frame_delay << 24) | mode->line_bytes);
	if (ret)
		return ret;

	for (i = 0; i < FPGA_HW_FRAME_COUNT; i++) {
		ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + addr_base +
					 AXI_VDMA_START_ADDR_OFFSET + i * 4,
					 lower_32_bits(fpga_hw_frame_addr[i]));
		if (ret)
			return ret;
	}

	ret = fpga_drm_axi_update_bits(fpga, FPGA_HW_VDMA_BASE + chan +
				       AXI_VDMA_CR_OFFSET,
				       AXI_VDMA_CR_RUNSTOP, AXI_VDMA_CR_RUNSTOP);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + addr_base +
				 AXI_VDMA_VSIZE_OFFSET, mode->drm.vdisplay);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + chan +
				AXI_VDMA_SR_OFFSET, &status);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "%s configured at 0x%08llx %ux%u line=%u CR=0x%08x SR=0x%08x frames=%u first=0x%08llx\n",
		 name, FPGA_HW_VDMA_BASE + chan,
		 mode->drm.hdisplay, mode->drm.vdisplay, mode->line_bytes,
		 (u32)(cr | AXI_VDMA_CR_RUNSTOP),
		 status, FPGA_HW_FRAME_COUNT, fpga_hw_frame_addr[0]);
	return 0;
}

static int fpga_drm_config_vdma(struct fpga_drm_device *fpga,
				const struct fpga_video_mode *mode)
{
	u32 parkptr;
	int ret;

	ret = fpga_drm_program_vdma_channel(fpga, mode, "VDMA S2MM",
					    AXI_VDMA_RX_OFFSET,
					    AXI_VDMA_S2MM_ADDR_OFFSET,
					    AXI_VDMA_CR_TAIL_EN |
					    AXI_VDMA_CR_SYNC_EN |
					    AXI_VDMA_CR_FSYNC_TUSER |
					    AXI_VDMA_CR_GENLOCK_INTERNAL |
					    AXI_VDMA_CR_GENLOCK_REPEAT,
					    0);
	if (ret)
		return ret;

	ret = fpga_drm_program_vdma_channel(fpga, mode, "VDMA MM2S",
					    AXI_VDMA_TX_OFFSET,
					    AXI_VDMA_MM2S_ADDR_OFFSET,
					    AXI_VDMA_CR_TAIL_EN |
					    AXI_VDMA_CR_SYNC_EN |
					    AXI_VDMA_CR_GENLOCK_INTERNAL,
					    1);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + AXI_VDMA_PARKPTR_OFFSET,
				&parkptr);
	if (ret)
		return ret;

	drm_info(&fpga->drm, "VDMA park pointer readback=0x%08x\n", parkptr);
	return 0;
}

static u32 fpga_drm_vtc_pack(u32 start, u32 end)
{
	return (start & 0x3fff) | ((end & 0x3fff) << 16);
}

static int fpga_drm_config_vtc(struct fpga_drm_device *fpga,
			       const struct fpga_video_mode *mode)
{
	const struct drm_display_mode *m = &mode->drm;
	const u32 htotal = m->htotal;
	const u32 vtotal = m->vtotal;
	const u32 hactive = m->hdisplay;
	const u32 vactive = m->vdisplay;
	const u32 hsync_start = m->hsync_start;
	const u32 hsync_end = m->hsync_end;
	const u32 vsync_start = m->vsync_start - 1;
	const u32 vsync_end = m->vsync_end - 1;
	u32 polarity = VTC_POL_BASE;
	u32 ctl, gtstat;
	int ret;

	if (m->flags & DRM_MODE_FLAG_PHSYNC)
		polarity |= VTC_POL_HSYNC;
	if (m->flags & DRM_MODE_FLAG_PVSYNC)
		polarity |= VTC_POL_VSYNC;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GHSIZE, htotal);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVSIZE,
				 vtotal | (vtotal << 16));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GASIZE,
				 hactive | (vactive << 16));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GASIZE_F1,
				 vactive << 16);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GHSYNC,
				 fpga_drm_vtc_pack(hsync_start, hsync_end));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVSYNC,
				 fpga_drm_vtc_pack(vsync_start, vsync_end));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVSYNC_F1,
				 fpga_drm_vtc_pack(vsync_start, vsync_end));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVBHOFF,
				 fpga_drm_vtc_pack(hactive, hactive));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVBHOFF_F1,
				 fpga_drm_vtc_pack(hactive, hactive));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVSHOFF,
				 fpga_drm_vtc_pack(hsync_start, hsync_start));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GVSHOFF_F1,
				 fpga_drm_vtc_pack(hsync_start, hsync_start));
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GFENC, 0);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GPOL, polarity);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VTC_BASE + VTC_CTL, &ctl);
	if (ret)
		return ret;
	ctl &= ~VTC_CTL_ALLSS;
	ctl |= VTC_CTL_SW | VTC_CTL_GE | VTC_CTL_RU;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_CTL, ctl);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VTC_BASE + VTC_GTSTAT, &gtstat);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "VTC configured at 0x%08llx %s h=%u/%u-%u/%u v=%u/%u-%u/%u pol=0x%02x CTL=0x%08x GTSTAT=0x%08x\n",
		 FPGA_HW_VTC_BASE, mode->name, hactive, hsync_start,
		 hsync_end, htotal, vactive, vsync_start + 1,
		 vsync_end + 1, vtotal, polarity, ctl, gtstat);
	return 0;
}

static int fpga_drm_iic_wait(struct fpga_drm_device *fpga, u32 clear,
			     u32 set, unsigned int timeout_us)
{
	u32 sr;
	unsigned int waited = 0;
	int ret;

	do {
		ret = fpga_drm_axi_read(fpga, FPGA_HW_AXI_IIC_BASE + AXI_IIC_SR,
					&sr);
		if (ret)
			return ret;
		if (!(sr & clear) && ((sr & set) == set))
			return 0;
		udelay(10);
		waited += 10;
	} while (waited < timeout_us);

	return -ETIMEDOUT;
}

static int fpga_drm_iic_write_dtr(struct fpga_drm_device *fpga, u32 val)
{
	int ret;

	ret = fpga_drm_iic_wait(fpga, AXI_IIC_SR_TX_FIFO_FULL, 0, 10000);
	if (ret)
		return ret;

	return fpga_drm_axi_write(fpga, FPGA_HW_AXI_IIC_BASE + AXI_IIC_DTR, val);
}

static int fpga_drm_iic_write_reg(struct fpga_drm_device *fpga,
				  u8 dev_addr8, u16 reg, u8 data)
{
	u8 dev7 = dev_addr8 >> 1;
	int ret;

	ret = fpga_drm_iic_wait(fpga, AXI_IIC_SR_BUS_BUSY, 0, 100000);
	if (ret)
		return ret;

	ret = fpga_drm_iic_write_dtr(fpga, AXI_IIC_TX_DYN_START | (dev7 << 1));
	if (ret)
		return ret;
	ret = fpga_drm_iic_write_dtr(fpga, reg & 0xff);
	if (ret)
		return ret;
	ret = fpga_drm_iic_write_dtr(fpga, AXI_IIC_TX_DYN_STOP | data);
	if (ret)
		return ret;

	return fpga_drm_iic_wait(fpga, AXI_IIC_SR_BUS_BUSY,
				 AXI_IIC_SR_TX_FIFO_EMPTY, 100000);
}

static int fpga_drm_config_iic_hdmi(struct fpga_drm_device *fpga)
{
	unsigned int i;
	int ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_AXI_IIC_BASE + AXI_IIC_RESETR,
				 AXI_IIC_RESET);
	if (ret)
		return ret;
	udelay(10);
	ret = fpga_drm_axi_write(fpga, FPGA_HW_AXI_IIC_BASE + AXI_IIC_CR,
				 AXI_IIC_CR_ENABLE | AXI_IIC_CR_TX_FIFO_RESET);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_AXI_IIC_BASE + AXI_IIC_CR,
				 AXI_IIC_CR_ENABLE);
	if (ret)
		return ret;

	for (i = 0; fpga_hdmi_lut[i].dev_addr8 != 0xff; i++) {
		ret = fpga_drm_iic_write_reg(fpga, fpga_hdmi_lut[i].dev_addr8,
					     fpga_hdmi_lut[i].reg_addr,
					     fpga_hdmi_lut[i].reg_data);
		if (ret)
			return ret;
		usleep_range(1000, 2000);
	}

	drm_info(&fpga->drm, "HDMI I2C LUT programmed at AXI IIC 0x%08llx entries=%u\n",
		 FPGA_HW_AXI_IIC_BASE, i);
	return 0;
}

static int fpga_drm_config_video_lock_gpio(struct fpga_drm_device *fpga)
{
	u32 ch1, ch2;
	int ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_LOCK_GPIO_BASE +
				 AXI_GPIO_TRI_CH1, 0x0f);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_LOCK_GPIO_BASE +
				 AXI_GPIO_TRI_CH2, 0xffffffff);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_LOCK_GPIO_BASE +
				AXI_GPIO_DATA_CH1, &ch1);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_LOCK_GPIO_BASE +
				AXI_GPIO_DATA_CH2, &ch2);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "video lock GPIO at 0x%08llx ch1=0x%01x ch2=0x%08x\n",
		 FPGA_HW_VIDEO_LOCK_GPIO_BASE, ch1 & 0xf, ch2);
	return 0;
}

static int fpga_drm_wait_clk_wiz_locked(struct fpga_drm_device *fpga)
{
	u32 status, cfg23;
	int ret;
	int i;

	for (i = 0; i < 1000; i++) {
		ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
					CLK_WIZ_SR, &status);
		if (ret)
			return ret;
		ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
					CLK_WIZ_CFG23, &cfg23);
		if (ret)
			return ret;

		if ((status & CLK_WIZ_SR_LOCKED) && !(cfg23 & CLK_WIZ_CFG23_LOAD))
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int fpga_drm_config_clk_wiz(struct fpga_drm_device *fpga,
				   const struct fpga_video_mode *mode)
{
	u32 status;
	int ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE + CLK_WIZ_SR,
				&status);
	if (ret)
		return ret;
	if (!(status & CLK_WIZ_SR_LOCKED))
		drm_warn(&fpga->drm,
			 "video clock wizard was unlocked before reconfiguring %s: SR=0x%08x\n",
			 mode->name, status);

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG0, mode->clk_cfg0);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG1, 0);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG2, mode->clk_cfg2);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG3, 0);
	if (ret)
		return ret;
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG4, CLK_WIZ_DUTY_50);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE +
				 CLK_WIZ_CFG23,
				 CLK_WIZ_CFG23_LOAD | CLK_WIZ_CFG23_SADDR);
	if (ret)
		return ret;

	ret = fpga_drm_wait_clk_wiz_locked(fpga);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE + CLK_WIZ_SR,
				&status);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "video clock configured for %s pixel_clock=%u kHz CFG0=0x%08x CFG2=0x%08x SR=0x%08x\n",
		 mode->name, mode->drm.clock, mode->clk_cfg0, mode->clk_cfg2,
		 status);
	return 0;
}

static int fpga_drm_configure_static_pipeline(struct fpga_drm_device *fpga)
{
	int ret;

	if (!configure_pipeline) {
		drm_info(&fpga->drm, "FPGA video pipeline configuration disabled\n");
		return 0;
	}

	ret = fpga_drm_require_range(fpga, "color_convert",
				     FPGA_HW_COLOR_CONVERT_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "pixel_unpack",
				     FPGA_HW_PIXEL_UNPACK_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "video_lock_gpio",
				     FPGA_HW_VIDEO_LOCK_GPIO_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "axi_iic",
				     FPGA_HW_AXI_IIC_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "axi_vdma_0",
				     FPGA_HW_VDMA_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "hdmi_out_v_tc_0",
				     FPGA_HW_VTC_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	ret = fpga_drm_require_range(fpga, "video_clk_wiz",
				     FPGA_HW_VIDEO_CLK_WIZ_BASE, FPGA_HW_REG_WINDOW);
	if (ret)
		return ret;
	drm_info(&fpga->drm,
		 "VDMA DDR frame ring 0x%08llx-0x%08llx count=%u stride=0x%x\n",
		 FPGA_HW_FRAME_BASE,
		 FPGA_HW_FRAME_BASE +
		 (FPGA_HW_FRAME_COUNT - 1) * FPGA_HW_FRAME_SPACING +
		 FPGA_DRM_MAX_FRAME_BYTES - 1,
		 FPGA_HW_FRAME_COUNT, FPGA_HW_FRAME_SPACING);

	ret = fpga_drm_config_video_lock_gpio(fpga);
	if (ret)
		return ret;
	ret = fpga_drm_config_pixel_unpack(fpga);
	if (ret)
		return ret;
	ret = fpga_drm_config_color_convert(fpga);
	if (ret)
		return ret;
	ret = fpga_drm_config_iic_hdmi(fpga);
	if (ret)
		return ret;

	return 0;
}

static int fpga_drm_program_mode(struct fpga_drm_device *fpga,
				 const struct fpga_video_mode *mode)
{
	u32 vdma_s2mm_sr, vdma_mm2s_sr, vtc_isr, vtc_err, clk_wiz_status;
	int ret;

	if (!configure_pipeline) {
		drm_info(&fpga->drm,
			 "FPGA video mode programming disabled, accepting %s\n",
			 mode->name);
		return 0;
	}

	ret = fpga_drm_config_clk_wiz(fpga, mode);
	if (ret)
		return ret;
	ret = fpga_drm_config_vdma(fpga, mode);
	if (ret)
		return ret;
	ret = fpga_drm_config_vtc(fpga, mode);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + AXI_VDMA_RX_OFFSET +
				AXI_VDMA_SR_OFFSET, &vdma_s2mm_sr);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + AXI_VDMA_TX_OFFSET +
				AXI_VDMA_SR_OFFSET, &vdma_mm2s_sr);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VTC_BASE + VTC_ISR, &vtc_isr);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VTC_BASE + VTC_ERROR, &vtc_err);
	if (ret)
		return ret;
	ret = fpga_drm_axi_read(fpga, FPGA_HW_VIDEO_CLK_WIZ_BASE + 0x04,
				&clk_wiz_status);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "mode %s readback: VDMA S2MM_SR=0x%08x MM2S_SR=0x%08x VTC_ISR=0x%08x VTC_ERR=0x%08x CLK_WIZ_STATUS=0x%08x\n",
		 mode->name, vdma_s2mm_sr, vdma_mm2s_sr, vtc_isr, vtc_err,
		 clk_wiz_status);
	return 0;
}

static void fpga_drm_queue_dma_completion(struct fpga_drm_device *fpga, int err)
{
	unsigned long flags;
	bool queue = false;

	spin_lock_irqsave(&fpga->dma_state_lock, flags);
	if (fpga->dma_inflight && !fpga->dma_completion_pending) {
		fpga->dma_completion_pending = true;
		fpga->dma_completion_err = err;
		queue = true;
	}
	spin_unlock_irqrestore(&fpga->dma_state_lock, flags);

	if (queue)
		schedule_work(&fpga->dma_complete_work);
}

static void fpga_drm_xdma_done(unsigned long cb_hndl, int err)
{
	struct xdma_io_cb *cb = (struct xdma_io_cb *)cb_hndl;
	struct fpga_drm_device *fpga = cb->private;

	fpga_drm_queue_dma_completion(fpga, err);
}

static void fpga_drm_dma_timeout_work(struct work_struct *work)
{
	struct fpga_drm_device *fpga =
		container_of(to_delayed_work(work), struct fpga_drm_device,
			     dma_timeout_work);

	fpga_drm_queue_dma_completion(fpga, -ETIMEDOUT);
}

static bool fpga_drm_dma_busy(struct fpga_drm_device *fpga)
{
	unsigned long flags;
	bool busy;

	spin_lock_irqsave(&fpga->dma_state_lock, flags);
	busy = fpga->dma_inflight || fpga->dma_completion_pending;
	if (busy)
		fpga->upload_pending = true;
	spin_unlock_irqrestore(&fpga->dma_state_lock, flags);

	return busy;
}

static bool fpga_drm_dma_idle(struct fpga_drm_device *fpga)
{
	unsigned long flags;
	bool idle;

	spin_lock_irqsave(&fpga->dma_state_lock, flags);
	idle = !fpga->dma_inflight && !fpga->dma_completion_pending;
	spin_unlock_irqrestore(&fpga->dma_state_lock, flags);

	return idle;
}

static void fpga_drm_dma_finish(struct fpga_drm_device *fpga,
				int completion_err, ssize_t completion_len)
{
	const struct fpga_video_mode *mode = fpga->active_mode;
	unsigned long flags;
	bool upload_pending;
	int ret = completion_err;

	if (!mode)
		mode = fpga_drm_preferred_mode();

	if (!ret && completion_len != mode->frame_bytes)
		ret = -EIO;

	spin_lock_irqsave(&fpga->dma_state_lock, flags);
	fpga->dma_inflight = false;
	fpga->dma_completion_pending = false;
	fpga->dma_completion_err = 0;
	upload_pending = fpga->upload_pending;
	fpga->upload_pending = false;
	spin_unlock_irqrestore(&fpga->dma_state_lock, flags);

	wake_up_all(&fpga->dma_idle_wq);

	if (ret) {
		fpga->upload_failures++;
		drm_err_ratelimited(&fpga->drm,
				    "async frame upload failed: err=%d len=%zd\n",
				    ret, completion_len);
	} else {
		fpga->frames_uploaded++;
		if (debug_logging)
			drm_info(&fpga->drm,
				 "async frame upload complete mode=%s count=%llu\n",
				 mode->name, fpga->frames_uploaded);
	}

	if (upload_pending)
		schedule_work(&fpga->upload_work);
}

static void fpga_drm_dma_complete_work(struct work_struct *work)
{
	struct fpga_drm_device *fpga =
		container_of(work, struct fpga_drm_device, dma_complete_work);
	ssize_t ret = 0;
	int err;
	bool pending;

	mutex_lock(&fpga->dma_lock);

	spin_lock_irq(&fpga->dma_state_lock);
	pending = fpga->dma_completion_pending;
	err = fpga->dma_completion_err;
	spin_unlock_irq(&fpga->dma_state_lock);

	if (!pending) {
		mutex_unlock(&fpga->dma_lock);
		return;
	}

	cancel_delayed_work(&fpga->dma_timeout_work);

	if (fpga->frame_cb.req) {
		ret = xdma_xfer_completion(&fpga->frame_cb, fpga->xdma,
					   h2c_channel, true, stream_ep_addr,
					   &fpga->active_frame_sgt, false,
					   FPGA_DRM_TIMEOUT_MS);
		fpga->frame_cb.req = NULL;
	}

	fpga_drm_dma_finish(fpga, err, ret);

	mutex_unlock(&fpga->dma_lock);
}

static void fpga_drm_put_snapshot(struct fpga_plane_snapshot *snap)
{
	if (snap->fb)
		drm_framebuffer_put(snap->fb);
	memset(snap, 0, sizeof(*snap));
	iosys_map_clear(&snap->map);
}

static void fpga_drm_replace_snapshot(struct fpga_plane_snapshot *snap,
				      struct drm_plane_state *state)
{
	struct drm_shadow_plane_state *shadow_state;

	fpga_drm_put_snapshot(snap);

	if (!state || !state->fb || !state->crtc)
		return;

	shadow_state = to_drm_shadow_plane_state(state);

	drm_framebuffer_get(state->fb);
	snap->enabled = true;
	snap->fb = state->fb;
	snap->map = shadow_state->data[0];
	snap->src.x1 = state->src_x >> 16;
	snap->src.y1 = state->src_y >> 16;
	snap->src.x2 = snap->src.x1 + (state->src_w >> 16);
	snap->src.y2 = snap->src.y1 + (state->src_h >> 16);
	snap->dst.x1 = state->crtc_x;
	snap->dst.y1 = state->crtc_y;
	snap->dst.x2 = state->crtc_x + state->crtc_w;
	snap->dst.y2 = state->crtc_y + state->crtc_h;
}

static bool fpga_drm_get_snapshot(struct fpga_plane_snapshot *dst,
				  const struct fpga_plane_snapshot *src)
{
	*dst = *src;
	if (!dst->enabled || !dst->fb)
		return false;

	drm_framebuffer_get(dst->fb);
	return true;
}

static int fpga_drm_copy_primary_frame(struct fpga_drm_device *fpga,
				       const struct fpga_plane_snapshot *primary)
{
	const struct fpga_video_mode *mode = fpga->active_mode;
	struct drm_framebuffer *fb = primary->fb;
	u8 *src = primary->map.vaddr;
	unsigned int y;

	if (!src)
		return -EINVAL;

	if (!mode)
		mode = fpga_drm_preferred_mode();

	if (!primary->enabled || !fb ||
	    fb->format->format != DRM_FORMAT_XRGB8888 ||
	    fb->width != mode->drm.hdisplay || fb->height != mode->drm.vdisplay)
		return -EINVAL;

	if (fb->pitches[0] < mode->line_bytes)
		return -EINVAL;

	for (y = 0; y < mode->drm.vdisplay; y++)
		memcpy(fpga->line_bufs[y], src + y * fb->pitches[0],
		       mode->line_bytes);

	return 0;
}

static int fpga_drm_blend_overlay_cpu(struct fpga_drm_device *fpga,
				      const struct fpga_plane_snapshot *overlay)
{
	struct drm_framebuffer *fb = overlay->fb;
	const struct drm_rect *src = &overlay->src;
	const struct drm_rect *dst = &overlay->dst;
	u8 *base = overlay->map.vaddr;
	unsigned int width = drm_rect_width(dst);
	unsigned int height = drm_rect_height(dst);
	unsigned int x, y;

	if (!overlay->enabled)
		return 0;

	if (!base || !fb || fb->format->format != DRM_FORMAT_XRGB8888)
		return -EINVAL;

	if (src->x1 < 0 || src->y1 < 0 || dst->x1 < 0 || dst->y1 < 0)
		return -EINVAL;

	if (src->x2 > fb->width || src->y2 > fb->height ||
	    dst->x2 > fpga->active_mode->drm.hdisplay ||
	    dst->y2 > fpga->active_mode->drm.vdisplay)
		return -EINVAL;

	if (drm_rect_width(src) != width || drm_rect_height(src) != height)
		return -EINVAL;

	if (fb->pitches[0] < fb->width * FPGA_DRM_BPP)
		return -EINVAL;

	for (y = 0; y < height; y++) {
		u8 *src_line = base + (src->y1 + y) * fb->pitches[0] +
			       src->x1 * FPGA_DRM_BPP;
		u8 *dst_line = fpga->line_bufs[dst->y1 + y] +
			       dst->x1 * FPGA_DRM_BPP;

		for (x = 0; x < width; x++)
			((u32 *)dst_line)[x] = ((u32 *)src_line)[x];
	}

	return 0;
}

static int fpga_drm_compose_frame(struct fpga_drm_device *fpga,
				  const struct fpga_plane_snapshot *primary,
				  const struct fpga_plane_snapshot *overlay)
{
	int ret;

	ret = fpga_drm_copy_primary_frame(fpga, primary);
	if (ret)
		return ret;

	if (overlay && overlay->enabled) {
		ret = fpga_drm_blend_overlay_cpu(fpga, overlay);
		if (ret) {
			if (debug_logging)
				drm_info(&fpga->drm,
					 "cpu overlay composition failed ret=%d fb=%u src=%dx%d-%dx%d dst=%dx%d-%dx%d\n",
					 ret, overlay->fb ? overlay->fb->base.id : 0,
					 overlay->src.x1, overlay->src.y1,
					 overlay->src.x2, overlay->src.y2,
					 overlay->dst.x1, overlay->dst.y1,
					 overlay->dst.x2, overlay->dst.y2);
			return ret;
		}
		fpga->cpu_compositions++;
	}

	return 0;
}

static void fpga_drm_prepare_active_sgt(struct fpga_drm_device *fpga,
					const struct fpga_video_mode *mode)
{
	struct scatterlist *sg;
	unsigned int y;

	for_each_sg(fpga->frame_sgt.sgl, sg, mode->drm.vdisplay, y)
		sg_set_buf(sg, fpga->line_bufs[y], mode->line_bytes);

	fpga->active_frame_sgt.sgl = fpga->frame_sgt.sgl;
	fpga->active_frame_sgt.orig_nents = mode->drm.vdisplay;
	fpga->active_frame_sgt.nents = 0;
}

static int fpga_drm_submit_frame_nowait(struct fpga_drm_device *fpga)
{
	const struct fpga_video_mode *mode = fpga->active_mode;
	unsigned long flags;
	ssize_t ret;

	if (!mode)
		mode = fpga_drm_preferred_mode();

	if (debug_logging)
		drm_info(&fpga->drm,
			 "upload composed frame mode=%s overlay=%u\n",
			 mode->name, fpga->overlay.enabled);

	fpga_drm_prepare_active_sgt(fpga, mode);

	memset(&fpga->frame_cb, 0, sizeof(fpga->frame_cb));
	fpga->frame_cb.private = fpga;
	fpga->frame_cb.ep_addr = stream_ep_addr;
	fpga->frame_cb.write = true;
	fpga->frame_cb.io_done = fpga_drm_xdma_done;

	spin_lock_irqsave(&fpga->dma_state_lock, flags);
	fpga->dma_inflight = true;
	fpga->dma_completion_pending = false;
	fpga->dma_completion_err = 0;
	fpga->upload_pending = false;
	spin_unlock_irqrestore(&fpga->dma_state_lock, flags);

	ret = xdma_xfer_submit_lines_nowait(&fpga->frame_cb, fpga->xdma,
					    h2c_channel, true, stream_ep_addr,
					    &fpga->active_frame_sgt, false,
					    mode->line_bytes,
					    mode->drm.vdisplay);
	if (ret != -EIOCBQUEUED) {
		spin_lock_irqsave(&fpga->dma_state_lock, flags);
		fpga->dma_inflight = false;
		fpga->dma_completion_pending = false;
		fpga->dma_completion_err = 0;
		spin_unlock_irqrestore(&fpga->dma_state_lock, flags);
		wake_up_all(&fpga->dma_idle_wq);
		return ret < 0 ? ret : -EIO;
	}

	schedule_delayed_work(&fpga->dma_timeout_work,
			      msecs_to_jiffies(FPGA_DRM_TIMEOUT_MS));
	return 0;
}

static void fpga_drm_upload_work(struct work_struct *work)
{
	struct fpga_drm_device *fpga =
		container_of(work, struct fpga_drm_device, upload_work);
	struct fpga_plane_snapshot primary;
	struct fpga_plane_snapshot overlay;
	bool have_primary;
	bool have_overlay;
	int ret;

	memset(&primary, 0, sizeof(primary));
	memset(&overlay, 0, sizeof(overlay));

	mutex_lock(&fpga->upload_lock);
	have_primary = fpga->pipe_enabled &&
		       fpga_drm_get_snapshot(&primary, &fpga->primary);
	have_overlay = fpga_drm_get_snapshot(&overlay, &fpga->overlay);
	mutex_unlock(&fpga->upload_lock);

	if (!have_primary)
		return;

	if (!upload_enabled) {
		if (debug_logging)
			drm_info(&fpga->drm, "upload skipped because upload_enabled=0\n");
		fpga_drm_put_snapshot(&primary);
		if (have_overlay)
			fpga_drm_put_snapshot(&overlay);
		return;
	}

	mutex_lock(&fpga->dma_lock);
	if (fpga_drm_dma_busy(fpga)) {
		mutex_unlock(&fpga->dma_lock);
		fpga_drm_put_snapshot(&primary);
		if (have_overlay)
			fpga_drm_put_snapshot(&overlay);
		return;
	}

	ret = fpga_drm_compose_frame(fpga, &primary,
				     have_overlay ? &overlay : NULL);
	if (!ret)
		ret = fpga_drm_submit_frame_nowait(fpga);
	mutex_unlock(&fpga->dma_lock);

	if (ret) {
		fpga->upload_failures++;
		drm_err_ratelimited(&fpga->drm,
				    "async frame submit failed: %d\n", ret);
	}

	fpga_drm_put_snapshot(&primary);
	if (have_overlay)
		fpga_drm_put_snapshot(&overlay);
}

static void fpga_drm_queue_commit_upload(struct fpga_drm_device *fpga)
{
	fpga->frames_queued++;
	if (debug_logging)
		drm_info(&fpga->drm,
			 "queue upload count=%llu full=%u enabled=%u overlay=%u\n",
			 fpga->frames_queued, upload_full_frame, upload_enabled,
			 fpga->overlay.enabled);

	schedule_work(&fpga->upload_work);
}

static void fpga_drm_stop_uploads(struct fpga_drm_device *fpga)
{
	struct drm_framebuffer *old_fb;

	cancel_work_sync(&fpga->upload_work);
	if (!wait_event_timeout(fpga->dma_idle_wq, fpga_drm_dma_idle(fpga),
				msecs_to_jiffies(FPGA_DRM_TIMEOUT_MS + 1000))) {
		fpga_drm_queue_dma_completion(fpga, -ETIMEDOUT);
		flush_work(&fpga->dma_complete_work);
	}
	cancel_delayed_work_sync(&fpga->dma_timeout_work);
	flush_work(&fpga->dma_complete_work);

	mutex_lock(&fpga->upload_lock);
	old_fb = fpga->primary.fb;
	memset(&fpga->primary, 0, sizeof(fpga->primary));
	iosys_map_clear(&fpga->primary.map);
	fpga_drm_put_snapshot(&fpga->overlay);
	mutex_unlock(&fpga->upload_lock);

	if (old_fb)
		drm_framebuffer_put(old_fb);
}

static enum drm_mode_status
fpga_drm_crtc_mode_valid(struct drm_crtc *crtc,
			 const struct drm_display_mode *mode)
{
	if (mode->clock > FPGA_DRM_MAX_PIXEL_CLOCK_KHZ)
		return MODE_CLOCK_HIGH;

	if (fpga_drm_find_video_mode(mode))
		return MODE_OK;

	return MODE_BAD;
}

static int
fpga_drm_reject_atomic_state(struct fpga_drm_device *fpga,
			     const char *stage,
			     const struct drm_plane_state *plane_state,
			     const struct drm_crtc_state *crtc_state,
			     int ret, const char *reason)
{
	fpga->atomic_rejects++;

	if (debug_logging && plane_state) {
		struct drm_framebuffer *fb = plane_state->fb;
		struct drm_plane *plane = plane_state->plane;
		const char *mode_name = crtc_state ? crtc_state->mode.name : "<none>";

		if (fb) {
			u32 fmt = fb->format->format;

			drm_info(&fpga->drm,
				 "atomic reject stage=%s plane=%u type=%u reason=%s ret=%d fb=%u fmt=%p4cc modifier=0x%llx fb=%ux%u pitch=%u crtc=%u mode=%s src=%ux%u dst=%dx%d+%ux%u\n",
				 stage, plane ? plane->base.id : 0,
				 plane ? plane->type : 0, reason, ret,
				 fb->base.id, &fmt, fb->modifier, fb->width,
				 fb->height, fb->pitches[0],
				 plane_state->crtc ? plane_state->crtc->base.id : 0,
				 mode_name, plane_state->src_w >> 16,
				 plane_state->src_h >> 16, plane_state->crtc_x,
				 plane_state->crtc_y, plane_state->crtc_w,
				 plane_state->crtc_h);
		} else {
			drm_info(&fpga->drm,
				 "atomic reject stage=%s plane=%u type=%u reason=%s ret=%d no-fb crtc=%u mode=%s dst=%dx%d+%ux%u\n",
				 stage, plane ? plane->base.id : 0,
				 plane ? plane->type : 0, reason, ret,
				 plane_state->crtc ? plane_state->crtc->base.id : 0,
				 mode_name, plane_state->crtc_x,
				 plane_state->crtc_y, plane_state->crtc_w,
				 plane_state->crtc_h);
		}
	} else if (debug_logging) {
		drm_info(&fpga->drm,
			 "atomic reject stage=%s reason=%s ret=%d no-plane-state\n",
			 stage, reason, ret);
	}

	return ret;
}

static int fpga_drm_check_xrgb8888_fb(struct fpga_drm_device *fpga,
				      const struct drm_plane_state *plane_state,
				      const struct drm_crtc_state *crtc_state,
				      const char *stage)
{
	if (!plane_state->fb)
		return 0;

	if (plane_state->fb->format->format != DRM_FORMAT_XRGB8888)
		return fpga_drm_reject_atomic_state(fpga, stage, plane_state,
						    crtc_state, -EINVAL,
						    "unsupported-format");

	if (plane_state->fb->modifier != DRM_FORMAT_MOD_LINEAR &&
	    plane_state->fb->modifier != DRM_FORMAT_MOD_INVALID)
		return fpga_drm_reject_atomic_state(fpga, stage, plane_state,
						    crtc_state, -EINVAL,
						    "unsupported-modifier");

	if (plane_state->fb->pitches[0] <
	    plane_state->fb->width * FPGA_DRM_BPP)
		return fpga_drm_reject_atomic_state(fpga, stage, plane_state,
						    crtc_state, -EINVAL,
						    "pitch-too-small");

	return 0;
}

static int fpga_drm_primary_atomic_check(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct fpga_drm_device *fpga = to_fpga(plane->dev);
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;
	const struct fpga_video_mode *mode;
	int ret;

	if (!plane_state || !plane_state->fb)
		return 0;

	if (!plane_state->crtc)
		return fpga_drm_reject_atomic_state(fpga, "primary", plane_state,
						    NULL, -EINVAL,
						    "missing-crtc");

	crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	if (!crtc_state)
		return fpga_drm_reject_atomic_state(fpga, "primary", plane_state,
						    NULL, -EINVAL,
						    "missing-crtc-state");

	mode = fpga_drm_find_video_mode(&crtc_state->mode);
	if (!mode) {
		drm_dbg_kms(&fpga->drm, "primary rejected unknown mode %s\n",
			    crtc_state->mode.name);
		return fpga_drm_reject_atomic_state(fpga, "primary", plane_state,
						    crtc_state, -EINVAL,
						    "unknown-mode");
	}

	ret = fpga_drm_check_xrgb8888_fb(fpga, plane_state, crtc_state,
					 "primary");
	if (ret)
		return ret;

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  false, false);
	if (ret)
		return fpga_drm_reject_atomic_state(fpga, "primary", plane_state,
						    crtc_state, ret,
						    "helper-check");

	if (plane_state->crtc_x || plane_state->crtc_y ||
	    plane_state->crtc_w != mode->drm.hdisplay ||
	    plane_state->crtc_h != mode->drm.vdisplay ||
	    (plane_state->src_w >> 16) != mode->drm.hdisplay ||
	    (plane_state->src_h >> 16) != mode->drm.vdisplay)
		return fpga_drm_reject_atomic_state(fpga, "primary", plane_state,
						    crtc_state, -EINVAL,
						    "not-fullscreen");

	return 0;
}

static int fpga_drm_overlay_atomic_check(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct fpga_drm_device *fpga = to_fpga(plane->dev);
	struct drm_plane_state *plane_state =
		drm_atomic_get_new_plane_state(state, plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!plane_state || !plane_state->fb)
		return 0;

	if (!enable_overlay)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    NULL, -EINVAL,
						    "overlay-disabled");

	if (!plane_state->crtc)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    NULL, -EINVAL,
						    "missing-crtc");

	crtc_state = drm_atomic_get_new_crtc_state(state, plane_state->crtc);
	if (!crtc_state)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    NULL, -EINVAL,
						    "missing-crtc-state");

	ret = fpga_drm_check_xrgb8888_fb(fpga, plane_state, crtc_state,
					 "overlay");
	if (ret)
		return ret;

	ret = drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, false);
	if (ret)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    crtc_state, ret,
						    "helper-check");

	if (plane_state->crtc_x < 0 || plane_state->crtc_y < 0 ||
	    plane_state->crtc_w <= 0 || plane_state->crtc_h <= 0 ||
	    plane_state->crtc_x + plane_state->crtc_w > crtc_state->mode.hdisplay ||
	    plane_state->crtc_y + plane_state->crtc_h > crtc_state->mode.vdisplay)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    crtc_state, -EINVAL,
						    "out-of-bounds");

	if ((plane_state->src_w >> 16) != plane_state->crtc_w ||
	    (plane_state->src_h >> 16) != plane_state->crtc_h)
		return fpga_drm_reject_atomic_state(fpga, "overlay", plane_state,
						    crtc_state, -EINVAL,
						    "scaling-unsupported");

	return 0;
}

static void fpga_drm_crtc_atomic_enable(struct drm_crtc *crtc,
					struct drm_atomic_state *state)
{
	struct fpga_drm_device *fpga = to_fpga(crtc->dev);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, crtc);
	const struct fpga_video_mode *mode =
		fpga_drm_find_video_mode(&crtc_state->mode);
	int ret;

	if (!mode) {
		drm_err(&fpga->drm, "CRTC enable rejected unknown mode %s\n",
			crtc_state->mode.name);
		return;
	}

	fpga_drm_stop_uploads(fpga);

	ret = fpga_drm_program_mode(fpga, mode);
	if (ret) {
		drm_err(&fpga->drm, "failed to program mode %s: %d\n",
			mode->name, ret);
		return;
	}

	mutex_lock(&fpga->upload_lock);
	fpga->active_mode = mode;
	fpga->pipe_enabled = true;
	mutex_unlock(&fpga->upload_lock);

	if (debug_logging)
		drm_info(&fpga->drm, "CRTC enable mode=%s\n", mode->name);
}

static void fpga_drm_crtc_atomic_disable(struct drm_crtc *crtc,
					 struct drm_atomic_state *state)
{
	struct fpga_drm_device *fpga = to_fpga(crtc->dev);

	mutex_lock(&fpga->upload_lock);
	fpga->pipe_enabled = false;
	mutex_unlock(&fpga->upload_lock);

	if (debug_logging)
		drm_info(&fpga->drm, "CRTC disable\n");

	fpga_drm_stop_uploads(fpga);
}

static void fpga_drm_crtc_atomic_flush(struct drm_crtc *crtc,
				       struct drm_atomic_state *state)
{
	struct fpga_drm_device *fpga = to_fpga(crtc->dev);
	struct drm_plane_state *primary_state;
	struct drm_plane_state *overlay_state = NULL;
	bool changed = false;
	bool primary_enabled;
	bool overlay_enabled;

	primary_state = drm_atomic_get_new_plane_state(state,
						       &fpga->primary_plane);
	if (enable_overlay)
		overlay_state = drm_atomic_get_new_plane_state(state,
							       &fpga->overlay_plane);

	mutex_lock(&fpga->upload_lock);
	if (primary_state) {
		fpga_drm_replace_snapshot(&fpga->primary, primary_state);
		changed = true;
	}
	if (overlay_state) {
		fpga_drm_replace_snapshot(&fpga->overlay, overlay_state);
		changed = true;
	}
	primary_enabled = fpga->primary.enabled;
	overlay_enabled = fpga->overlay.enabled;
	mutex_unlock(&fpga->upload_lock);

	fpga->atomic_commits++;

	if (debug_logging)
		drm_info(&fpga->drm,
			 "atomic flush changed=%u primary_state=%u overlay_state=%u primary_enabled=%u overlay_enabled=%u commits=%llu\n",
			 changed, primary_state ? 1 : 0, overlay_state ? 1 : 0,
			 primary_enabled, overlay_enabled, fpga->atomic_commits);

	if (changed)
		fpga_drm_queue_commit_upload(fpga);
}

static void fpga_drm_plane_atomic_update(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	/* The CRTC flush snapshots all planes and queues the composed upload. */
}

static void fpga_drm_plane_atomic_disable(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	/* Plane disable is handled by the CRTC flush snapshot path. */
}

static const struct drm_plane_helper_funcs fpga_drm_primary_helper_funcs = {
	.atomic_check = fpga_drm_primary_atomic_check,
	.atomic_update = fpga_drm_plane_atomic_update,
	.atomic_disable = fpga_drm_plane_atomic_disable,
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_helper_funcs fpga_drm_overlay_helper_funcs = {
	.atomic_check = fpga_drm_overlay_atomic_check,
	.atomic_update = fpga_drm_plane_atomic_update,
	.atomic_disable = fpga_drm_plane_atomic_disable,
	DRM_GEM_SHADOW_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs fpga_drm_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	DRM_GEM_SHADOW_PLANE_FUNCS,
};

static const struct drm_crtc_helper_funcs fpga_drm_crtc_helper_funcs = {
	.mode_valid = fpga_drm_crtc_mode_valid,
	.atomic_enable = fpga_drm_crtc_atomic_enable,
	.atomic_disable = fpga_drm_crtc_atomic_disable,
	.atomic_flush = fpga_drm_crtc_atomic_flush,
};

static const struct drm_crtc_funcs fpga_drm_crtc_funcs = {
	.reset = drm_atomic_helper_crtc_reset,
	.destroy = drm_crtc_cleanup,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_encoder_funcs fpga_drm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const u32 fpga_drm_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static const u64 fpga_drm_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID,
};

static enum drm_connector_status
fpga_drm_connector_detect(struct drm_connector *connector, bool force)
{
	struct fpga_drm_device *fpga = to_fpga(connector->dev);

	if (debug_logging)
		drm_info(&fpga->drm, "connector detect force=%u connected=%u\n",
			 force, connector_connected);

	return connector_connected ? connector_status_connected :
				     connector_status_disconnected;
}

static int fpga_drm_connector_get_modes(struct drm_connector *connector)
{
	struct fpga_drm_device *fpga = to_fpga(connector->dev);
	unsigned int count = 0;
	unsigned int i;

	if (!connector_connected) {
		if (debug_logging)
			drm_info(&fpga->drm, "get_modes: connector disconnected\n");
		return 0;
	}

	if (debug_logging)
		drm_info(&fpga->drm, "get_modes: adding %zu whitelist modes\n",
			 ARRAY_SIZE(fpga_video_modes));

	for (i = 0; i < ARRAY_SIZE(fpga_video_modes); i++) {
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev,
					  &fpga_video_modes[i].drm);
		if (!mode)
			continue;

		drm_mode_set_name(mode);
		drm_mode_probed_add(connector, mode);
		count++;
	}

	return count;
}

static const struct drm_connector_helper_funcs fpga_drm_connector_helper_funcs = {
	.get_modes = fpga_drm_connector_get_modes,
};

static const struct drm_connector_funcs fpga_drm_connector_funcs = {
	.detect = fpga_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_mode_config_funcs fpga_drm_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

DEFINE_DRM_GEM_FOPS(fpga_drm_fops);

static const struct drm_driver fpga_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &fpga_drm_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = "20260510",
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

static int fpga_drm_modeset_init(struct fpga_drm_device *fpga)
{
	struct drm_device *drm = &fpga->drm;
	u32 crtc_mask;
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 640;
	drm->mode_config.max_width = FPGA_DRM_MAX_WIDTH;
	drm->mode_config.min_height = 480;
	drm->mode_config.max_height = FPGA_DRM_MAX_HEIGHT;
	drm->mode_config.preferred_depth = 24;
	drm->mode_config.quirk_addfb_prefer_host_byte_order = true;
	drm->mode_config.funcs = &fpga_drm_mode_config_funcs;
	drm->mode_config.normalize_zpos = enable_overlay;

	ret = drm_universal_plane_init(drm, &fpga->primary_plane, 0,
				       &fpga_drm_plane_funcs,
				       fpga_drm_formats,
				       ARRAY_SIZE(fpga_drm_formats),
				       fpga_drm_modifiers,
				       DRM_PLANE_TYPE_PRIMARY,
				       "primary");
	if (ret)
		return ret;
	drm_plane_helper_add(&fpga->primary_plane,
			     &fpga_drm_primary_helper_funcs);

	ret = drm_plane_create_zpos_immutable_property(&fpga->primary_plane, 0);
	if (ret)
		return ret;

	ret = drm_crtc_init_with_planes(drm, &fpga->crtc,
					&fpga->primary_plane, NULL,
					&fpga_drm_crtc_funcs, "crtc-0");
	if (ret)
		return ret;
	drm_crtc_helper_add(&fpga->crtc, &fpga_drm_crtc_helper_funcs);

	crtc_mask = drm_crtc_mask(&fpga->crtc);
	fpga->primary_plane.possible_crtcs = crtc_mask;

	if (enable_overlay) {
		ret = drm_universal_plane_init(drm, &fpga->overlay_plane,
					       crtc_mask, &fpga_drm_plane_funcs,
					       fpga_drm_formats,
					       ARRAY_SIZE(fpga_drm_formats),
					       fpga_drm_modifiers,
					       DRM_PLANE_TYPE_OVERLAY,
					       "cpu-overlay");
		if (ret)
			return ret;
		drm_plane_helper_add(&fpga->overlay_plane,
				     &fpga_drm_overlay_helper_funcs);
		ret = drm_plane_create_zpos_immutable_property(&fpga->overlay_plane,
							       1);
		if (ret)
			return ret;
	}

	ret = drm_encoder_init(drm, &fpga->encoder, &fpga_drm_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, "encoder-0");
	if (ret)
		return ret;
	fpga->encoder.possible_crtcs = crtc_mask;

	ret = drm_connector_init(drm, &fpga->connector, &fpga_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;

	drm_connector_helper_add(&fpga->connector,
				 &fpga_drm_connector_helper_funcs);
	fpga->connector.display_info.non_desktop = connector_non_desktop;

	if (drm->mode_config.non_desktop_property) {
		ret = drm_object_property_set_value(&fpga->connector.base,
						    drm->mode_config.non_desktop_property,
						    connector_non_desktop);
		if (ret)
			return ret;
	}

	ret = drm_connector_attach_encoder(&fpga->connector, &fpga->encoder);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	return 0;
}

static void fpga_drm_free_frame_sgt(struct drm_device *drm, void *data)
{
	struct fpga_drm_device *fpga = data;

	if (fpga->frame_sgt_ready)
		sg_free_table(&fpga->frame_sgt);
}

static int fpga_drm_alloc_frame_buffers(struct fpga_drm_device *fpga)
{
	struct drm_device *drm = &fpga->drm;
	struct scatterlist *sg;
	unsigned int y;
	int ret;

	ret = sg_alloc_table(&fpga->frame_sgt, FPGA_DRM_MAX_HEIGHT, GFP_KERNEL);
	if (ret)
		return ret;
	fpga->frame_sgt_ready = true;

	ret = drmm_add_action_or_reset(drm, fpga_drm_free_frame_sgt, fpga);
	if (ret)
		return ret;

	for (y = 0; y < FPGA_DRM_MAX_HEIGHT; y++) {
		fpga->line_bufs[y] = drmm_kmalloc(drm, FPGA_DRM_MAX_LINE_BYTES,
						  GFP_KERNEL);
		if (!fpga->line_bufs[y])
			return -ENOMEM;
	}

	for_each_sg(fpga->frame_sgt.sgl, sg, FPGA_DRM_MAX_HEIGHT, y)
		sg_set_buf(sg, fpga->line_bufs[y], FPGA_DRM_MAX_LINE_BYTES);

	return 0;
}

static void fpga_drm_close_xdma(struct drm_device *drm, void *data)
{
	struct fpga_drm_device *fpga = data;

	if (fpga->xdma)
		xdma_device_close(fpga->pdev, fpga->xdma);
}

static int fpga_drm_open_xdma(struct fpga_drm_device *fpga)
{
	int ret;

	fpga->user_max = MAX_USER_IRQ;
	fpga->h2c_channel_max = XDMA_CHANNEL_NUM_MAX;
	fpga->c2h_channel_max = XDMA_CHANNEL_NUM_MAX;

	fpga->xdma = xdma_device_open(DRIVER_NAME, fpga->pdev,
				      &fpga->user_max,
				      &fpga->h2c_channel_max,
				      &fpga->c2h_channel_max);
	if (!fpga->xdma)
		return -ENODEV;

	fpga->mmio_bar = xdma_device_bypass_bar(fpga->xdma);
	ret = xdma_device_bypass_bar_info(fpga->xdma, &fpga->mmio_bar_idx,
					  &fpga->mmio_bar_len);
	if (!ret && fpga->mmio_bar) {
		fpga->mmio_bar_name = "bypass";
	} else {
		fpga->mmio_bar = xdma_device_user_bar(fpga->xdma);
		ret = xdma_device_user_bar_info(fpga->xdma, &fpga->mmio_bar_idx,
						&fpga->mmio_bar_len);
		if (!ret && fpga->mmio_bar)
			fpga->mmio_bar_name = "user";
	}

	if (ret || !fpga->mmio_bar) {
		xdma_device_close(fpga->pdev, fpga->xdma);
		fpga->xdma = NULL;
		fpga->mmio_bar = NULL;
		return ret ? ret : -ENODEV;
	}

	if (h2c_channel >= fpga->h2c_channel_max) {
		xdma_device_close(fpga->pdev, fpga->xdma);
		fpga->xdma = NULL;
		fpga->mmio_bar = NULL;
		return -EINVAL;
	}

	return drmm_add_action_or_reset(&fpga->drm, fpga_drm_close_xdma, fpga);
}

static int fpga_drm_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct fpga_drm_device *fpga;
	struct drm_device *drm;
	int ret;

	if (strcmp(composition_backend, "cpu")) {
		dev_err(&pdev->dev,
			"unsupported composition_backend=%s, only cpu is implemented\n",
			composition_backend);
		return -EINVAL;
	}

	fpga = devm_drm_dev_alloc(&pdev->dev, &fpga_drm_driver,
				  struct fpga_drm_device, drm);
	if (IS_ERR(fpga))
		return PTR_ERR(fpga);

	drm = &fpga->drm;
	fpga->pdev = pdev;
	fpga->active_mode = fpga_drm_preferred_mode();

	mutex_init(&fpga->upload_lock);
	mutex_init(&fpga->dma_lock);
	spin_lock_init(&fpga->dma_state_lock);
	init_waitqueue_head(&fpga->dma_idle_wq);
	INIT_WORK(&fpga->upload_work, fpga_drm_upload_work);
	INIT_WORK(&fpga->dma_complete_work, fpga_drm_dma_complete_work);
	INIT_DELAYED_WORK(&fpga->dma_timeout_work, fpga_drm_dma_timeout_work);

	ret = fpga_drm_alloc_frame_buffers(fpga);
	if (ret)
		return ret;

	ret = fpga_drm_open_xdma(fpga);
	if (ret)
		return ret;

	drm_info(drm,
		 "XDMA opened: user=%d h2c=%d c2h=%d using_h2c=%u mmio=%s BAR%d len=0x%llx\n",
		 fpga->user_max, fpga->h2c_channel_max, fpga->c2h_channel_max,
		 h2c_channel, fpga->mmio_bar_name, fpga->mmio_bar_idx,
		 (unsigned long long)fpga->mmio_bar_len);

	ret = fpga_drm_configure_static_pipeline(fpga);
	if (ret)
		return ret;

	ret = fpga_drm_modeset_init(fpga);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	if (enable_fbdev)
		drm_fbdev_generic_setup(drm, 32);

	drm_info(drm,
		 "registered %zu-mode XRGB8888 stream display max=%ux%u pixel_clock<=%u kHz connected=%u non_desktop=%u fbdev=%u upload=%u debug=%u overlay=%u composition=%s\n",
		 ARRAY_SIZE(fpga_video_modes), FPGA_DRM_MAX_WIDTH,
		 FPGA_DRM_MAX_HEIGHT, FPGA_DRM_MAX_PIXEL_CLOCK_KHZ,
		 connector_connected, connector_non_desktop, enable_fbdev,
		 upload_enabled, debug_logging, enable_overlay,
		 composition_backend);

	return 0;
}

static void fpga_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *drm = pci_get_drvdata(pdev);
	struct fpga_drm_device *fpga;

	if (!drm)
		return;

	fpga = to_fpga(drm);
	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);
	fpga_drm_stop_uploads(fpga);
	drm_info(drm,
		 "stats: atomic_commits=%llu atomic_rejects=%llu frames_queued=%llu frames_uploaded=%llu upload_failures=%llu cpu_compositions=%llu\n",
		 fpga->atomic_commits, fpga->atomic_rejects,
		 fpga->frames_queued, fpga->frames_uploaded,
		 fpga->upload_failures, fpga->cpu_compositions);
}

static void fpga_drm_shutdown(struct pci_dev *pdev)
{
	struct drm_device *drm = pci_get_drvdata(pdev);

	if (drm)
		drm_atomic_helper_shutdown(drm);
}

static const struct pci_device_id fpga_drm_pci_ids[] = {
	{ PCI_DEVICE(0x10ee, 0x9048), },
	{ PCI_DEVICE(0x10ee, 0x9044), },
	{ PCI_DEVICE(0x10ee, 0x9042), },
	{ PCI_DEVICE(0x10ee, 0x9041), },
	{ PCI_DEVICE(0x10ee, 0x903f), },
	{ PCI_DEVICE(0x10ee, 0x9038), },
	{ PCI_DEVICE(0x10ee, 0x9028), },
	{ PCI_DEVICE(0x10ee, 0x9018), },
	{ PCI_DEVICE(0x10ee, 0x9034), },
	{ PCI_DEVICE(0x10ee, 0x9024), },
	{ PCI_DEVICE(0x10ee, 0x9014), },
	{ PCI_DEVICE(0x10ee, 0x9032), },
	{ PCI_DEVICE(0x10ee, 0x9022), },
	{ PCI_DEVICE(0x10ee, 0x9012), },
	{ PCI_DEVICE(0x10ee, 0x9031), },
	{ PCI_DEVICE(0x10ee, 0x9021), },
	{ PCI_DEVICE(0x10ee, 0x9011), },
	{ PCI_DEVICE(0x10ee, 0x8011), },
	{ PCI_DEVICE(0x10ee, 0x8012), },
	{ PCI_DEVICE(0x10ee, 0x8014), },
	{ PCI_DEVICE(0x10ee, 0x8018), },
	{ PCI_DEVICE(0x10ee, 0x8021), },
	{ PCI_DEVICE(0x10ee, 0x8022), },
	{ PCI_DEVICE(0x10ee, 0x8024), },
	{ PCI_DEVICE(0x10ee, 0x8028), },
	{ PCI_DEVICE(0x10ee, 0x8031), },
	{ PCI_DEVICE(0x10ee, 0x8032), },
	{ PCI_DEVICE(0x10ee, 0x8034), },
	{ PCI_DEVICE(0x10ee, 0x8038), },
	{ PCI_DEVICE(0x10ee, 0x7011), },
	{ PCI_DEVICE(0x10ee, 0x7012), },
	{ PCI_DEVICE(0x10ee, 0x7014), },
	{ PCI_DEVICE(0x10ee, 0x7018), },
	{ PCI_DEVICE(0x10ee, 0x7021), },
	{ PCI_DEVICE(0x10ee, 0x7022), },
	{ PCI_DEVICE(0x10ee, 0x7024), },
	{ PCI_DEVICE(0x10ee, 0x7028), },
	{ PCI_DEVICE(0x10ee, 0x7031), },
	{ PCI_DEVICE(0x10ee, 0x7032), },
	{ PCI_DEVICE(0x10ee, 0x7034), },
	{ PCI_DEVICE(0x10ee, 0x7038), },
	{ PCI_DEVICE(0x10ee, 0x6828), },
	{ PCI_DEVICE(0x10ee, 0x6830), },
	{ PCI_DEVICE(0x10ee, 0x6928), },
	{ PCI_DEVICE(0x10ee, 0x6930), },
	{ PCI_DEVICE(0x10ee, 0x6A28), },
	{ PCI_DEVICE(0x10ee, 0x6A30), },
	{ PCI_DEVICE(0x10ee, 0x6D30), },
	{ PCI_DEVICE(0x10ee, 0x4808), },
	{ PCI_DEVICE(0x10ee, 0x4828), },
	{ PCI_DEVICE(0x10ee, 0x4908), },
	{ PCI_DEVICE(0x10ee, 0x4A28), },
	{ PCI_DEVICE(0x10ee, 0x4B28), },
	{ PCI_DEVICE(0x10ee, 0x2808), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, fpga_drm_pci_ids);

static struct pci_driver fpga_drm_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = fpga_drm_pci_ids,
	.probe = fpga_drm_probe,
	.remove = fpga_drm_remove,
	.shutdown = fpga_drm_shutdown,
};

module_pci_driver(fpga_drm_pci_driver);

MODULE_AUTHOR("alpk");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
