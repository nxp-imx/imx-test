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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
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
	struct wl_surface *video_surface_wrapper;
	struct wl_shell *shell;
	struct wl_shell_surface *shell_surface;
	struct zwp_linux_dmabuf_v1 *dmabuf;

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
};

static struct wl_videoformat wl_formats[] = {
	{WL_SHM_FORMAT_YUYV,   DRM_FORMAT_YUYV,   PIX_FMT_YUYV,    0},
	{WL_SHM_FORMAT_NV12,   DRM_FORMAT_NV12,   PIX_FMT_NV12,    0},
	{WL_SHM_FORMAT_YUV420, DRM_FORMAT_YUV420, PIX_FMT_I420,  0},
};

static uint32_t pixel_foramt_to_wl_dmabuf_format(uint32_t format)
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

static uint32_t wl_dmabuf_format_to_pixel_format(uint32_t dma_format)
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
	{NULL, 0, NULL},
};

static void dmabuf_format (void *data,
		struct zwp_linux_dmabuf_v1 *zwp_linux_dmabuf,
		uint32_t format)
{
}


static void dmabuf_modifier (void *data,
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

static void global_registry_handler(void *data, struct wl_registry *registry,
		uint32_t id, const char *interface, uint32_t version)
{
	struct wayland_sink_test_t *wlc = data;

	/*PITCHER_LOG("interface : %s\n", interface);*/
	if (strcmp(interface, "wl_compositor") == 0) {
		wlc->compositor = wl_registry_bind(registry,
				id,
				&wl_compositor_interface,
				min(version, 3));
	} else if (strcmp(interface, "wl_shell") == 0) {
		wlc->shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);
	} else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		wlc->dmabuf = wl_registry_bind(registry,
				id,
				&zwp_linux_dmabuf_v1_interface,
				version);
		zwp_linux_dmabuf_v1_add_listener(wlc->dmabuf, &dmabuf_listener, wlc);
	}
}

static void global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
}

static const struct wl_registry_listener registry_listener = {
	global_registry_handler,
	global_registry_remover
};

static void handle_ping(void *data,
		struct wl_shell_surface *shell_surface, uint32_t serial)
{
}

static void handle_configure(void *data,
		struct wl_shell_surface *shell_surface,
		uint32_t edges, int32_t width, int32_t height)
{
}

static void handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
	handle_ping,
	handle_configure,
	handle_popup_done
};

static void redraw(void *data, struct wl_callback *callback, uint32_t time)
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

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct wayland_buffer_link *link = data;

	pitcher_start_cpu_access(link->buffer, 1, 1);
	pitcher_put_buffer(link->buffer);
	atomic_dec(&link->wlc->buffer_count);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static void create_succeeded(void *data,
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

static void create_failed (void *data,
		struct zwp_linux_buffer_params_v1 *params)
{
	create_succeeded(data, params, NULL);
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
	create_succeeded,
	create_failed
};

static void *wl_display_thread_run(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;

	while (wl_display_dispatch(wlc->display) != -1) {
		if (wlc->end && wlc->done && !wlc->buffer_count)
			break;
	}

	PITCHER_LOG("wayland display thread done\n");
	return NULL;
}

static struct wl_buffer *wl_linux_dmabuf_construct_wl_buffer(
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
			buffer->format->width,
			buffer->format->height,
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

static void wayland_sink_uninit_display(struct wayland_sink_test_t *wlc)
{
	if (wlc->tid >= 0) {
		pthread_join(wlc->tid, NULL);
		wlc->tid = -1;
	}
	SAFE_RELEASE(wlc->shell_surface, wl_shell_surface_destroy);
	SAFE_RELEASE(wlc->video_surface_wrapper, wl_proxy_wrapper_destroy);
	SAFE_RELEASE(wlc->video_surface, wl_surface_destroy);
	SAFE_RELEASE(wlc->compositor, wl_compositor_destroy);
	SAFE_RELEASE(wlc->dmabuf, zwp_linux_dmabuf_v1_destroy);
	SAFE_RELEASE(wlc->registry, wl_registry_destroy);
	SAFE_RELEASE(wlc->display, wl_display_disconnect);
}

static void wayland_sink_exit_display(struct wayland_sink_test_t *wlc)
{
	struct wl_callback *frame_callback;

	if (!wlc || !wlc->display)
		return;

	if (wlc->redraw_pending)
		pthread_cond_wait(&wlc->redraw_wait, &wlc->render_lock);

	wl_surface_attach(wlc->video_surface_wrapper, NULL, 0, 0);
	frame_callback = wl_surface_frame(wlc->video_surface_wrapper);
	wl_callback_add_listener(frame_callback, &frame_listener, wlc);
	wl_surface_damage(wlc->video_surface_wrapper, 0 ,0 , wlc->node.width, wlc->node.height);
	wl_surface_commit(wlc->video_surface_wrapper);
	wl_display_flush(wlc->display);
}

static int free_buffer_link(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;

	SAFE_RELEASE(link, pitcher_free);
	return 1;
}

static int compare_buffer_link(unsigned long item, unsigned long key)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)key;

	if (link->buffer == buffer)
		return 1;
	return 0;
}

static int get_buffer_link(unsigned long item, void *arg)
{
	struct wayland_buffer_link *link = (struct wayland_buffer_link *)item;
	struct wayland_buffer_link **pp = arg;

	if (pp)
		*pp = link;

	return 0;
}

static struct wayland_buffer_link *find_buffer_link(struct wayland_sink_test_t *wlc,
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

static int wayland_sink_enqueue_buffer(struct wayland_sink_test_t *wlc,
					struct pitcher_buffer *buffer)
{
	struct wayland_buffer_link *link;
	int ret;

	link = find_buffer_link(wlc, buffer);
	if (!link)
		return -RET_E_NO_MEMORY;
	if (!link->wbuf) {
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

static struct wayland_buffer_link *get_last_buffer(struct wayland_sink_test_t *wlc)
{
	unsigned long item;
	int ret;

	ret = pitcher_queue_pop(wlc->queue, &item);
	if (ret < 0)
		return NULL;

	return (struct wayland_buffer_link *)item;
}

static int wayland_render_last_buffer(struct wayland_sink_test_t *wlc)
{
	struct wayland_buffer_link *link = get_last_buffer(wlc);
	struct wl_buffer *wl_buffer;
	struct wl_callback *frame_callback;
	uint32_t width;
	uint32_t height;

	if (!link || !link->wbuf)
		return -RET_E_INVAL;

	if (link->buffer && link->buffer->format) {
		width = link->buffer->format->width;
		height = link->buffer->format->height;
	} else {
		width = wlc->node.width;
		height = wlc->node.height;
	}

	wl_buffer = link->wbuf;
	pitcher_end_cpu_access(link->buffer, 1, 1);

	while (wlc->redraw_pending)
		pthread_cond_wait(&wlc->redraw_wait, &wlc->render_lock);

	wlc->redraw_pending = 1;
	frame_callback = wl_surface_frame(wlc->video_surface_wrapper);
	wl_callback_add_listener(frame_callback, &frame_listener, wlc);

	wl_surface_attach(wlc->video_surface_wrapper, wl_buffer, 0, 0);
	wl_surface_set_buffer_scale(wlc->video_surface_wrapper, 1);
	wl_surface_damage(wlc->video_surface_wrapper, 0 ,0 , width, height);
	wl_surface_commit(wlc->video_surface_wrapper);
	wl_display_flush(wlc->display);

	/*
	 *if (wlc->redraw_pending)
	 *        pthread_cond_wait(&wlc->redraw_wait, &wlc->render_lock);
	 */

	if (link->buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		wlc->end = true;
	atomic_inc(&wlc->buffer_count);
	wlc->disp_count++;

	return RET_OK;
}

static void *wl_thread_run(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;
	int ret;

	while (!wlc->end || wlc->disp_count < wlc->frame_count) {
		pthread_mutex_lock(&wlc->render_lock);
		ret = wayland_render_last_buffer(wlc);
		pthread_mutex_unlock(&wlc->render_lock);
		if (ret)
			usleep(100);
	}

	wlc->done = true;
	PITCHER_LOG("wl thread done, frame_count = %ld, disp_count = %ld\n",
			wlc->frame_count, wlc->disp_count);
	return NULL;
}

static int wayland_sink_start(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;
	int ret;

	if (!wlc)
		return -RET_E_INVAL;

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
	if (!wlc->compositor || !wlc->shell || !wlc->dmabuf)
		goto error;

	ret = pthread_create(&wlc->pid, NULL, wl_thread_run, wlc);
	if (ret)
		goto error;

	ret = pthread_create(&wlc->tid, NULL, wl_display_thread_run, wlc);
	if (ret)
		goto error;

	wlc->video_surface = wl_compositor_create_surface(wlc->compositor);
	if (!wlc->video_surface)
		goto error;
	wlc->video_surface_wrapper = wl_proxy_create_wrapper(wlc->video_surface);
	if (!wlc->video_surface_wrapper)
		goto error;

	wlc->shell_surface = wl_shell_get_shell_surface(wlc->shell, wlc->video_surface);
	if (!wlc->shell_surface)
		goto error;

	wl_shell_surface_set_toplevel(wlc->shell_surface);
	wl_shell_surface_add_listener(wlc->shell_surface, &shell_surface_listener, NULL);

	if (wlc->fps)
		wlc->interval = NSEC_PER_SEC / wlc->fps;
	if (wlc->interval)
		wlc->tv_next = pitcher_get_realtime_time() + wlc->interval;

	return RET_OK;
error:
	wlc->end = true;
	if (wlc->pid >= 0) {
		pthread_join(wlc->pid, NULL);
		wlc->pid = -1;
	}
	wayland_sink_uninit_display(wlc);
	SAFE_RELEASE(wlc->links, pitcher_destroy_queue);
	SAFE_RELEASE(wlc->queue, pitcher_destroy_queue);
	return -RET_E_INVAL;
}

static int wayland_sink_checkready(void *arg, int *is_end)
{
	struct wayland_sink_test_t *wlc = arg;

	if (!wlc)
		return -RET_E_INVAL;

	if (is_force_exit())
		wlc->end = true;
	if (is_source_end(wlc->chnno) && !pitcher_chn_poll_input(wlc->chnno))
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

static int wayland_sink_run(void *arg, struct pitcher_buffer *buffer)
{
	struct wayland_sink_test_t *wlc = arg;
	int ret;

	if (!wlc)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;
	if (pitcher_buffer_is_dma_buf(buffer)) {
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

static int wayland_sink_stop(void *arg)
{
	struct wayland_sink_test_t *wlc = arg;

	if (!wlc)
		return -RET_E_INVAL;

	if (wlc->pid >= 0) {
		pthread_join(wlc->pid, NULL);
		wlc->pid = -1;
	}
	pthread_mutex_lock(&wlc->render_lock);
	wayland_sink_exit_display(wlc);
	wayland_sink_uninit_display(wlc);
	pthread_mutex_unlock(&wlc->render_lock);

	SAFE_RELEASE(wlc->queue, pitcher_destroy_queue);
	pitcher_queue_enumerate(wlc->links, free_buffer_link, NULL);
	SAFE_RELEASE(wlc->links, pitcher_destroy_queue);

	return 0;
}

static int init_wayland_sink_node(struct test_node *node)
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
	wlc->format_pixel_2_wl = pixel_foramt_to_wl_dmabuf_format;
	wlc->format_wl_2_pixel = wl_dmabuf_format_to_pixel_format;

	return RET_OK;
}

static void free_wayland_sink_node(struct test_node *node)
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

static int set_wayland_sink_source(struct test_node *node, struct test_node *src)
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

static int get_wayland_sink_chnno(struct test_node *node)
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

	return &wlc->node;
}
#endif
