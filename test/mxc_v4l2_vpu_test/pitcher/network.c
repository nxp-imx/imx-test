/*
 * Copyright 2024 NXP
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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "network.h"


static int network_checkready(void *arg, int *is_end)
{
	struct network_test_t *network = arg;

	if (!network)
		return false;

	if (is_force_exit())
		network->end = true;

	if (is_source_end(network->output_chnno))
		network->end = true;

	if (is_end)
		*is_end = network->end;

	if (network->end)
		return false;

	if (!pitcher_chn_poll_input(network->output_chnno))
		return false;

	if (pitcher_poll_idle_buffer(network->output_chnno))
		return true;

	return false;
}

static int network_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct network_test_t *network = arg;

	if (!network || !pbuf)
		return -RET_E_INVAL;

	if (pbuf->planes[0].bytesused == 0)
		return 0;

	float r = ((float)rand()/RAND_MAX);
	PITCHER_DBG("r=%f vs drop_rate=%f\n", r, network->drop_rate);
	if (r >= network->drop_rate) {
		struct pitcher_buffer *buffer = pitcher_get_idle_buffer(network->capture_chnno);
		if (!buffer)
			return -RET_E_NOT_READY;
		buffer->format = pbuf->format;
		buffer->crop = pbuf->crop;
		buffer->planes[0].bytesused = pbuf->planes[0].bytesused;
		memcpy(buffer->planes[0].virt, pbuf->planes[0].virt, pbuf->planes[0].bytesused);
		pitcher_push_back_output(network->capture_chnno, buffer);
		SAFE_RELEASE(buffer, pitcher_put_buffer);
		PITCHER_DBG("packet passed\n");
	} else {
		PITCHER_DBG("packet dropped\n");
	}

	return RET_OK;
}

static int network_recycle_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct network_test_t *network = arg;
	int is_end = false;

	if (!network)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(network->capture_chnno) && !network->end)
		pitcher_put_buffer_idle(network->capture_chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

static struct pitcher_buffer *network_alloc_buffer(void *arg)
{
	struct network_test_t *network = arg;
	struct pitcher_buffer_desc desc;

	if (!network)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size[0] = network->packet_size;
	desc.init_plane = pitcher_alloc_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.recycle = network_recycle_buffer;
	desc.arg = network;

	return pitcher_new_buffer(&desc);
}

static void free_network_node(struct test_node *node)
{
	struct network_test_t *network;

	if (!node)
		return;
	network = container_of(node, struct network_test_t, node);

	SAFE_CLOSE(network->output_chnno, pitcher_unregister_chn);
	SAFE_CLOSE(network->capture_chnno, pitcher_unregister_chn);
	SAFE_RELEASE(network, pitcher_free);
}

static int init_network_node(struct test_node *node)
{
	struct network_test_t *network;
	int ret;

	if (!node)
		return -RET_E_INVAL;

	network = container_of(node, struct network_test_t, node);

	network->desc.fd = -1;
	network->desc.check_ready = network_checkready;
	network->desc.runfunc = network_run;
	network->desc.buffer_count = 1;
	network->desc.alloc_buffer = network_alloc_buffer;

	ret = pitcher_register_chn(network->node.context, &network->desc, network);
	if (ret < 0) {
		PITCHER_ERR("register network fail\n");
		SAFE_CLOSE(network->capture_chnno, pitcher_unregister_chn);
		return ret;
	}
	network->capture_chnno = ret;

	ret = pitcher_register_chn(network->node.context, &network->desc, network);
	if (ret < 0) {
		PITCHER_ERR("register network fail\n");
		SAFE_CLOSE(network->output_chnno, pitcher_unregister_chn);
		return ret;
	}
	network->output_chnno = ret;

	return RET_OK;
}

static int get_network_source_chnno(struct test_node *node)
{
	struct network_test_t *network;

	if (!node)
		return -RET_E_INVAL;

	network = container_of(node, struct network_test_t, node);

	return network->capture_chnno;
}

static int get_network_sink_chnno(struct test_node *node)
{
	struct network_test_t *network;

	if (!node)
		return -RET_E_INVAL;

	network = container_of(node, struct network_test_t, node);

	return network->output_chnno;
}

static int set_network_source(struct test_node *node, struct test_node *src)
{
	struct network_test_t *network;

	if (!node || !src)
		return -RET_E_INVAL;

	network = container_of(node, struct network_test_t, node);
	network->packet_size = get_image_size(src->pixelformat,
										  src->width,
										  src->height,
										  0);

	return RET_OK;
}

struct test_node *alloc_network_node(void)
{
	struct network_test_t *network;

	network = pitcher_calloc(1, sizeof(*network));
	if (!network)
		return NULL;

	network->node.key = -1;
	network->node.source = -1;
	network->node.type = TEST_TYPE_NETWORK;
	network->node.pixelformat = PIX_FMT_RAW;
	network->node.get_source_chnno = get_network_source_chnno;
	network->node.get_sink_chnno = get_network_sink_chnno;
	network->node.set_source = set_network_source;
	network->node.init_node = init_network_node;
	network->node.free_node = free_network_node;
	network->output_chnno = -1;
	network->capture_chnno = -1;

	return &network->node;
}
