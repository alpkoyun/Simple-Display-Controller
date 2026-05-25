// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed-mode DRM/KMS driver for the FPGA PCIe HDMI stream design.
 *
 * The FPGA accepts XDMA H2C AXI-stream packets. Each packet is one 1280 pixel
 * scanline in XRGB8888 format, so TLAST must occur once per line.
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
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_simple_kms_helper.h>

#include "libxdma_api.h"
#include "libxdma.h"

#define DRIVER_NAME	"fpga_drm"
#define DRIVER_DESC	"FPGA PCIe XDMA fixed-mode DRM driver"
#define DRIVER_MAJOR	0
#define DRIVER_MINOR	1

#define FPGA_DRM_WIDTH		1280
#define FPGA_DRM_HEIGHT		720
#define FPGA_DRM_BPP		4
#define FPGA_DRM_LINE_BYTES	(FPGA_DRM_WIDTH * FPGA_DRM_BPP)
#define FPGA_DRM_FRAME_BYTES	(FPGA_DRM_LINE_BYTES * FPGA_DRM_HEIGHT)
#define FPGA_DRM_TIMEOUT_MS	1000

#define FPGA_HW_FRAME_COUNT		4U
#define FPGA_HW_FRAME_SPACING		(FPGA_DRM_FRAME_BYTES + 0x1000U)
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
#define VTC_POL_ALL			0x7F

#define AXI_GPIO_DATA_CH1		0x00
#define AXI_GPIO_TRI_CH1		0x04
#define AXI_GPIO_DATA_CH2		0x08
#define AXI_GPIO_TRI_CH2		0x0C

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
		 "Configure FPGA video IPs through XDMA MMIO BAR during probe. Default is true.");

unsigned int h2c_timeout = 10;
unsigned int c2h_timeout = 10;

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

	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;

	struct work_struct upload_work;
	struct work_struct dma_complete_work;
	struct delayed_work dma_timeout_work;
	struct mutex upload_lock;
	struct mutex dma_lock;
	spinlock_t dma_state_lock;
	wait_queue_head_t dma_idle_wq;
	struct drm_framebuffer *upload_fb;
	struct drm_rect upload_rect;
	struct iosys_map upload_map;
	bool pipe_enabled;

	struct sg_table frame_sgt;
	bool frame_sgt_ready;
	struct xdma_io_cb frame_cb;
	u8 *line_bufs[FPGA_DRM_HEIGHT];
	bool dma_inflight;
	bool dma_completion_pending;
	bool upload_pending;
	int dma_completion_err;
	u64 frames_queued;
	u64 frames_uploaded;
	u64 upload_failures;
};

static struct fpga_drm_device *to_fpga(struct drm_device *drm)
{
	return container_of(drm, struct fpga_drm_device, drm);
}

static const struct drm_display_mode fpga_drm_mode = {
	.clock = 74250,
	.hdisplay = FPGA_DRM_WIDTH,
	.hsync_start = 1390,
	.hsync_end = 1430,
	.htotal = 1650,
	.vdisplay = FPGA_DRM_HEIGHT,
	.vsync_start = 725,
	.vsync_end = 730,
	.vtotal = 750,
	.flags = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

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
				 FPGA_DRM_LINE_BYTES);
	if (ret)
		return ret;

	ret = fpga_drm_axi_write(fpga, FPGA_HW_VDMA_BASE + addr_base +
				 AXI_VDMA_STRD_FRMDLY_OFFSET,
				 (frame_delay << 24) | FPGA_DRM_LINE_BYTES);
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
				 AXI_VDMA_VSIZE_OFFSET, FPGA_DRM_HEIGHT);
	if (ret)
		return ret;

	ret = fpga_drm_axi_read(fpga, FPGA_HW_VDMA_BASE + chan +
				AXI_VDMA_SR_OFFSET, &status);
	if (ret)
		return ret;

	drm_info(&fpga->drm,
		 "%s configured at 0x%08llx CR=0x%08x SR=0x%08x frames=%u first=0x%08llx\n",
		 name, FPGA_HW_VDMA_BASE + chan,
		 (u32)(cr | AXI_VDMA_CR_RUNSTOP),
		 status, FPGA_HW_FRAME_COUNT, fpga_hw_frame_addr[0]);
	return 0;
}

static int fpga_drm_config_vdma(struct fpga_drm_device *fpga)
{
	u32 parkptr;
	int ret;

	ret = fpga_drm_program_vdma_channel(fpga, "VDMA S2MM",
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

	ret = fpga_drm_program_vdma_channel(fpga, "VDMA MM2S",
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

static int fpga_drm_config_vtc(struct fpga_drm_device *fpga)
{
	const u32 htotal = 1650;
	const u32 vtotal = 750;
	const u32 hactive = FPGA_DRM_WIDTH;
	const u32 vactive = FPGA_DRM_HEIGHT;
	const u32 hsync_start = 1390;
	const u32 hsync_end = 1430;
	const u32 vsync_start = 724;
	const u32 vsync_end = 729;
	u32 ctl, gtstat;
	int ret;

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
	ret = fpga_drm_axi_write(fpga, FPGA_HW_VTC_BASE + VTC_GPOL,
				 VTC_POL_ALL);
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
		 "VTC configured at 0x%08llx 1280x720@60 CTL=0x%08x GTSTAT=0x%08x\n",
		 FPGA_HW_VTC_BASE, ctl, gtstat);
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

static int fpga_drm_configure_pipeline(struct fpga_drm_device *fpga)
{
	u32 vdma_s2mm_sr, vdma_mm2s_sr, vtc_isr, vtc_err, clk_wiz_status;
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
		 FPGA_DRM_FRAME_BYTES - 1,
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
	ret = fpga_drm_config_vdma(fpga);
	if (ret)
		return ret;
	ret = fpga_drm_config_iic_hdmi(fpga);
	if (ret)
		return ret;
	ret = fpga_drm_config_vtc(fpga);
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
		 "pipeline readback: VDMA S2MM_SR=0x%08x MM2S_SR=0x%08x VTC_ISR=0x%08x VTC_ERR=0x%08x CLK_WIZ_STATUS=0x%08x\n",
		 vdma_s2mm_sr, vdma_mm2s_sr, vtc_isr, vtc_err, clk_wiz_status);
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
	unsigned long flags;
	bool upload_pending;
	int ret = completion_err;

	if (!ret && completion_len != FPGA_DRM_FRAME_BYTES)
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
				 "async frame upload complete count=%llu\n",
				 fpga->frames_uploaded);
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
					   &fpga->frame_sgt, false,
					   FPGA_DRM_TIMEOUT_MS);
		fpga->frame_cb.req = NULL;
	}

	fpga_drm_dma_finish(fpga, err, ret);

	mutex_unlock(&fpga->dma_lock);
}

static int fpga_drm_copy_frame(struct fpga_drm_device *fpga,
			       struct drm_framebuffer *fb,
			       const struct iosys_map *map)
{
	u8 *src = map->vaddr;
	unsigned int y;

	if (!src)
		return -EINVAL;

	if (fb->format->format != DRM_FORMAT_XRGB8888 ||
	    fb->width != FPGA_DRM_WIDTH || fb->height != FPGA_DRM_HEIGHT)
		return -EINVAL;

	if (fb->pitches[0] < FPGA_DRM_LINE_BYTES)
		return -EINVAL;

	for (y = 0; y < FPGA_DRM_HEIGHT; y++)
		memcpy(fpga->line_bufs[y], src + y * fb->pitches[0],
		       FPGA_DRM_LINE_BYTES);

	return 0;
}

static int fpga_drm_submit_frame_nowait(struct fpga_drm_device *fpga,
					struct drm_framebuffer *fb,
					const struct iosys_map *map)
{
	unsigned long flags;
	ssize_t ret;

	if (debug_logging)
		drm_info(&fpga->drm,
			 "upload frame fb=%ux%u pitch=%u format=%p4cc\n",
			 fb->width, fb->height, fb->pitches[0],
			 &fb->format->format);

	ret = fpga_drm_copy_frame(fpga, fb, map);
	if (ret)
		return ret;

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
					    &fpga->frame_sgt, false,
					    FPGA_DRM_LINE_BYTES,
					    FPGA_DRM_HEIGHT);
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
	struct drm_framebuffer *fb = NULL;
	struct iosys_map map;
	int ret;

	mutex_lock(&fpga->upload_lock);
	if (fpga->pipe_enabled && fpga->upload_fb) {
		fb = fpga->upload_fb;
		drm_framebuffer_get(fb);
		map = fpga->upload_map;
	}
	mutex_unlock(&fpga->upload_lock);

	if (!fb)
		return;

	if (!upload_enabled) {
		if (debug_logging)
			drm_info(&fpga->drm, "upload skipped because upload_enabled=0\n");
		drm_framebuffer_put(fb);
		return;
	}

	mutex_lock(&fpga->dma_lock);
	if (fpga_drm_dma_busy(fpga)) {
		mutex_unlock(&fpga->dma_lock);
		drm_framebuffer_put(fb);
		return;
	}

	ret = fpga_drm_submit_frame_nowait(fpga, fb, &map);
	mutex_unlock(&fpga->dma_lock);

	if (ret) {
		fpga->upload_failures++;
		drm_err_ratelimited(&fpga->drm,
				    "async frame submit failed: %d\n", ret);
	}

	drm_framebuffer_put(fb);
}

static void fpga_drm_mark_dirty(struct fpga_drm_device *fpga,
				struct drm_framebuffer *fb,
				const struct iosys_map *map,
				const struct drm_rect *dirty)
{
	struct drm_framebuffer *old_fb = NULL;

	mutex_lock(&fpga->upload_lock);
	if (!fpga->pipe_enabled) {
		mutex_unlock(&fpga->upload_lock);
		return;
	}

	if (fpga->upload_fb != fb) {
		old_fb = fpga->upload_fb;
		drm_framebuffer_get(fb);
		fpga->upload_fb = fb;
		fpga->upload_map = *map;
		fpga->upload_rect = *dirty;
	} else if (!upload_full_frame) {
		struct drm_rect *rect = &fpga->upload_rect;

		rect->x1 = min(rect->x1, dirty->x1);
		rect->y1 = min(rect->y1, dirty->y1);
		rect->x2 = max(rect->x2, dirty->x2);
		rect->y2 = max(rect->y2, dirty->y2);
	}
	mutex_unlock(&fpga->upload_lock);

	if (old_fb)
		drm_framebuffer_put(old_fb);

	fpga->frames_queued++;
	if (debug_logging)
		drm_info(&fpga->drm,
			 "queue upload count=%llu dirty=%d,%d-%d,%d full=%u enabled=%u\n",
			 fpga->frames_queued, dirty->x1, dirty->y1, dirty->x2,
			 dirty->y2, upload_full_frame, upload_enabled);

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
	old_fb = fpga->upload_fb;
	fpga->upload_fb = NULL;
	iosys_map_clear(&fpga->upload_map);
	mutex_unlock(&fpga->upload_lock);

	if (old_fb)
		drm_framebuffer_put(old_fb);
}

static enum drm_mode_status
fpga_drm_mode_valid(struct drm_simple_display_pipe *pipe,
		    const struct drm_display_mode *mode)
{
	if (mode->hdisplay == FPGA_DRM_WIDTH &&
	    mode->vdisplay == FPGA_DRM_HEIGHT &&
	    drm_mode_vrefresh(mode) == 60)
		return MODE_OK;

	return MODE_BAD;
}

static void fpga_drm_pipe_enable(struct drm_simple_display_pipe *pipe,
				 struct drm_crtc_state *crtc_state,
				 struct drm_plane_state *plane_state)
{
	struct fpga_drm_device *fpga = to_fpga(pipe->crtc.dev);
	struct drm_shadow_plane_state *shadow_state =
		to_drm_shadow_plane_state(plane_state);
	struct drm_rect rect = {
		.x1 = 0,
		.y1 = 0,
		.x2 = FPGA_DRM_WIDTH,
		.y2 = FPGA_DRM_HEIGHT,
	};

	mutex_lock(&fpga->upload_lock);
	fpga->pipe_enabled = true;
	mutex_unlock(&fpga->upload_lock);

	if (debug_logging)
		drm_info(&fpga->drm, "pipe enable fb=%p\n", plane_state->fb);

	fpga_drm_mark_dirty(fpga, plane_state->fb, &shadow_state->data[0], &rect);
}

static void fpga_drm_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct fpga_drm_device *fpga = to_fpga(pipe->crtc.dev);

	mutex_lock(&fpga->upload_lock);
	fpga->pipe_enabled = false;
	mutex_unlock(&fpga->upload_lock);

	if (debug_logging)
		drm_info(&fpga->drm, "pipe disable\n");

	fpga_drm_stop_uploads(fpga);
}

static void fpga_drm_pipe_update(struct drm_simple_display_pipe *pipe,
				 struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_shadow_plane_state *shadow_state =
		to_drm_shadow_plane_state(state);
	struct fpga_drm_device *fpga = to_fpga(pipe->crtc.dev);
	struct drm_rect rect;

	if (!state->fb)
		return;

	if (upload_full_frame) {
		rect.x1 = 0;
		rect.y1 = 0;
		rect.x2 = FPGA_DRM_WIDTH;
		rect.y2 = FPGA_DRM_HEIGHT;
		if (debug_logging)
			drm_info(&fpga->drm, "pipe update full-frame\n");
		fpga_drm_mark_dirty(fpga, state->fb, &shadow_state->data[0], &rect);
		return;
	}

	if (drm_atomic_helper_damage_merged(old_state, state, &rect)) {
		if (debug_logging)
			drm_info(&fpga->drm, "pipe update damage=%d,%d-%d,%d\n",
				 rect.x1, rect.y1, rect.x2, rect.y2);
		fpga_drm_mark_dirty(fpga, state->fb, &shadow_state->data[0], &rect);
	}
}

static const struct drm_simple_display_pipe_funcs fpga_drm_pipe_funcs = {
	.enable = fpga_drm_pipe_enable,
	.disable = fpga_drm_pipe_disable,
	.update = fpga_drm_pipe_update,
	.mode_valid = fpga_drm_mode_valid,
	DRM_GEM_SIMPLE_DISPLAY_PIPE_SHADOW_PLANE_FUNCS,
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

	if (!connector_connected) {
		if (debug_logging)
			drm_info(&fpga->drm, "get_modes: connector disconnected\n");
		return 0;
	}

	if (debug_logging)
		drm_info(&fpga->drm, "get_modes: adding fixed 1280x720@60\n");

	return drm_connector_helper_get_modes_fixed(connector, &fpga_drm_mode);
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
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = FPGA_DRM_WIDTH;
	drm->mode_config.max_width = FPGA_DRM_WIDTH;
	drm->mode_config.min_height = FPGA_DRM_HEIGHT;
	drm->mode_config.max_height = FPGA_DRM_HEIGHT;
	drm->mode_config.preferred_depth = 24;
	drm->mode_config.quirk_addfb_prefer_host_byte_order = true;
	drm->mode_config.funcs = &fpga_drm_mode_config_funcs;

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

	ret = drm_simple_display_pipe_init(drm, &fpga->pipe,
					   &fpga_drm_pipe_funcs,
					   fpga_drm_formats,
					   ARRAY_SIZE(fpga_drm_formats),
					   fpga_drm_modifiers,
					   &fpga->connector);
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

	ret = sg_alloc_table(&fpga->frame_sgt, FPGA_DRM_HEIGHT, GFP_KERNEL);
	if (ret)
		return ret;
	fpga->frame_sgt_ready = true;

	ret = drmm_add_action_or_reset(drm, fpga_drm_free_frame_sgt, fpga);
	if (ret)
		return ret;

	for (y = 0; y < FPGA_DRM_HEIGHT; y++) {
		fpga->line_bufs[y] = drmm_kmalloc(drm, FPGA_DRM_LINE_BYTES,
						  GFP_KERNEL);
		if (!fpga->line_bufs[y])
			return -ENOMEM;
	}

	for_each_sg(fpga->frame_sgt.sgl, sg, FPGA_DRM_HEIGHT, y)
		sg_set_buf(sg, fpga->line_bufs[y], FPGA_DRM_LINE_BYTES);

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

	fpga = devm_drm_dev_alloc(&pdev->dev, &fpga_drm_driver,
				  struct fpga_drm_device, drm);
	if (IS_ERR(fpga))
		return PTR_ERR(fpga);

	drm = &fpga->drm;
	fpga->pdev = pdev;

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

	ret = fpga_drm_configure_pipeline(fpga);
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
		 "registered fixed %ux%u XRGB8888 stream display, connected=%u non_desktop=%u fbdev=%u upload=%u debug=%u\n",
		 FPGA_DRM_WIDTH, FPGA_DRM_HEIGHT, connector_connected,
		 connector_non_desktop, enable_fbdev, upload_enabled, debug_logging);

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
