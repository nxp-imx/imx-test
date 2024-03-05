/*
 * Copyright(c) 2024 NXP. All rights reserved.
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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/loop.h"
#include "pitcher/queue.h"
#include "mxc_v4l2_vpu_enc.h"

struct filter_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	struct pix_fmt_info format;
	int end;
	unsigned long frame_count;
	uint32_t sizeimage;

	Queue buffers;
	uint32_t framerate;

	const char *name;
	int (*filter_func)(struct filter_test_t *filter, struct pitcher_buffer *pbuf);

	/*fps*/
	struct pitcher_timer_task task;
	uint64_t tv_start;
	uint64_t tv_end;
	uint64_t interval;
	uint64_t deviation;
	uint64_t result_fps;
	unsigned long frame_index;

	/*corrupt*/
	struct {
		uint8_t type;
		uint8_t mask;
		uint32_t count;
	} corrupt;

	uint32_t rate;
};

struct mxc_vpu_test_option filter_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"function", 1, "--function <func>\n\t\t\t set filter function ('fps', 'corrupt', 'drop')"},
	{"framerate", 1, "--framerate <f>\n\t\t\tset frame rate(fps)"},
	{"bs", 1, "--bs <bs count>\n\t\t\tSpecify the buffer size, the unit is Kb."},
	{"type", 1, "--type <corrupt type>\n\t\t\tCorrupt buffer data, 0:and, 1:or, 2:not"},
	{"mask", 1, "--mask <corrupt mask>\n\t\t\tCorrupt mask"},
	{"rate", 1, "--rate\n\t\t\tCorrupt rate, the unit is %"},
	{"count", 1, "--count <corrupt byte count>\n\t\t\tCorrupt byte count limit, default is 1"},
	{NULL, 0, NULL},
};

int recycle_filter_buffer(struct pitcher_buffer *buffer, void *arg, int *del)
{
	struct filter_test_t *filter = arg;
	int is_end = false;

	if (!filter)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(filter->chnno) && !filter->end)
		pitcher_put_buffer_idle(filter->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

static int __clear_filter_buffer(unsigned long item, void *arg)
{
	struct pitcher_buffer *buffer = (struct pitcher_buffer *)item;

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return 1;
}

struct pitcher_buffer *alloc_filter_buffer(void *arg)
{
	struct filter_test_t *filter = arg;
	struct pitcher_buffer_desc desc;

	if (!filter)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size[0] = filter->format.size;
	desc.init_plane = pitcher_alloc_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.recycle = recycle_filter_buffer;
	desc.arg = filter;

	return pitcher_new_buffer(&desc);
}

struct pitcher_buffer *get_filter_buffer(struct filter_test_t *filter)
{
	struct pitcher_buffer *buffer;

	if (!filter)
		return NULL;

	buffer = pitcher_get_idle_buffer(filter->chnno);
	if (buffer)
		return buffer;

	buffer = alloc_filter_buffer(filter);
	if (!buffer)
		return NULL;

	filter->desc.buffer_count++;
	return buffer;
}

struct pitcher_buffer *pop_filter_buffer(struct filter_test_t *filter)
{
	unsigned long item;
	int ret;

	ret = pitcher_queue_pop(filter->buffers, &item);
	if (ret < 0)
		return NULL;

	return (struct pitcher_buffer *)item;
}

int filter_fps_timer_func(struct pitcher_timer_task *task, int *del)
{
	struct filter_test_t *filter = container_of(task, struct filter_test_t, task);
	struct pitcher_buffer *buffer = NULL;
	uint64_t tv;

	if (filter->end && pitcher_queue_is_empty(filter->buffers)) {
		if (filter->frame_count > 1) {
			uint64_t delta = filter->tv_end - filter->tv_start;

			filter->result_fps = (filter->frame_count -1) * 1000000000 * 1000 / delta;
		}
		*del = true;
		return RET_OK;
	}

	tv = pitcher_get_monotonic_raw_time();
	if (tv + filter->deviation < filter->tv_start + filter->interval * filter->frame_index)
		return RET_OK;

	buffer = pop_filter_buffer(filter);
	if (!buffer)
		return -RET_E_EMPTY;
	pitcher_push_back_output(filter->chnno, buffer);
	SAFE_RELEASE(buffer, pitcher_put_buffer);
	filter->tv_end = tv;
	filter->frame_index++;

	return RET_OK;
}

int filter_fps_func(struct filter_test_t *filter, struct pitcher_buffer *pbuf)
{
	int ret = RET_OK;

	if (filter->frame_count == 1) {
		pitcher_push_back_output(filter->chnno, pbuf);

		filter->task.func = filter_fps_timer_func;
		filter->task.interval = 1;
		filter->task.times = -1;
		pitcher_loop_add_task(pitcher_get_main_loop(filter->node.context), &filter->task);

		filter->interval = 1000000000 / filter->framerate;
		filter->tv_start = pitcher_get_monotonic_raw_time();
		filter->tv_end = filter->tv_start;
		filter->frame_index++;
		filter->deviation = 500000;

		return RET_OK;
	}

	pitcher_get_buffer(pbuf);
	pitcher_queue_push_back(filter->buffers, (unsigned long)pbuf);

	return ret;
}

static int filter_common_func(struct filter_test_t *filter, struct pitcher_buffer *pbuf,
			      int (*procecc_cb)(struct filter_test_t *, struct pitcher_buffer *))
{
	struct pitcher_buffer *pdst;

	pdst = get_filter_buffer(filter);
	if (!pdst)
		return -RET_E_EMPTY;

	pdst->format = pbuf->format;
	if (pitcher_copy_buffer_data(pbuf, pdst)) {
		unsigned long size = 0;

		for (unsigned int i = 0; i < pbuf->count; i++) {
			memcpy(pdst->planes[0].virt + size,
			       pbuf->planes[i].virt,
			       pbuf->planes[i].bytesused);
			size += pbuf->planes[i].bytesused;
		}
		pdst->planes[0].bytesused = size;
	}

	if (procecc_cb)
		procecc_cb(filter, pdst);

	pitcher_push_back_output(filter->chnno, pdst);
	SAFE_RELEASE(pdst, pitcher_put_buffer);

	return RET_OK;
}

static int filter_corrupt_buffer(struct filter_test_t *filter, struct pitcher_buffer *buffer)
{
	uint32_t r = rand() % 100;
	uint32_t count;
	uint8_t *pdata = NULL;

	if (!filter->corrupt.count)
		return RET_OK;

	if (r >= filter->rate)
		return RET_OK;
	if (filter->corrupt.type > 2)
		return RET_OK;

	pdata = (uint8_t *)buffer->planes[0].virt;
	count = (rand() % filter->corrupt.count) + 1;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t offset = rand() % buffer->planes[0].bytesused;

		switch (filter->corrupt.type) {
		case 0:
			pdata[offset] &= filter->corrupt.mask;
			break;
		case 1:
			pdata[offset] |= filter->corrupt.mask;
			break;
		case 2:
			pdata[offset] = ~pdata[offset];
			break;
		}
	}

	return RET_OK;
}

int filter_corrupt_func(struct filter_test_t *filter, struct pitcher_buffer *pbuf)
{
	return filter_common_func(filter, pbuf, filter_corrupt_buffer);
}

int filter_drop_func(struct filter_test_t *filter, struct pitcher_buffer *pbuf)
{
	uint32_t r = rand() % 100;

	if (r < filter->rate)
		return RET_OK;

	return filter_common_func(filter, pbuf, NULL);
}

void init_filter_function(struct filter_test_t *filter)
{
	if (!filter || !filter->name)
		return;

	if (!strcasecmp(filter->name, "fps") && filter->framerate)
		filter->filter_func = filter_fps_func;
	else if (!strcasecmp(filter->name, "corrupt"))
		filter->filter_func = filter_corrupt_func;
	else if (!strcasecmp(filter->name, "drop"))
		filter->filter_func = filter_drop_func;
}

int start_filter(void *arg)
{
	struct filter_test_t *filter = arg;

	if (!filter)
		return -RET_E_NULL_POINTER;

	filter->frame_count = 0;
	filter->frame_index = 0;
	filter->end = false;
	return RET_OK;
}

int stop_filter(void *arg)
{
	struct filter_test_t *filter = arg;

	if (!filter)
		return -RET_E_NULL_POINTER;

	pitcher_queue_clear(filter->buffers, __clear_filter_buffer, NULL);

	return RET_OK;
}

int checkready_filter(void *arg, int *is_end)
{
	struct filter_test_t *filter = arg;

	if (!filter)
		return false;

	if (is_force_exit())
		filter->end = true;
	if (is_source_end(filter->chnno) && !pitcher_chn_poll_input(filter->chnno) &&
	    pitcher_queue_is_empty(filter->buffers))
		filter->end = true;
	if (is_end)
		*is_end = filter->end;

	if (!pitcher_chn_poll_input(filter->chnno))
		return false;
	if (filter->desc.buffer_count >= 10 && !pitcher_poll_idle_buffer(filter->chnno))
		return false;

	return true;
}

int filter_runfunc(void *arg, struct pitcher_buffer *buffer)
{
	struct filter_test_t *filter = arg;
	int ret = RET_OK;

	if (!filter || !buffer)
		return -RET_E_INVAL;

	if (buffer->planes[0].bytesused == 0)
		return 0;

	if (!filter->format.size) {
		if (buffer->format) {
			filter->format = *buffer->format;
		} else {
			unsigned long size = 0;

			for (unsigned int i = 0; i < buffer->count; i++)
				size += buffer->planes[i].size;
			filter->format.size = max(size, filter->sizeimage);
		}
	}

	filter->frame_count++;
	if (filter->filter_func) {
		ret = filter->filter_func(filter, buffer);
	} else {
		pitcher_push_back_output(filter->chnno, buffer);
	}

	return ret;
}

int init_filter_node(struct test_node *node)
{
	struct filter_test_t *filter;

	if (!node)
		return -RET_E_NULL_POINTER;

	filter = container_of(node, struct filter_test_t, node);
	filter->desc.fd = -1;
	filter->desc.start = start_filter;
	filter->desc.stop = stop_filter;
	filter->desc.check_ready = checkready_filter;
	filter->desc.runfunc = filter_runfunc;
	if (filter->format.size) {
		filter->desc.buffer_count = 4;
		filter->desc.alloc_buffer = alloc_filter_buffer;
	}
	snprintf(filter->desc.name, sizeof(filter->desc.name),
		 "filter.%s.%d", filter->name, node->key);
	init_filter_function(filter);
	filter->buffers = pitcher_init_queue();

	return RET_OK;
}

void free_filter_node(struct test_node *node)
{
	struct filter_test_t *filter;

	if (!node)
		return;

	filter = container_of(node, struct filter_test_t, node);
	PITCHER_LOG("%s frame count %ld, fps %ld.%ld\n", filter->desc.name, filter->frame_count,
		    filter->result_fps / 1000, filter->result_fps % 1000);
	pitcher_queue_clear(filter->buffers, __clear_filter_buffer, NULL);
	SAFE_RELEASE(filter->buffers, pitcher_destroy_queue);
	SAFE_RELEASE(filter, pitcher_free);
}

int get_filter_chnno(struct test_node *node)
{
	struct filter_test_t *filter;
	struct test_node *src;

	if (!node)
		return -RET_E_NULL_POINTER;

	filter = container_of(node, struct filter_test_t, node);
	if (filter->chnno >= 0)
		return filter->chnno;

	src = get_test_node(node->source);
	if (!src || src->get_source_chnno(src) < 0)
		return filter->chnno;

	filter->chnno = pitcher_register_chn(filter->node.context, &filter->desc, filter);
	if (filter->chnno >= 0 && filter->format.desc && filter->format.desc->fourcc_nc)
		pitcher_set_preferred_fourcc(filter->chnno, filter->format.desc->fourcc);

	return filter->chnno;
}

int set_filter_source(struct test_node *node, struct test_node *src)
{
	struct filter_test_t *filter;

	if (!node || !src)
		return -RET_E_NULL_POINTER;

	filter = container_of(node, struct filter_test_t, node);
	filter->node.width = src->width;
	filter->node.height = src->height;
	filter->node.pixelformat = src->pixelformat;

	memset(&filter->format, 0, sizeof(filter->format));
	filter->format.format = filter->node.pixelformat;
	filter->format.width = filter->node.width;
	filter->format.height = filter->node.height;
	pitcher_get_pix_fmt_info(&filter->format, 0);
	if (filter->chnno >= 0 && filter->format.desc && filter->format.desc->fourcc_nc)
		pitcher_set_preferred_fourcc(filter->chnno, filter->format.desc->fourcc);

	return RET_OK;
}

int parse_filter_option(struct test_node *node, struct mxc_vpu_test_option *option, char *argv[])
{
	struct filter_test_t *filter;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	filter = container_of(node, struct filter_test_t, node);
	filter->sizeimage = 2 * 1024 * 1024;
	filter->corrupt.count = 1;

	if (!strcasecmp(option->name, "key")) {
		filter->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		filter->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "function")) {
		filter->name = argv[0];
	} else if (!strcasecmp(option->name, "framerate")) {
		filter->framerate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "bs")) {
		uint32_t bs = strtol(argv[0], NULL, 0);

		filter->sizeimage = max(bs, 128) * 1024;
	} else if (!strcasecmp(option->name, "type")) {
		filter->corrupt.type = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "mask")) {
		filter->corrupt.mask = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "rate")) {
		filter->rate = strtol(argv[0], NULL, 0);
		filter->rate = min(filter->rate, 100);
	} else if (!strcasecmp(option->name, "count")) {
		filter->corrupt.count = strtol(argv[0], NULL, 0);
	}

	return RET_OK;
}

struct test_node *alloc_filter(void)
{
	struct filter_test_t *filter;

	filter = pitcher_calloc(1, sizeof(*filter));
	if (!filter)
		return NULL;

	filter->node.key = -1;
	filter->node.source = -1;
	filter->node.type = TEST_TYPE_FILTER;
	filter->chnno = -1;

	filter->node.init_node = init_filter_node;
	filter->node.free_node = free_filter_node;
	filter->node.get_source_chnno = get_filter_chnno;
	filter->node.get_sink_chnno = get_filter_chnno;
	filter->node.set_source = set_filter_source;

	return &filter->node;
}
