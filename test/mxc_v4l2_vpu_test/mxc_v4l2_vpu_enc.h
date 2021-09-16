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
/*
 * mxc_v4l2_vpu_enc.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _MXC_V4L2_VPU_ENC_H
#define _MXC_V4L2_VPU_ENC_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"

enum {
	TEST_TYPE_ENCODER = 0,
	TEST_TYPE_CAMERA,
	TEST_TYPE_FILEIN,
	TEST_TYPE_FILEOUT,
	TEST_TYPE_CONVERT,
	TEST_TYPE_DECODER,
	TEST_TYPE_PARSER,
	TEST_TYPE_SINK,
};

struct test_node {
	int key;
	int source;
	int type;

	uint32_t pixelformat;
	uint32_t width;
	uint32_t height;
	uint32_t bytesperline;
	uint32_t framerate;
	int (*set_source)(struct test_node *node, struct test_node *src);
	int (*init_node)(struct test_node *node);
	void (*free_node)(struct test_node *node);
	int (*get_source_chnno)(struct test_node *node);
	int (*get_sink_chnno)(struct test_node *node);
	int frame_skip;
	unsigned int seek_thd;
	PitcherContext context;
};

struct mxc_vpu_test_option
{
	const char *name;
	uint32_t arg_num;
	const char *desc;
};

int is_force_exit(void);
int is_source_end(int chnno);
struct test_node *get_test_node(uint32_t key);

#ifdef ENABLE_WAYLAND
extern struct mxc_vpu_test_option waylandsink_options[];
int parse_wayland_sink_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[]);
struct test_node *alloc_wayland_sink_node(void);
#endif

extern struct mxc_vpu_test_option dmanode_options[];
int parse_dmanode_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[]);
struct test_node *alloc_dmanode(void);

#ifdef __cplusplus
}
#endif
#endif
