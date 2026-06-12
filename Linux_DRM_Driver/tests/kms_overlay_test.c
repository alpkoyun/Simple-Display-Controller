// SPDX-License-Identifier: MIT
/*
 * Direct atomic KMS overlay test for fpga_drm.
 *
 * This intentionally uses the kernel DRM UAPI directly instead of libdrm:
 * the target machine has libdrm runtime packages installed, but not the
 * libdrm development headers. The test still exercises only standard KMS
 * ioctls and does not depend on any fpga_drm-private ABI.
 */

#define _GNU_SOURCE

#include <drm.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef DRM_PLANE_TYPE_OVERLAY
#define DRM_PLANE_TYPE_OVERLAY 0
#define DRM_PLANE_TYPE_PRIMARY 1
#define DRM_PLANE_TYPE_CURSOR 2
#endif

#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#endif

struct options {
	const char *device;
	int overlay_x;
	int overlay_y;
	uint32_t overlay_w;
	uint32_t overlay_h;
	unsigned int hold_seconds;
	bool test_only;
};

struct drm_resources {
	uint32_t *crtcs;
	uint32_t *connectors;
	uint32_t *encoders;
	uint32_t count_crtcs;
	uint32_t count_connectors;
	uint32_t count_encoders;
	uint32_t min_width;
	uint32_t max_width;
	uint32_t min_height;
	uint32_t max_height;
};

struct connector_info {
	uint32_t id;
	uint32_t encoder_id;
	uint32_t *encoders;
	uint32_t count_encoders;
	struct drm_mode_modeinfo mode;
};

struct plane_info {
	uint32_t id;
	uint32_t type;
	uint32_t possible_crtcs;
	uint32_t *formats;
	uint32_t count_formats;
};

struct dumb_fb {
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
	uint32_t handle;
	uint32_t fb_id;
	uint64_t size;
	void *map;
};

struct atomic_obj {
	uint32_t id;
	uint32_t props[16];
	uint64_t values[16];
	uint32_t count;
};

struct atomic_req {
	struct atomic_obj objects[4];
	uint32_t count;
};

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  --device PATH             DRM card node (default: /dev/dri/card0)\n"
		"  --overlay X,Y,W,H         Overlay rectangle (default: 80,60,320,180)\n"
		"  --hold SECONDS            Hold real commit before cleanup (default: 5)\n"
		"  --commit-test-only        Run atomic TEST_ONLY commit and exit\n"
		"  --test-only               Alias for --commit-test-only\n"
		"  -h, --help                Show this help\n",
		argv0);
}

static int parse_overlay(const char *arg, struct options *opts)
{
	int x, y;
	unsigned int w, h;

	if (sscanf(arg, "%d,%d,%u,%u", &x, &y, &w, &h) != 4 ||
	    x < 0 || y < 0 || w == 0 || h == 0) {
		fprintf(stderr, "invalid --overlay rectangle: %s\n", arg);
		return -1;
	}

	opts->overlay_x = x;
	opts->overlay_y = y;
	opts->overlay_w = w;
	opts->overlay_h = h;
	return 0;
}

static int parse_args(int argc, char **argv, struct options *opts)
{
	*opts = (struct options) {
		.device = "/dev/dri/card0",
		.overlay_x = 80,
		.overlay_y = 60,
		.overlay_w = 320,
		.overlay_h = 180,
		.hold_seconds = 5,
	};

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--device")) {
			if (++i >= argc) {
				fprintf(stderr, "--device needs an argument\n");
				return -1;
			}
			opts->device = argv[i];
		} else if (!strcmp(argv[i], "--overlay")) {
			if (++i >= argc) {
				fprintf(stderr, "--overlay needs an argument\n");
				return -1;
			}
			if (parse_overlay(argv[i], opts))
				return -1;
		} else if (!strcmp(argv[i], "--hold")) {
			char *end = NULL;
			unsigned long value;

			if (++i >= argc) {
				fprintf(stderr, "--hold needs an argument\n");
				return -1;
			}
			errno = 0;
			value = strtoul(argv[i], &end, 0);
			if (errno || !end || *end || value > 3600) {
				fprintf(stderr, "invalid --hold value: %s\n", argv[i]);
				return -1;
			}
			opts->hold_seconds = (unsigned int)value;
		} else if (!strcmp(argv[i], "--commit-test-only") ||
			   !strcmp(argv[i], "--test-only")) {
			opts->test_only = true;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			exit(0);
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			usage(argv[0]);
			return -1;
		}
	}

	return 0;
}

static int drm_ioctl_retry(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret && (errno == EINTR || errno == EAGAIN));

	return ret;
}

static int set_client_cap(int fd, uint64_t capability, uint64_t value)
{
	struct drm_set_client_cap cap = {
		.capability = capability,
		.value = value,
	};

	if (drm_ioctl_retry(fd, DRM_IOCTL_SET_CLIENT_CAP, &cap)) {
		fprintf(stderr, "DRM_IOCTL_SET_CLIENT_CAP(%" PRIu64 ") failed: %s\n",
			capability, strerror(errno));
		return -1;
	}

	return 0;
}

static void free_resources(struct drm_resources *res)
{
	free(res->crtcs);
	free(res->connectors);
	free(res->encoders);
	memset(res, 0, sizeof(*res));
}

static int get_resources(int fd, struct drm_resources *out)
{
	struct drm_mode_card_res res = {0};

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) {
		fprintf(stderr, "DRM_IOCTL_MODE_GETRESOURCES failed: %s\n",
			strerror(errno));
		return -1;
	}

	out->count_crtcs = res.count_crtcs;
	out->count_connectors = res.count_connectors;
	out->count_encoders = res.count_encoders;
	out->min_width = res.min_width;
	out->max_width = res.max_width;
	out->min_height = res.min_height;
	out->max_height = res.max_height;
	out->crtcs = calloc(out->count_crtcs, sizeof(*out->crtcs));
	out->connectors = calloc(out->count_connectors, sizeof(*out->connectors));
	out->encoders = calloc(out->count_encoders, sizeof(*out->encoders));
	if ((out->count_crtcs && !out->crtcs) ||
	    (out->count_connectors && !out->connectors) ||
	    (out->count_encoders && !out->encoders)) {
		perror("calloc resources");
		return -1;
	}

	res.crtc_id_ptr = (uintptr_t)out->crtcs;
	res.connector_id_ptr = (uintptr_t)out->connectors;
	res.encoder_id_ptr = (uintptr_t)out->encoders;
	res.count_crtcs = out->count_crtcs;
	res.count_connectors = out->count_connectors;
	res.count_encoders = out->count_encoders;

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETRESOURCES, &res)) {
		fprintf(stderr, "DRM_IOCTL_MODE_GETRESOURCES ids failed: %s\n",
			strerror(errno));
		return -1;
	}

	out->count_crtcs = res.count_crtcs;
	out->count_connectors = res.count_connectors;
	out->count_encoders = res.count_encoders;
	out->min_width = res.min_width;
	out->max_width = res.max_width;
	out->min_height = res.min_height;
	out->max_height = res.max_height;
	return 0;
}

static void free_connector_info(struct connector_info *conn)
{
	free(conn->encoders);
	memset(conn, 0, sizeof(*conn));
}

static int read_connector(int fd, uint32_t connector_id, struct connector_info *out)
{
	struct drm_mode_get_connector conn = {
		.connector_id = connector_id,
	};
	struct drm_mode_modeinfo *modes = NULL;
	uint32_t *encoders = NULL;
	uint32_t *props = NULL;
	uint64_t *prop_values = NULL;

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn)) {
		fprintf(stderr, "GETCONNECTOR %u failed: %s\n",
			connector_id, strerror(errno));
		return -1;
	}

	if (conn.count_modes)
		modes = calloc(conn.count_modes, sizeof(*modes));
	if (conn.count_encoders)
		encoders = calloc(conn.count_encoders, sizeof(*encoders));
	if (conn.count_props) {
		props = calloc(conn.count_props, sizeof(*props));
		prop_values = calloc(conn.count_props, sizeof(*prop_values));
	}
	if ((conn.count_modes && !modes) ||
	    (conn.count_encoders && !encoders) ||
	    (conn.count_props && (!props || !prop_values))) {
		perror("calloc connector");
		free(modes);
		free(encoders);
		free(props);
		free(prop_values);
		return -1;
	}

	conn.modes_ptr = (uintptr_t)modes;
	conn.encoders_ptr = (uintptr_t)encoders;
	conn.props_ptr = (uintptr_t)props;
	conn.prop_values_ptr = (uintptr_t)prop_values;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn)) {
		fprintf(stderr, "GETCONNECTOR %u arrays failed: %s\n",
			connector_id, strerror(errno));
		free(modes);
		free(encoders);
		free(props);
		free(prop_values);
		return -1;
	}

	if (conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0) {
		free(modes);
		free(encoders);
		free(props);
		free(prop_values);
		return 1;
	}

	out->id = connector_id;
	out->encoder_id = conn.encoder_id;
	out->count_encoders = conn.count_encoders;
	out->encoders = encoders;
	out->mode = modes[0];
	for (uint32_t i = 0; i < conn.count_modes; i++) {
		if (modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			out->mode = modes[i];
			break;
		}
	}
	free(modes);
	free(props);
	free(prop_values);
	return 0;
}

static int find_connected_connector(int fd, const struct drm_resources *res,
				    struct connector_info *out)
{
	for (uint32_t i = 0; i < res->count_connectors; i++) {
		int ret = read_connector(fd, res->connectors[i], out);

		if (ret < 0)
			return ret;
		if (ret == 0)
			return 0;
	}

	fprintf(stderr, "no connected connector with modes found\n");
	return -1;
}

static int get_encoder(int fd, uint32_t encoder_id, struct drm_mode_get_encoder *enc)
{
	memset(enc, 0, sizeof(*enc));
	enc->encoder_id = encoder_id;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETENCODER, enc)) {
		fprintf(stderr, "GETENCODER %u failed: %s\n",
			encoder_id, strerror(errno));
		return -1;
	}
	return 0;
}

static int pick_crtc(const struct drm_resources *res, const struct drm_mode_get_encoder *enc,
		     uint32_t *crtc_id, uint32_t *crtc_index)
{
	for (uint32_t i = 0; i < res->count_crtcs; i++) {
		if (enc->possible_crtcs & (1u << i)) {
			*crtc_id = res->crtcs[i];
			*crtc_index = i;
			return 0;
		}
	}
	return -1;
}

static int find_crtc_for_connector(int fd, const struct drm_resources *res,
				   const struct connector_info *conn,
				   uint32_t *crtc_id, uint32_t *crtc_index)
{
	struct drm_mode_get_encoder enc;

	if (conn->encoder_id && !get_encoder(fd, conn->encoder_id, &enc) &&
	    !pick_crtc(res, &enc, crtc_id, crtc_index))
		return 0;

	for (uint32_t i = 0; i < conn->count_encoders; i++) {
		if (!get_encoder(fd, conn->encoders[i], &enc) &&
		    !pick_crtc(res, &enc, crtc_id, crtc_index))
			return 0;
	}

	if (res->count_crtcs == 1) {
		*crtc_id = res->crtcs[0];
		*crtc_index = 0;
		return 0;
	}

	fprintf(stderr, "failed to find usable CRTC for connector %u\n", conn->id);
	return -1;
}

static int get_object_property(int fd, uint32_t obj_id, uint32_t obj_type,
			       const char *name, uint32_t *prop_id, uint64_t *value)
{
	struct drm_mode_obj_get_properties props = {
		.obj_id = obj_id,
		.obj_type = obj_type,
	};
	uint32_t *ids = NULL;
	uint64_t *values = NULL;
	int ret = -1;

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props)) {
		fprintf(stderr, "OBJ_GETPROPERTIES obj=%u failed: %s\n",
			obj_id, strerror(errno));
		return -1;
	}

	ids = calloc(props.count_props, sizeof(*ids));
	values = calloc(props.count_props, sizeof(*values));
	if ((props.count_props && !ids) || (props.count_props && !values)) {
		perror("calloc properties");
		goto out;
	}

	props.props_ptr = (uintptr_t)ids;
	props.prop_values_ptr = (uintptr_t)values;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props)) {
		fprintf(stderr, "OBJ_GETPROPERTIES obj=%u arrays failed: %s\n",
			obj_id, strerror(errno));
		goto out;
	}

	for (uint32_t i = 0; i < props.count_props; i++) {
		struct drm_mode_get_property prop = {
			.prop_id = ids[i],
		};

		if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETPROPERTY, &prop))
			continue;
		if (!strcmp(prop.name, name)) {
			if (prop_id)
				*prop_id = ids[i];
			if (value)
				*value = values[i];
			ret = 0;
			goto out;
		}
	}

	fprintf(stderr, "property '%s' not found on object %u\n", name, obj_id);

out:
	free(ids);
	free(values);
	return ret;
}

static bool format_supported(const struct plane_info *plane, uint32_t format)
{
	for (uint32_t i = 0; i < plane->count_formats; i++) {
		if (plane->formats[i] == format)
			return true;
	}
	return false;
}

static void free_plane_info(struct plane_info *plane)
{
	free(plane->formats);
	memset(plane, 0, sizeof(*plane));
}

static int read_plane(int fd, uint32_t plane_id, struct plane_info *out)
{
	struct drm_mode_get_plane plane = {
		.plane_id = plane_id,
	};
	uint64_t type = UINT64_MAX;

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETPLANE, &plane)) {
		fprintf(stderr, "GETPLANE %u failed: %s\n", plane_id, strerror(errno));
		return -1;
	}

	out->formats = calloc(plane.count_format_types, sizeof(*out->formats));
	if (plane.count_format_types && !out->formats) {
		perror("calloc plane formats");
		return -1;
	}

	plane.format_type_ptr = (uintptr_t)out->formats;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETPLANE, &plane)) {
		fprintf(stderr, "GETPLANE %u formats failed: %s\n",
			plane_id, strerror(errno));
		free_plane_info(out);
		return -1;
	}

	out->id = plane_id;
	out->possible_crtcs = plane.possible_crtcs;
	out->count_formats = plane.count_format_types;

	if (!get_object_property(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type", NULL, &type))
		out->type = (uint32_t)type;
	else
		out->type = UINT32_MAX;

	return 0;
}

static int find_planes(int fd, uint32_t crtc_index, uint32_t *primary_id,
		       uint32_t *overlay_id)
{
	struct drm_mode_get_plane_res res = {0};
	uint32_t *plane_ids = NULL;
	int ret = -1;

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res)) {
		fprintf(stderr, "GETPLANERESOURCES failed: %s\n", strerror(errno));
		return -1;
	}

	plane_ids = calloc(res.count_planes, sizeof(*plane_ids));
	if (res.count_planes && !plane_ids) {
		perror("calloc planes");
		return -1;
	}

	res.plane_id_ptr = (uintptr_t)plane_ids;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &res)) {
		fprintf(stderr, "GETPLANERESOURCES ids failed: %s\n", strerror(errno));
		goto out;
	}

	for (uint32_t i = 0; i < res.count_planes; i++) {
		struct plane_info plane = {0};
		bool can_use;

		if (read_plane(fd, plane_ids[i], &plane))
			goto out;

		can_use = (plane.possible_crtcs & (1u << crtc_index)) &&
			  format_supported(&plane, DRM_FORMAT_XRGB8888);

		printf("plane %u type=%u possible_crtcs=0x%x XR24=%u\n",
		       plane.id, plane.type, plane.possible_crtcs, can_use ? 1 : 0);

		if (can_use && plane.type == DRM_PLANE_TYPE_PRIMARY && !*primary_id)
			*primary_id = plane.id;
		else if (can_use && plane.type == DRM_PLANE_TYPE_OVERLAY && !*overlay_id)
			*overlay_id = plane.id;

		free_plane_info(&plane);
	}

	if (!*primary_id) {
		fprintf(stderr, "no usable XR24 primary plane found\n");
		goto out;
	}
	if (!*overlay_id) {
		fprintf(stderr, "no usable XR24 overlay plane found; load fpga_drm with enable_overlay=1\n");
		goto out;
	}

	ret = 0;
out:
	free(plane_ids);
	return ret;
}

static uint32_t xrgb(uint8_t r, uint8_t g, uint8_t b)
{
	return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void fill_primary(struct dumb_fb *fb)
{
	for (uint32_t y = 0; y < fb->height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)fb->map + y * fb->pitch);

		for (uint32_t x = 0; x < fb->width; x++) {
			uint8_t r = (uint8_t)((x * 255u) / (fb->width ? fb->width : 1));
			uint8_t g = (uint8_t)((y * 255u) / (fb->height ? fb->height : 1));
			uint8_t b = ((x / 32u) ^ (y / 32u)) & 1u ? 0x30 : 0x90;

			row[x] = xrgb(r, g, b);
		}
	}
}

static void fill_overlay(struct dumb_fb *fb)
{
	for (uint32_t y = 0; y < fb->height; y++) {
		uint32_t *row = (uint32_t *)((uint8_t *)fb->map + y * fb->pitch);

		for (uint32_t x = 0; x < fb->width; x++) {
			bool border = x < 8 || y < 8 || x >= fb->width - 8 ||
				      y >= fb->height - 8;
			bool stripe = ((x / 16u) + (y / 16u)) & 1u;

			if (border)
				row[x] = xrgb(255, 255, 255);
			else if (stripe)
				row[x] = xrgb(255, 30, 30);
			else
				row[x] = xrgb(30, 220, 255);
		}
	}
}

static int create_dumb_fb(int fd, uint32_t width, uint32_t height,
			  void (*fill)(struct dumb_fb *), struct dumb_fb *fb)
{
	struct drm_mode_create_dumb create = {
		.width = width,
		.height = height,
		.bpp = 32,
	};
	struct drm_mode_fb_cmd2 add = {
		.width = width,
		.height = height,
		.pixel_format = DRM_FORMAT_XRGB8888,
	};
	struct drm_mode_map_dumb map = {0};

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create)) {
		fprintf(stderr, "CREATE_DUMB %ux%u failed: %s\n",
			width, height, strerror(errno));
		return -1;
	}

	fb->width = width;
	fb->height = height;
	fb->pitch = create.pitch;
	fb->handle = create.handle;
	fb->size = create.size;

	add.handles[0] = fb->handle;
	add.pitches[0] = fb->pitch;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_ADDFB2, &add)) {
		fprintf(stderr, "ADDFB2 %ux%u failed: %s\n",
			width, height, strerror(errno));
		return -1;
	}
	fb->fb_id = add.fb_id;

	map.handle = fb->handle;
	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_MAP_DUMB, &map)) {
		fprintf(stderr, "MAP_DUMB failed: %s\n", strerror(errno));
		return -1;
	}

	fb->map = mmap(NULL, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fd, (off_t)map.offset);
	if (fb->map == MAP_FAILED) {
		fprintf(stderr, "mmap dumb buffer failed: %s\n", strerror(errno));
		fb->map = NULL;
		return -1;
	}

	fill(fb);
	return 0;
}

static void destroy_dumb_fb(int fd, struct dumb_fb *fb)
{
	if (fb->map)
		munmap(fb->map, fb->size);
	if (fb->fb_id) {
		unsigned int id = fb->fb_id;
		if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_RMFB, &id))
			fprintf(stderr, "RMFB %u failed: %s\n", fb->fb_id, strerror(errno));
	}
	if (fb->handle) {
		struct drm_mode_destroy_dumb destroy = {
			.handle = fb->handle,
		};
		if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy))
			fprintf(stderr, "DESTROY_DUMB failed: %s\n", strerror(errno));
	}
	memset(fb, 0, sizeof(*fb));
}

static int create_mode_blob(int fd, const struct drm_mode_modeinfo *mode,
			    uint32_t *blob_id)
{
	struct drm_mode_create_blob blob = {
		.data = (uintptr_t)mode,
		.length = sizeof(*mode),
	};

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_CREATEPROPBLOB, &blob)) {
		fprintf(stderr, "CREATEPROPBLOB failed: %s\n", strerror(errno));
		return -1;
	}
	*blob_id = blob.blob_id;
	return 0;
}

static void destroy_blob(int fd, uint32_t blob_id)
{
	struct drm_mode_destroy_blob blob = {
		.blob_id = blob_id,
	};

	if (blob_id && drm_ioctl_retry(fd, DRM_IOCTL_MODE_DESTROYPROPBLOB, &blob))
		fprintf(stderr, "DESTROYPROPBLOB %u failed: %s\n",
			blob_id, strerror(errno));
}

static struct atomic_obj *atomic_get_obj(struct atomic_req *req, uint32_t id)
{
	for (uint32_t i = 0; i < req->count; i++) {
		if (req->objects[i].id == id)
			return &req->objects[i];
	}

	if (req->count >= ARRAY_SIZE(req->objects))
		return NULL;

	req->objects[req->count].id = id;
	return &req->objects[req->count++];
}

static int atomic_add_prop_id(struct atomic_req *req, uint32_t obj_id,
			      uint32_t prop_id, uint64_t value)
{
	struct atomic_obj *obj = atomic_get_obj(req, obj_id);

	if (!obj || obj->count >= ARRAY_SIZE(obj->props)) {
		fprintf(stderr, "too many atomic objects/properties\n");
		return -1;
	}

	obj->props[obj->count] = prop_id;
	obj->values[obj->count] = value;
	obj->count++;
	return 0;
}

static int atomic_add_prop(int fd, struct atomic_req *req, uint32_t obj_id,
			   uint32_t obj_type, const char *name, uint64_t value)
{
	uint32_t prop_id;

	if (get_object_property(fd, obj_id, obj_type, name, &prop_id, NULL))
		return -1;
	return atomic_add_prop_id(req, obj_id, prop_id, value);
}

static int atomic_commit(int fd, const struct atomic_req *req, uint32_t flags)
{
	uint32_t total_props = 0;
	uint32_t obj_ids[ARRAY_SIZE(req->objects)];
	uint32_t counts[ARRAY_SIZE(req->objects)];
	uint32_t props[ARRAY_SIZE(req->objects) * 16];
	uint64_t values[ARRAY_SIZE(req->objects) * 16];
	struct drm_mode_atomic atomic = {
		.flags = flags,
		.count_objs = req->count,
		.objs_ptr = (uintptr_t)obj_ids,
		.count_props_ptr = (uintptr_t)counts,
		.props_ptr = (uintptr_t)props,
		.prop_values_ptr = (uintptr_t)values,
	};

	for (uint32_t i = 0; i < req->count; i++) {
		obj_ids[i] = req->objects[i].id;
		counts[i] = req->objects[i].count;
		for (uint32_t j = 0; j < req->objects[i].count; j++) {
			props[total_props] = req->objects[i].props[j];
			values[total_props] = req->objects[i].values[j];
			total_props++;
		}
	}

	if (drm_ioctl_retry(fd, DRM_IOCTL_MODE_ATOMIC, &atomic)) {
		fprintf(stderr, "ATOMIC commit flags=0x%x failed: %s\n",
			flags, strerror(errno));
		return -1;
	}

	return 0;
}

static int build_commit_req(int fd, struct atomic_req *req,
			    uint32_t connector_id, uint32_t crtc_id,
			    uint32_t primary_plane_id, uint32_t overlay_plane_id,
			    uint32_t mode_blob_id, const struct dumb_fb *primary,
			    const struct dumb_fb *overlay, int overlay_x,
			    int overlay_y, uint32_t overlay_w,
			    uint32_t overlay_h)
{
	memset(req, 0, sizeof(*req));

	if (atomic_add_prop(fd, req, connector_id, DRM_MODE_OBJECT_CONNECTOR,
			    "CRTC_ID", crtc_id) ||
	    atomic_add_prop(fd, req, crtc_id, DRM_MODE_OBJECT_CRTC,
			    "MODE_ID", mode_blob_id) ||
	    atomic_add_prop(fd, req, crtc_id, DRM_MODE_OBJECT_CRTC,
			    "ACTIVE", 1))
		return -1;

	if (atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "FB_ID", primary->fb_id) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_ID", crtc_id) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_X", 0) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_Y", 0) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_W", (uint64_t)primary->width << 16) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_H", (uint64_t)primary->height << 16) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_X", 0) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_Y", 0) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_W", primary->width) ||
	    atomic_add_prop(fd, req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_H", primary->height))
		return -1;

	if (atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "FB_ID", overlay->fb_id) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_ID", crtc_id) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_X", 0) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_Y", 0) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_W", (uint64_t)overlay_w << 16) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "SRC_H", (uint64_t)overlay_h << 16) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_X", (uint32_t)overlay_x) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_Y", (uint32_t)overlay_y) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_W", overlay_w) ||
	    atomic_add_prop(fd, req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_H", overlay_h))
		return -1;

	return 0;
}

static int cleanup_commit(int fd, uint32_t connector_id, uint32_t crtc_id,
			  uint32_t primary_plane_id, uint32_t overlay_plane_id)
{
	struct atomic_req req = {0};

	if (atomic_add_prop(fd, &req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "FB_ID", 0) ||
	    atomic_add_prop(fd, &req, overlay_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_ID", 0) ||
	    atomic_add_prop(fd, &req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "FB_ID", 0) ||
	    atomic_add_prop(fd, &req, primary_plane_id, DRM_MODE_OBJECT_PLANE,
			    "CRTC_ID", 0) ||
	    atomic_add_prop(fd, &req, crtc_id, DRM_MODE_OBJECT_CRTC,
			    "ACTIVE", 0) ||
	    atomic_add_prop(fd, &req, crtc_id, DRM_MODE_OBJECT_CRTC,
			    "MODE_ID", 0) ||
	    atomic_add_prop(fd, &req, connector_id, DRM_MODE_OBJECT_CONNECTOR,
			    "CRTC_ID", 0))
		return -1;

	return atomic_commit(fd, &req, DRM_MODE_ATOMIC_ALLOW_MODESET);
}

static void sleep_seconds(unsigned int seconds)
{
	struct timespec req = {
		.tv_sec = seconds,
	};

	while (nanosleep(&req, &req) && errno == EINTR)
		;
}

int main(int argc, char **argv)
{
	struct options opts;
	struct drm_resources res = {0};
	struct connector_info conn = {0};
	struct dumb_fb primary = {0};
	struct dumb_fb overlay = {0};
	struct atomic_req req = {0};
	uint32_t crtc_id = 0;
	uint32_t crtc_index = 0;
	uint32_t primary_plane_id = 0;
	uint32_t overlay_plane_id = 0;
	uint32_t mode_blob_id = 0;
	uint32_t overlay_fb_w;
	uint32_t overlay_fb_h;
	int fd = -1;
	int ret = 1;

	if (parse_args(argc, argv, &opts)) {
		usage(argv[0]);
		return 2;
	}

	fd = open(opts.device, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", opts.device, strerror(errno));
		return 1;
	}

	if (set_client_cap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) ||
	    set_client_cap(fd, DRM_CLIENT_CAP_ATOMIC, 1))
		goto out;

	if (get_resources(fd, &res) ||
	    find_connected_connector(fd, &res, &conn) ||
	    find_crtc_for_connector(fd, &res, &conn, &crtc_id, &crtc_index) ||
	    find_planes(fd, crtc_index, &primary_plane_id, &overlay_plane_id))
		goto out;

	if (opts.overlay_x + opts.overlay_w > conn.mode.hdisplay ||
	    opts.overlay_y + opts.overlay_h > conn.mode.vdisplay) {
		fprintf(stderr,
			"overlay %d,%d %ux%u is outside mode %ux%u\n",
			opts.overlay_x, opts.overlay_y, opts.overlay_w,
			opts.overlay_h, conn.mode.hdisplay, conn.mode.vdisplay);
		goto out;
	}

	printf("connector=%u crtc=%u primary=%u overlay=%u mode=%s %ux%u\n",
	       conn.id, crtc_id, primary_plane_id, overlay_plane_id,
	       conn.mode.name, conn.mode.hdisplay, conn.mode.vdisplay);
	printf("overlay rectangle=%d,%d %ux%u\n",
	       opts.overlay_x, opts.overlay_y, opts.overlay_w, opts.overlay_h);
	printf("framebuffer limits min=%ux%u max=%ux%u\n",
	       res.min_width, res.min_height, res.max_width, res.max_height);

	overlay_fb_w = opts.overlay_w < res.min_width ? res.min_width : opts.overlay_w;
	overlay_fb_h = opts.overlay_h < res.min_height ? res.min_height : opts.overlay_h;
	if (overlay_fb_w != opts.overlay_w || overlay_fb_h != opts.overlay_h)
		printf("overlay backing framebuffer=%ux%u, displayed source=%ux%u\n",
		       overlay_fb_w, overlay_fb_h, opts.overlay_w, opts.overlay_h);

	if (create_dumb_fb(fd, conn.mode.hdisplay, conn.mode.vdisplay,
			   fill_primary, &primary) ||
	    create_dumb_fb(fd, overlay_fb_w, overlay_fb_h,
			   fill_overlay, &overlay) ||
	    create_mode_blob(fd, &conn.mode, &mode_blob_id) ||
	    build_commit_req(fd, &req, conn.id, crtc_id, primary_plane_id,
			     overlay_plane_id, mode_blob_id, &primary, &overlay,
			     opts.overlay_x, opts.overlay_y, opts.overlay_w,
			     opts.overlay_h))
		goto out;

	if (opts.test_only) {
		if (atomic_commit(fd, &req, DRM_MODE_ATOMIC_ALLOW_MODESET |
				  DRM_MODE_ATOMIC_TEST_ONLY))
			goto out;
		printf("atomic TEST_ONLY commit succeeded\n");
		ret = 0;
		goto out;
	}

	if (atomic_commit(fd, &req, DRM_MODE_ATOMIC_ALLOW_MODESET))
		goto out;

	printf("atomic overlay commit succeeded; holding for %u seconds\n",
	       opts.hold_seconds);
	sleep_seconds(opts.hold_seconds);

	if (cleanup_commit(fd, conn.id, crtc_id, primary_plane_id,
			   overlay_plane_id))
		goto out;
	printf("display state cleaned up\n");

	ret = 0;

out:
	destroy_blob(fd, mode_blob_id);
	destroy_dumb_fb(fd, &overlay);
	destroy_dumb_fb(fd, &primary);
	free_connector_info(&conn);
	free_resources(&res);
	if (fd >= 0)
		close(fd);
	return ret;
}
