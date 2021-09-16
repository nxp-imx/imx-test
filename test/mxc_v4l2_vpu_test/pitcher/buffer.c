/*
 * Copyright 2018-2021 NXP
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
/*
 * core/buffer.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "obj.h"

struct ext_buffer {
	struct pitcher_buffer buffer;
	struct pitcher_obj obj;
	handle_plane init_plane;
	handle_plane uninit_plane;
	handle_buffer recycle;
	void *arg;
	struct pitcher_buf_ref planes[0];
};

int pitcher_alloc_plane(struct pitcher_buf_ref *plane,
			unsigned int index, void *arg)
{
	assert(plane);

	if (!plane->size)
		return -RET_E_INVAL;

	plane->virt = pitcher_calloc(1, plane->size);
	if (!plane->virt)
		return -RET_E_NO_MEMORY;

	return RET_OK;
}

int pitcher_free_plane(struct pitcher_buf_ref *plane,
			unsigned int index, void *arg)
{
	if (!plane)
		return RET_OK;

	SAFE_RELEASE(plane->virt, pitcher_free);
	return RET_OK;
}

static void __release_buffer(struct pitcher_obj *obj)
{
	struct ext_buffer *exb = NULL;
	int is_del = 0;
	unsigned int i;

	if (!obj)
		return;

	exb = container_of(obj, struct ext_buffer, obj);

	exb->buffer.flags = 0;
	if (exb->recycle)
		exb->recycle(&exb->buffer, exb->arg, &is_del);
	if (!is_del)
		return;

	for (i = 0; i < exb->buffer.count; i++)
		exb->uninit_plane(&exb->buffer.planes[i], i, exb->arg);

	pitcher_release_obj(&exb->obj);
	SAFE_RELEASE(exb, pitcher_free);
}

struct pitcher_buffer *pitcher_new_buffer(struct pitcher_buffer_desc *desc)
{
	struct ext_buffer *exb = NULL;
	unsigned int i;
	int ret;

	if (!desc || !desc->plane_count || !desc->init_plane ||
			!desc->uninit_plane || !desc->recycle)
		return NULL;
	if (desc->plane_count > MAX_PLANES)
		return NULL;

	exb = pitcher_calloc(1, sizeof(*exb) +
			desc->plane_count * sizeof(struct pitcher_buf_ref));
	if (!exb)
		return NULL;

	exb->buffer.count = desc->plane_count;
	exb->buffer.planes = exb->planes;
	exb->init_plane = desc->init_plane;
	exb->uninit_plane = desc->uninit_plane;
	exb->recycle = desc->recycle;
	exb->arg = desc->arg;

	for (i = 0; i < exb->buffer.count; i++) {
		exb->buffer.planes[i].size = desc->plane_size[i];
		exb->buffer.planes[i].dmafd = -1;
		ret = exb->init_plane(&exb->buffer.planes[i], i, exb->arg);
		if (ret < 0)
			break;
	}
	if (i == 0)
		goto error;
	exb->buffer.count = i;

	pitcher_init_obj(&exb->obj, __release_buffer);
	pitcher_get_obj(&exb->obj);

	return &exb->buffer;
error:
	SAFE_RELEASE(exb, pitcher_free);
	return NULL;
}

struct pitcher_buffer *pitcher_get_buffer(struct pitcher_buffer *buffer)
{
	struct ext_buffer *exb;

	assert(buffer);

	exb = container_of(buffer, struct ext_buffer, buffer);
	pitcher_get_obj(&exb->obj);

	return buffer;
}

void pitcher_put_buffer(struct pitcher_buffer *buffer)
{
	struct ext_buffer *exb;

	assert(buffer);

	exb = container_of(buffer, struct ext_buffer, buffer);
	pitcher_put_obj(&exb->obj);
}

unsigned int pitcher_get_buffer_refcount(struct pitcher_buffer *buffer)
{
	struct ext_buffer *exb;

	assert(buffer);

	exb = container_of(buffer, struct ext_buffer, buffer);
	return pitcher_get_obj_refcount(&exb->obj);
}

int pitcher_auto_remove_buffer(struct pitcher_buffer *buffer, void *arg, int *del)
{
	if (del)
		*del = true;

	return RET_OK;
}

