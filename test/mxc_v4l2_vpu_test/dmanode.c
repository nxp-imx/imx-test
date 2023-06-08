/*
 * Copyright(c) 2021 NXP. All rights reserved.
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
#include "pitcher/dmabuf.h"
#include "mxc_v4l2_vpu_enc.h"

struct dmanode_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	struct pix_fmt_info format;
	int end;

	unsigned long frame_count;
};

struct mxc_vpu_test_option dmanode_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{NULL, 0, NULL},
};

int dmanode_init_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

int dmanode_free_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	pitcher_free_dma_buf(plane);
	return RET_OK;
}

int dmanode_recycle_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct dmanode_test_t *dn = arg;
	int is_end = false;

	if (!dn)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(dn->chnno) && !dn->end)
		pitcher_put_buffer_idle(dn->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

struct pitcher_buffer *dmanode_alloc_buffer(void *arg)
{
	struct dmanode_test_t *dn = arg;
	struct pitcher_buffer_desc desc;

	if (!dn)
		return NULL;
	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.init_plane = dmanode_init_plane;
	desc.uninit_plane = dmanode_free_plane;
	desc.recycle = dmanode_recycle_buffer;
	desc.arg = dn;

	return pitcher_new_buffer(&desc);
}

int start_dmanode(void *arg)
{
	struct dmanode_test_t *dn = arg;

	if (!dn)
		return -RET_E_NULL_POINTER;

	dn->end = false;
	return RET_OK;
}

int checkready_dmanode(void *arg, int *is_end)
{
	struct dmanode_test_t *dn = arg;

	if (!dn)
		return false;

	if (is_force_exit())
		dn->end = true;
	if (is_source_end(dn->chnno) && !pitcher_chn_poll_input(dn->chnno))
		dn->end = true;
	if (is_end)
		*is_end = dn->end;

	if (!pitcher_chn_poll_input(dn->chnno))
		return false;
	if (!pitcher_poll_idle_buffer(dn->chnno))
		return false;

	return true;
}

int run_dmanode(void *arg, struct pitcher_buffer *buffer)
{
	struct dmanode_test_t *dn = arg;
	struct pitcher_buffer *dst;
	unsigned int i;
	unsigned long bytesused = 0;

	if (buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		dn->end = true;
	if (buffer->planes[0].bytesused)
		dn->frame_count++;
	if (pitcher_buffer_is_dma_buf(buffer) == RET_OK) {
		pitcher_push_back_output(dn->chnno, buffer);
		return RET_OK;
	}

	dst = pitcher_get_idle_buffer(dn->chnno);
	if (!dst)
		return -RET_E_NOT_READY;

	if (!dst->format) {
		pitcher_free_dma_buf(&dst->planes[0]);
		if (buffer->format)
			dst->format = buffer->format;
		else
			dst->format = &dn->format;
		dst->planes[0].size = dst->format->size;
		if (pitcher_alloc_dma_buf(&dst->planes[0])) {
			dst->format = NULL;
			return -RET_E_NO_MEMORY;
		}
	}

	for (i = 0; i < dst->format->num_planes; i++) {
		struct pitcher_buf_ref splane;
		struct pitcher_buf_ref dplane;

		pitcher_get_buffer_plane(buffer, i, &splane);
		pitcher_get_buffer_plane(dst, i, &dplane);
		memcpy(dplane.virt, splane.virt, splane.bytesused);
		bytesused += splane.bytesused;
	}
	dst->flags = buffer->flags;
	dst->planes[0].bytesused = bytesused;
	pitcher_push_back_output(dn->chnno, dst);
	SAFE_RELEASE(dst, pitcher_put_buffer);

	return RET_OK;
}

int init_dmanode(struct test_node *node)
{
	struct dmanode_test_t *dn;

	if (!node)
		return -RET_E_NULL_POINTER;

	dn = container_of(node, struct dmanode_test_t, node);
	dn->desc.fd = -1;
	dn->desc.start = start_dmanode;
	dn->desc.check_ready = checkready_dmanode;
	dn->desc.runfunc = run_dmanode;
	dn->desc.buffer_count = 4;
	dn->desc.alloc_buffer = dmanode_alloc_buffer;
	snprintf(dn->desc.name, sizeof(dn->desc.name), "dma");

	return RET_OK;
}

void free_dmanode(struct test_node *node)
{
	struct dmanode_test_t *dn;

	if (!node)
		return;

	dn = container_of(node, struct dmanode_test_t, node);
	PITCHER_LOG("dma buffer: %ld\n", dn->frame_count);
	SAFE_RELEASE(dn, pitcher_free);
}

int get_dmanode_chnno(struct test_node *node)
{
	struct dmanode_test_t *dn;
	struct test_node *src;

	if (!node)
		return -RET_E_NULL_POINTER;

	dn = container_of(node, struct dmanode_test_t, node);
	if (dn->chnno >= 0)
		return dn->chnno;

	src = get_test_node(node->source);
	if (!src || src->get_source_chnno(src) < 0)
		return dn->chnno;

	dn->chnno = pitcher_register_chn(dn->node.context, &dn->desc, dn);
	return dn->chnno;
}

int set_dmanode_source(struct test_node *node, struct test_node *src)
{
	struct dmanode_test_t *dn;

	if (!node || !src)
		return -RET_E_INVAL;

	dn = container_of(node, struct dmanode_test_t, node);
	dn->node.width = src->width;
	dn->node.height = src->height;
	dn->node.pixelformat = src->pixelformat;

	memset(&dn->format, 0, sizeof(dn->format));
	dn->format.format = dn->node.pixelformat;
	dn->format.width = dn->node.width;
	dn->format.height = dn->node.height;
	pitcher_get_pix_fmt_info(&dn->format, 0);

	return RET_OK;
}

int parse_dmanode_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct dmanode_test_t *dn;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	dn = container_of(node, struct dmanode_test_t, node);
	if (!strcasecmp(option->name, "key")) {
		dn->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		dn->node.source = strtol(argv[0], NULL, 0);
	}

	return RET_OK;
}

struct test_node *alloc_dmanode(void)
{
	struct dmanode_test_t *dn;

	dn = pitcher_calloc(1, sizeof(*dn));
	if (!dn)
		return NULL;

	dn->node.key = -1;
	dn->node.source = -1;
	dn->node.type = TEST_TYPE_CONVERT;
	dn->chnno = -1;

	dn->node.init_node = init_dmanode;
	dn->node.free_node = free_dmanode;
	dn->node.get_source_chnno = get_dmanode_chnno;
	dn->node.get_sink_chnno = get_dmanode_chnno;
	dn->node.set_source = set_dmanode_source;

	return &dn->node;
}
