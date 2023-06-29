/*
 * Copyright(c) 2021 NXP. All rights reserved.
 * Copyright (C) 2014 Collabora Ltd.
 *
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifdef ENABLE_WAYLAND
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/queue.h"
#include "pitcher/dmabuf.h"
#include "mxc_v4l2_vpu_enc.h"

#include <libdrm/drm_fourcc.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include "wayland-generated-protocols/linux-dmabuf-unstable-v1-client-protocol.h"
#include "wayland-generated-protocols/xdg-shell-client-protocol.h"

struct wayland_buffer_link;
struct wayland_sink_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	uint32_t ifmt;
	struct pix_fmt_info format;
	int end;
	int done;
	unsigned long frame_count;
	unsigned long disp_count;
	long buffer_count;
	Queue queue;
	Queue links;
	uint32_t fps;
	uint64_t interval;
	uint64_t tv_next;

	uint32_t redraw_pending;
	pthread_mutex_t render_lock;
	pthread_cond_t redraw_wait;
	pthread_t tid;
	pthread_t pid;

	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_registry *registry;
	struct wl_surface *video_surface;

	struct xdg_wm_base *xdg_wm_base;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct wl_shm *wl_shm;
	bool use_shm;
	int shm_index;

	uint32_t (*format_pixel_2_shm)(uint32_t format);
	uint32_t (*format_pixel_2_wl)(uint32_t format);
	uint32_t (*format_wl_2_pixel)(uint32_t format);
};

struct construct_buffer_data {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct wl_buffer *wbuf;
};

struct wl_videoformat {
	enum wl_shm_format wl_shm_format;
	uint32_t dma_format;
	uint32_t pix_format;
	uint32_t enable;
};

struct wayland_buffer_link {
	struct wl_buffer *wbuf;
	struct pitcher_buffer *buffer;
	struct wayland_sink_test_t *wlc;
	bool is_shm;
	int shm_fd;
	void *shm_virt;
};

static struct wl_videoformat wl_formats[] = {
	{WL_SHM_FORMAT_YUYV,   DRM_FORMAT_YUYV,   PIX_FMT_YUYV,    0},
	{WL_SHM_FORMAT_NV12,   DRM_FORMAT_NV12,   PIX_FMT_NV12,    0},
	{WL_SHM_FORMAT_YUV420, DRM_FORMAT_YUV420, PIX_FMT_I420,    0},
};

uint32_t pixel_foramt_to_wl_shm_format(uint32_t format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wl_formats); i++) {
		if (format == wl_formats[i].pix_format)
			return wl_formats[i].wl_shm_format;
	}

	return -1;
}

uint32_t pixel_foramt_to_wl_dmabuf_format(uint32_t format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wl_formats); i++) {
		if (!wl_formats[i].enable)
			continue;
		if (format == wl_formats[i].pix_format)
			return wl_formats[i].dma_format;
	}

	return DRM_FORMAT_INVALID;
}

uint32_t wl_dmabuf_format_to_pixel_format(uint32_t dma_format)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wl_formats); i++) {
		if (dma_format == wl_formats[i].dma_format)
			return wl_formats[i].pix_format;
	}

	return 0;
}

struct mxc_vpu_test_option waylandsink_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"framerate", 1, "--framerate <f>\n\t\t\tset fps"},
	{"interval", 1, "--interval <val>\n\t\t\tset frame interval"},
	{"shm", 1, "--shm <val>\n\t\t\tuse shm, default:0"},
	{NULL, 0, NULL},
};

void dmabuf_format (void *data,
		struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
		uint32_t format)
{
}


void dmabuf_modifier (void *data,
		struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf_v1,
		uint32_t format,
		uint32_t modifier_hi,
		uint32_t modifier_lo)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wl_formats); i++) {
		if (format == wl_formats[i].dma_format)
			wl_formats[i].enable = 1;
	}
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
	dmabuf_format,
	dmabuf_modifier,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	xdg_surface_ack_configure(xdg_surface, serial);

	PITCHER_LOG("configure xdg surface\n");
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	.ping = xdg_wm_base_ping,
};

void global_registry_handler(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version)
{
	struct wayland_sink_test_t *wlc = data;

	/*PITCHER_LOG("interface : %s, version : %d\n", interface, version);*/
	if (strcmp(interface, "wl_compositor") == 0) {
		wlc->compositor = wl_registry_bind(registry,
				id,
				&wl_compositor_interface,
				min(version, 3));
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wlc->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wlc->xdg_wm_base, &xdg_wm_base_listener, wlc);
	} else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		wlc->dmabuf = wl_registry_bind(registry,
				id,
				&zwp_linux_dmabuf_v1_interface,
				min(version, 3));
		zwp_linux_dmabuf_v1_add_listener(wlc->dmabuf, &dmabuf_listener, wlc);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wlc->wl_shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
	}
}

void global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

void redraw(void *data, struct wl_callback *callback, uint32_t time)
{
	struct wayland_sink_test_t *wlc = data;

	pthread_mutex_lock(&wlc->render_lock);
	wlc->redraw_pending = 0;
	pthread_cond_signal(&wlc->redraw_wait);
	pthread_mutex_unlock(&wlc->render_lock);
	SAFE_RELEASE(callback, wl_callback_destroy);
}

static const struct wl_callback_listener frame_listener = {
	redraw
};

void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct wayland_buffer_link *link = data;

	pitcher_start_cpu_access(link->buffer, 1, 1);
	pitcher_put_buffer(link->buffer);
	atomic_dec(&link->wlc->buffer_count);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

void create_succeeded(void *data,
		struct zwp_linux_buffer_params_v1 *params,
		struct wl_buffer *new_buffer)
{
	struct construct_buffer_data *d = data;

	pthread_mutex_lock(&d->lock);
	d->wbuf = new_buffer;
	zwp_linux_buffer_params_v1_destroy (params);
	pthread_cond_signal(&d->cond);
	pthread_mutex_unlock(&d->lock);
}

void create_failed (void *data,
		struct zwp_linux_buffer_params_v1 *params)
{
	create_succeeded(data, params, NULL);
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
	create_succeeded,
	create_failed
};

void *wl_display_thread_run(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;

	while (wl_display_dispatch(wlc->display) != -1) {
		if (wlc->end && wlc->done && !wlc->buffer_count)
			break;
	}

	PITCHER_LOG("wayland display thread done\n");
	wlc->end = true;
	return NULL;
}

struct wl_buffer *wl_linux_dmabuf_construct_wl_buffer(
		struct wayland_sink_test_t *wlc,
		struct pitcher_buffer *buffer)
{
	int i;
	uint32_t format = DRM_FORMAT_INVALID;
	uint32_t flags = 0;
	struct construct_buffer_data data;
	struct zwp_linux_buffer_params_v1 *params;
	struct timespec timeout;
	struct pitcher_buf_ref plane;
	int ret;

	if (!wlc->dmabuf)
		return NULL;

	if (wlc->format_pixel_2_wl)
		format = wlc->format_pixel_2_wl(buffer->format->format);
	if (format == DRM_FORMAT_INVALID) {
		PITCHER_ERR("wayland sink doesn't support format %s\n",
				pitcher_get_format_name(buffer->format->format));
		return NULL;
	}

	data.wbuf = NULL;
	pthread_cond_init(&data.cond, NULL);
	pthread_mutex_init(&data.lock, NULL);
	pthread_mutex_lock(&data.lock);

	params = zwp_linux_dmabuf_v1_create_params(wlc->dmabuf);
	if (!params)
		goto exit;

	for (i = 0; i < buffer->format->num_planes; i++) {
		plane.dmafd = -1;
		pitcher_get_buffer_plane(buffer, i, &plane);
		if (plane.dmafd < 0)
			goto exit;
		zwp_linux_buffer_params_v1_add(params,
				plane.dmafd, i, plane.offset,
				buffer->format->planes[i].line,
				0, 0);
	}
	zwp_linux_buffer_params_v1_add_listener(params, &params_listener, &data);
	zwp_linux_buffer_params_v1_create(params,
			wlc->format.width,
			wlc->format.height,
			format, flags);
	wl_display_flush(wlc->display);
	data.wbuf = (struct wl_buffer *)0x1;
	while (data.wbuf == (struct wl_buffer *)0x1) {
		clock_gettime(CLOCK_REALTIME, &timeout);
		timeout.tv_sec += 3;
		if ((ret = pthread_cond_timedwait(&data.cond, &data.lock, &timeout))) {
			PITCHER_ERR("zwp_linux_buffer_params_v1 time out\n");
			zwp_linux_buffer_params_v1_destroy(params);
			data.wbuf = NULL;
		}
	}
exit:
	if (!data.wbuf)
		PITCHER_ERR("can't create linux-dmabuf buffer\n");
	pthread_mutex_unlock(&data.lock);
	pthread_cond_destroy(&data.cond);
	pthread_mutex_destroy(&data.lock);
	return data.wbuf;
}

int wl_create_shm_file(int index)
{
	char name[64];
	int fd;

	snprintf(name, sizeof(name), "/wl_shm-20230617-%d", index);
	fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd >= 0) {
		shm_unlink(name);
		return fd;
	}

	return -1;
}

int wl_shm_create_buffer(struct wayland_buffer_link *link)
{
	struct wayland_sink_test_t *wlc = link->wlc;
	const int width = wlc->format.width;
	const int height = wlc->format.height;
	const int format = wlc->format_pixel_2_shm(wlc->format.format);
	struct wl_shm_pool *pool;
	int ret;

	if (format == -1) {
		PITCHER_ERR("wayland sink doesn't support format %s\n",
				pitcher_get_format_name(wlc->format.format));
		return -RET_E_NOT_SUPPORT;
	}
	if (link->wbuf)
		return RET_OK;

	link->shm_fd = wl_create_shm_file(wlc->shm_index++);
	if (link->shm_fd < 0) {
		PITCHER_ERR("fail to create shm file\n");
		return -RET_E_OPEN;
	}

	ret = ftruncate(link->shm_fd, wlc->format.size);
	if (ret < 0) {
		PITCHER_ERR("fail to ftruncate\n");
		close(link->shm_fd);
		link->shm_fd = -1;
		return -RET_E_NO_MEMORY;
	}
	link->shm_virt = mmap(NULL,
			      wlc->format.size,
			      PROT_READ | PROT_WRITE,
			      MAP_SHARED,
			      link->shm_fd, 0);
	if (link->shm_virt == MAP_FAILED) {
		close(link->shm_fd);
		link->shm_fd = -1;
	}

	pool = wl_shm_create_pool(wlc->wl_shm, link->shm_fd, wlc->format.size);
	link->wbuf = wl_shm_pool_create_buffer(pool, 0, width, height, wlc->format.planes[0].line, format);
	wl_shm_pool_destroy(pool);
	if (!link->wbuf)
		return -RET_E_NO_MEMORY;

	wl_buffer_add_listener(link->wbuf, &buffer_listener, link);
	link->is_shm = true;

	return RET_OK;
}

int free_wl_buffer(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;

	if (link->is_shm) {
		if (link->shm_virt) {
			munmap(link->shm_virt, link->wlc->format.size);
			link->shm_virt = NULL;
		}
		SAFE_CLOSE(link->shm_fd, close);
	}

	SAFE_RELEASE(link->wbuf, wl_buffer_destroy);
	return 0;
}

void wayland_sink_uninit_display(struct wayland_sink_test_t *wlc)
{
	if (wlc->tid != -1) {
		struct timespec ts;

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += 3;

		if (pthread_timedjoin_np(wlc->tid, NULL, &ts)) {
			PITCHER_ERR("fail to exit display thread, kill it\n");
			pthread_kill(wlc->tid, SIGINT);
		}
		wlc->tid = -1;
	}
	pitcher_queue_enumerate(wlc->links, free_wl_buffer, NULL);
	SAFE_RELEASE(wlc->xdg_toplevel, xdg_toplevel_destroy);
	SAFE_RELEASE(wlc->xdg_surface, xdg_surface_destroy);
	SAFE_RELEASE(wlc->video_surface, wl_surface_destroy);
	SAFE_RELEASE(wlc->compositor, wl_compositor_destroy);
	SAFE_RELEASE(wlc->xdg_wm_base, xdg_wm_base_destroy);
	SAFE_RELEASE(wlc->dmabuf, zwp_linux_dmabuf_v1_destroy);
	SAFE_RELEASE(wlc->wl_shm, wl_shm_destroy);
	SAFE_RELEASE(wlc->registry, wl_registry_destroy);
	SAFE_RELEASE(wlc->display, wl_display_disconnect);
}

void wayland_sink_exit_display(struct wayland_sink_test_t *wlc)
{
	if (!wlc || !wlc->display)
		return;

	if (wlc->redraw_pending)
		pthread_cond_wait(&wlc->redraw_wait, &wlc->render_lock);

	wl_surface_attach(wlc->video_surface, NULL, 0, 0);
	wl_surface_damage(wlc->video_surface, 0, 0, wlc->node.width, wlc->node.height);
	wl_surface_commit(wlc->video_surface);
	wl_display_flush(wlc->display);
}

int free_input_buffer(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;

	pitcher_put_buffer(link->buffer);
	return 1;
}

int free_buffer_link(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;

	SAFE_RELEASE(link, pitcher_free);
	return 1;
}

int compare_buffer_link(unsigned long item, unsigned long key)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)key;

	if (link->buffer == buffer)
		return 1;
	return 0;
}

int get_buffer_link(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;
	struct wayland_buffer_link **pp = arg;

	if (pp)
		*pp = link;

	return 0;
}

struct wayland_buffer_link *find_buffer_link(struct wayland_sink_test_t *wlc,
					struct pitcher_buffer *buffer)
{
	struct wayland_buffer_link *link = NULL;

	if (!pitcher_queue_find(wlc->links, get_buffer_link, &link,
				compare_buffer_link, (unsigned long)buffer))
		return link;

	link = pitcher_calloc(1, sizeof(*link));
	if (!link)
		return NULL;

	link->buffer = buffer;
	link->wbuf = NULL;
	link->wlc = wlc;
	pitcher_queue_push_back(wlc->links, (unsigned long)link);
	return link;
}

int wayland_sink_copy_frame_to_wl_buffer(struct wayland_buffer_link *link)
{
	struct pitcher_buffer *buffer = link->buffer;
	struct v4l2_rect *crop = buffer->crop;
	struct pix_fmt_info *format = buffer->format;
	const struct pixel_format_desc *desc = buffer->format->desc;
	struct pitcher_buf_ref splane;
	int w, h, line;
	int planes_line;
	unsigned long offset;
	void *dst = link->shm_virt;
	int dst_line;

	for (int i = 0; i < format->num_planes; i++) {
		uint32_t left;
		uint32_t top;

		offset = 0;
		pitcher_get_buffer_plane(buffer, i, &splane);
		if (crop && crop->width != 0 && crop->height != 0) {
			w = crop->width;
			h = crop->height;
		} else {
			w = format->width;
			h = format->height;
		}
		w = ALIGN(w, 1 << desc->log2_chroma_w);
		h = ALIGN(h, 1 << desc->log2_chroma_h);
		if (i) {
			w >>= desc->log2_chroma_w;
			h >>= desc->log2_chroma_h;
		}
		line = ALIGN(w * desc->comp[i].bpp, 8) >> 3;
		left = crop->left;
		top = crop->top;
		if (i) {
			left >>= desc->log2_chroma_w;
			top >>= desc->log2_chroma_h;
		}

		dst_line = link->wlc->format.planes[i].line;
		planes_line = format->planes[i].line;
		offset = planes_line * top;
		for (int j = 0; j < h; j++) {
			memcpy(dst, splane.virt + offset + left, line);
			offset += planes_line;
			dst += dst_line;
		}
	}

	return 0;
}

int wayland_sink_enqueue_buffer(struct wayland_sink_test_t *wlc,
					struct pitcher_buffer *buffer)
{
	struct wayland_buffer_link *link;
	int ret;

	link = find_buffer_link(wlc, buffer);
	if (!link)
		return -RET_E_NO_MEMORY;
	if (wlc->use_shm) {
		if (!link->wbuf)
			wl_shm_create_buffer(link);
		if (!link->wbuf)
			return -RET_E_NO_MEMORY;
		wayland_sink_copy_frame_to_wl_buffer(link);
	} else if (!link->wbuf) {
		link->wbuf = wl_linux_dmabuf_construct_wl_buffer(wlc, link->buffer);
		if (!link->wbuf)
			return -RET_E_NO_MEMORY;
		wl_buffer_add_listener(link->wbuf, &buffer_listener, link);
	}

	pthread_mutex_lock(&wlc->render_lock);
	ret = pitcher_queue_push_back(wlc->queue, (unsigned long)link);
	pthread_mutex_unlock(&wlc->render_lock);

	return ret;
}

struct wayland_buffer_link *get_last_buffer(struct wayland_sink_test_t *wlc)
{
	unsigned long item;
	int ret;

	ret = pitcher_queue_pop(wlc->queue, &item);
	if (ret < 0)
		return NULL;

	return (struct wayland_buffer_link *)item;
}

int wayland_render_last_buffer(struct wayland_sink_test_t *wlc)
{
	struct wayland_buffer_link *link = get_last_buffer(wlc);
	struct wl_buffer *wl_buffer;
	struct wl_callback *frame_callback;
	uint32_t width;
	uint32_t height;

	if (!link || !link->wbuf)
		return -RET_E_INVAL;

	width = wlc->format.width;
	height = wlc->format.height;

	wl_buffer = link->wbuf;
	pitcher_end_cpu_access(link->buffer, 1, 1);

	while (wlc->redraw_pending && !is_force_exit())
		pthread_cond_wait(&wlc->redraw_wait, &wlc->render_lock);
	if (is_force_exit()) {
		pitcher_put_buffer(link->buffer);
		return RET_OK;
	}

	wlc->redraw_pending = 1;
	frame_callback = wl_surface_frame(wlc->video_surface);
	wl_callback_add_listener(frame_callback, &frame_listener, wlc);

	wl_surface_attach(wlc->video_surface, wl_buffer, 0, 0);
	wl_surface_set_buffer_scale(wlc->video_surface, 1);
	wl_surface_damage(wlc->video_surface, 0, 0, width, height);
	wl_surface_commit(wlc->video_surface);
	wl_display_flush(wlc->display);

	if (link->buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		wlc->end = true;
	atomic_inc(&wlc->buffer_count);
	wlc->disp_count++;

	return RET_OK;
}

void *wl_thread_run(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;
	int ret;

	while (!wlc->end || wlc->disp_count < wlc->frame_count) {
		pthread_mutex_lock(&wlc->render_lock);
		ret = wayland_render_last_buffer(wlc);
		pthread_mutex_unlock(&wlc->render_lock);
		if (ret)
			usleep(100);
		if (is_force_exit() && wlc->end)
			break;
	}

	wlc->done = true;
	PITCHER_LOG("wl thread done, frame_count = %ld, disp_count = %ld\n",
			wlc->frame_count, wlc->disp_count);
	return NULL;
}

int wayland_sink_start(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;
	int ret;

	if (!wlc)
		return -RET_E_INVAL;

	if (pitcher_is_error(wlc->chnno)) {
		PITCHER_ERR("there is already some error in wayland sink\n");
		return -RET_E_INVAL;
	}

	wlc->end = false;
	wlc->done = false;
	wlc->queue = pitcher_init_queue();
	if (!wlc->queue)
		return -RET_E_NO_MEMORY;

	wlc->links = pitcher_init_queue();
	if (!wlc->links)
		goto error;

	wlc->display = wl_display_connect(NULL);
	if (!wlc->display) {
		PITCHER_ERR("wayland connect display fail\n");
		goto error;
	}
	wlc->registry = wl_display_get_registry(wlc->display);
	wl_registry_add_listener(wlc->registry, &registry_listener, wlc);
	wl_display_dispatch(wlc->display);
	wl_display_roundtrip(wlc->display);
	if (!wlc->compositor || !wlc->dmabuf || !wlc->xdg_wm_base)
		goto error;
	if (wlc->use_shm) {
		if (wlc->format_pixel_2_shm(wlc->format.format) == -1) {
			PITCHER_ERR("wayland sink doesn't support format %s\n",
				    pitcher_get_format_name(wlc->format.format));
			goto error;
		}
	} else if (wlc->format_pixel_2_wl(wlc->format.format) == DRM_FORMAT_INVALID) {
		PITCHER_ERR("wayland sink doesn't support format %s\n",
				pitcher_get_format_name(wlc->format.format));
		goto error;
	}

	wlc->video_surface = wl_compositor_create_surface(wlc->compositor);
	if (!wlc->video_surface)
		goto error;

	ret = pthread_create(&wlc->pid, NULL, wl_thread_run, wlc);
	if (ret)
		goto error;

	if (wlc->xdg_wm_base) {
		wlc->xdg_surface = xdg_wm_base_get_xdg_surface(wlc->xdg_wm_base, wlc->video_surface);
		if (!wlc->xdg_surface)
			goto error;
		xdg_surface_add_listener(wlc->xdg_surface, &xdg_surface_listener, wlc);
		wlc->xdg_toplevel = xdg_surface_get_toplevel(wlc->xdg_surface);
		xdg_toplevel_set_title(wlc->xdg_toplevel, "mxc_v4l2_vpu_test");
		wl_surface_commit(wlc->video_surface);
	}

	ret = pthread_create(&wlc->tid, NULL, wl_display_thread_run, wlc);
	if (ret)
		goto error;

	if (wlc->fps)
		wlc->interval = NSEC_PER_SEC / wlc->fps;
	if (wlc->interval)
		wlc->tv_next = pitcher_get_realtime_time() + wlc->interval;

	return RET_OK;
error:
	pitcher_set_error(wlc->chnno);
	wlc->end = true;
	if (wlc->pid != -1) {
		pthread_join(wlc->pid, NULL);
		wlc->pid = -1;
	}
	wayland_sink_uninit_display(wlc);
	SAFE_RELEASE(wlc->links, pitcher_destroy_queue);
	SAFE_RELEASE(wlc->queue, pitcher_destroy_queue);
	return -RET_E_INVAL;
}

int wayland_sink_checkready(void *arg, int *is_end)
{
	struct wayland_sink_test_t *wlc = arg;

	if (!wlc)
		return -RET_E_INVAL;

	if (is_force_exit())
		wlc->end = true;
	if (is_source_end(wlc->chnno) && !pitcher_chn_poll_input(wlc->chnno))
		wlc->end = true;
	if (pitcher_is_error(wlc->chnno))
		wlc->end = true;
	if (is_end)
		*is_end = wlc->end;

	if (!pitcher_chn_poll_input(wlc->chnno))
		return false;

	if (wlc->interval) {
		uint64_t tv = pitcher_get_realtime_time();

		if (tv < wlc->tv_next)
			return false;
		wlc->tv_next += wlc->interval;
		return true;
	}

	return true;
}

int wayland_sink_run(void *arg, struct pitcher_buffer *buffer)
{
	struct wayland_sink_test_t *wlc = arg;
	struct v4l2_rect *crop;
	int ret;

	if (!wlc)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;

	crop = buffer->crop;
	if (crop && crop->width && crop->height &&
	    (crop->width != wlc->format.width || crop->height != wlc->format.height)) {
		PITCHER_LOG("update display size %d x %d\n", crop->width, crop->height);

		memset(&wlc->format, 0, sizeof(wlc->format));
		wlc->format.format = wlc->node.pixelformat;
		wlc->format.width = wlc->node.width = crop->width;
		wlc->format.height = wlc->node.height = crop->height;
		pitcher_get_pix_fmt_info(&wlc->format, 0);
	}

	if (pitcher_buffer_is_dma_buf(buffer) && !wlc->use_shm) {
		PITCHER_ERR("wayland sink only support dma buffer\n");
		return -RET_E_NOT_SUPPORT;
	}
	if (buffer->planes[0].bytesused == 0) {
		wlc->end = true;
		return RET_OK;
	}

	if (!buffer->format)
		buffer->format = &wlc->format;

	ret = wayland_sink_enqueue_buffer(wlc, pitcher_get_buffer(buffer));
	if (ret) {
		pitcher_put_buffer(buffer);
		return ret;
	}
	wlc->frame_count++;

	return RET_OK;
}

int wayland_sink_stop(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;

	if (!wlc)
		return -RET_E_INVAL;

	if (wlc->pid != -1) {
		pthread_mutex_lock(&wlc->render_lock);
		if (wlc->redraw_pending) {
			wlc->redraw_pending = 0;
			pthread_cond_signal(&wlc->redraw_wait);
		}
		pthread_mutex_unlock(&wlc->render_lock);
		pthread_join(wlc->pid, NULL);
		wlc->pid = -1;
	}
	pthread_mutex_lock(&wlc->render_lock);
	wayland_sink_exit_display(wlc);
	wayland_sink_uninit_display(wlc);
	pthread_mutex_unlock(&wlc->render_lock);

	pitcher_queue_enumerate(wlc->queue, free_input_buffer, NULL);
	SAFE_RELEASE(wlc->queue, pitcher_destroy_queue);
	pitcher_queue_enumerate(wlc->links, free_buffer_link, NULL);
	SAFE_RELEASE(wlc->links, pitcher_destroy_queue);

	return 0;
}

int init_wayland_sink_node(struct test_node *node)
{
	struct wayland_sink_test_t *wlc;

	if (!node)
		return -RET_E_NULL_POINTER;

	wlc = container_of(node, struct wayland_sink_test_t, node);
	wlc->desc.fd = -1;
	wlc->desc.start = wayland_sink_start;
	wlc->desc.check_ready = wayland_sink_checkready;
	wlc->desc.runfunc = wayland_sink_run;
	wlc->desc.stop = wayland_sink_stop;
	snprintf(wlc->desc.name, sizeof(wlc->desc.name), "waylandsink.%d",
			wlc->node.key);
	wlc->tid = -1;
	pthread_cond_init(&wlc->redraw_wait, NULL);
	pthread_mutex_init(&wlc->render_lock, NULL);
	wlc->format_pixel_2_shm = pixel_foramt_to_wl_shm_format;
	wlc->format_pixel_2_wl = pixel_foramt_to_wl_dmabuf_format;
	wlc->format_wl_2_pixel = wl_dmabuf_format_to_pixel_format;

	return RET_OK;
}

void free_wayland_sink_node(struct test_node *node)
{
	struct wayland_sink_test_t *wlc;

	if (!node)
		return;

	wlc = container_of(node, struct wayland_sink_test_t, node);
	PITCHER_LOG("wayland sink frame count : %ld, display : %ld\n",
			wlc->frame_count, wlc->disp_count);
	pthread_mutex_destroy(&wlc->render_lock);
	pthread_cond_destroy(&wlc->redraw_wait);
	SAFE_RELEASE(wlc, pitcher_free);
}

int set_wayland_sink_source(struct test_node *node, struct test_node *src)
{
	struct wayland_sink_test_t *wlc;

	if (!node || !src)
		return -RET_E_INVAL;

	wlc = container_of(node, struct wayland_sink_test_t, node);
	wlc->node.width = ALIGN(src->width, 2);
	wlc->node.height = ALIGN(src->height, 2);
	wlc->node.pixelformat = src->pixelformat;

	memset(&wlc->format, 0, sizeof(wlc->format));
	wlc->format.format = wlc->node.pixelformat;
	wlc->format.width = wlc->node.width;
	wlc->format.height = wlc->node.height;
	pitcher_get_pix_fmt_info(&wlc->format, 0);

	return RET_OK;
}

int get_wayland_sink_chnno(struct test_node *node)
{
	struct wayland_sink_test_t *wlc;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	wlc = container_of(node, struct wayland_sink_test_t, node);
	if (wlc->chnno >= 0)
		return wlc->chnno;

	src_node = get_test_node(wlc->node.source);
	if (src_node->get_source_chnno(src_node) < 0)
		return wlc->chnno;

	wlc->chnno = pitcher_register_chn(wlc->node.context, &wlc->desc, wlc);
	return wlc->chnno;
}

int parse_wayland_sink_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct wayland_sink_test_t *wlc;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	wlc = container_of(node, struct wayland_sink_test_t, node);

	if (!strcasecmp(option->name, "key")) {
		wlc->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		wlc->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "framerate")) {
		wlc->fps = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "interval")) {
		wlc->interval = (uint64_t)strtol(argv[0], NULL, 0) * NSEC_PER_MSEC;
	} else if (!strcasecmp(option->name, "shm")) {
		wlc->use_shm = strtol(argv[0], NULL, 0) ? true: false;
	}

	return RET_OK;
}

struct test_node *alloc_wayland_sink_node(void)
{
	struct wayland_sink_test_t *wlc;

	wlc = pitcher_calloc(1, sizeof(*wlc));
	if (!wlc)
		return NULL;

	wlc->node.key = -1;
	wlc->node.source = -1;
	wlc->node.type = TEST_TYPE_SINK;
	wlc->chnno = -1;

	wlc->node.pixelformat = PIX_FMT_NV12;
	wlc->node.init_node = init_wayland_sink_node;
	wlc->node.free_node = free_wayland_sink_node;
	wlc->node.get_sink_chnno = get_wayland_sink_chnno;
	wlc->node.set_source = set_wayland_sink_source;

	wlc->pid = -1;
	wlc->tid = -1;

	return &wlc->node;
}
#endif
