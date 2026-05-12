// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed-mode DRM/KMS driver for the FPGA PCIe HDMI stream design.
 *
 * The FPGA accepts XDMA H2C AXI-stream packets. Each packet is one 1280 pixel
 * scanline in XRGB8888 format, so TLAST must occur once per line.
 */

#include <linux/module.h>
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

unsigned int h2c_timeout = 10;
unsigned int c2h_timeout = 10;

struct fpga_drm_device {
	struct drm_device drm;
	struct pci_dev *pdev;
	void *xdma;
	int user_max;
	int h2c_channel_max;
	int c2h_channel_max;

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
	fpga->user_max = MAX_USER_IRQ;
	fpga->h2c_channel_max = XDMA_CHANNEL_NUM_MAX;
	fpga->c2h_channel_max = XDMA_CHANNEL_NUM_MAX;

	fpga->xdma = xdma_device_open(DRIVER_NAME, fpga->pdev,
				      &fpga->user_max,
				      &fpga->h2c_channel_max,
				      &fpga->c2h_channel_max);
	if (!fpga->xdma)
		return -ENODEV;

	if (h2c_channel >= fpga->h2c_channel_max) {
		xdma_device_close(fpga->pdev, fpga->xdma);
		fpga->xdma = NULL;
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

	drm_info(drm, "XDMA opened: user=%d h2c=%d c2h=%d using_h2c=%u\n",
		 fpga->user_max, fpga->h2c_channel_max, fpga->c2h_channel_max,
		 h2c_channel);

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
