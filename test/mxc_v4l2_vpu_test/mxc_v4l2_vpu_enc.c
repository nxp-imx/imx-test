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
 * mxc_v4l2_vpu_test.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/pitcher_v4l2.h"
#include "pitcher/parse.h"
#include "pitcher/platform.h"
#include "pitcher/platform_8x.h"
#include "pitcher/convert.h"

#define VERSION_MAJOR		2
#define VERSION_MINOR		0

#define MAX_NODE_COUNT		32
#define DEFAULT_FMT		V4L2_PIX_FMT_NV12
#define DEFAULT_WIDTH		1920
#define DEFAULT_HEIGHT		1080
#define DEFAULT_FRAMERATE	30
#define MIN_BS			128

enum {
	TEST_TYPE_ENCODER = 0,
	TEST_TYPE_CAMERA,
	TEST_TYPE_FILEIN,
	TEST_TYPE_FILEOUT,
	TEST_TYPE_CONVERT,
	TEST_TYPE_DECODER,
	TEST_TYPE_PARSER,
};

struct test_node {
	int key;
	int source;
	int type;

	uint32_t pixelformat;
	uint32_t width;
	uint32_t height;
	uint32_t framerate;
	int (*set_source)(struct test_node *node, struct test_node *src);
	int (*init_node)(struct test_node *node);
	void (*free_node)(struct test_node *node);
	int (*get_source_chnno)(struct test_node *node);
	int (*get_sink_chnno)(struct test_node *node);
	int frame_skip;
	PitcherContext context;
};

struct encoder_test_t {
	struct test_node node;
	int fd;

	struct v4l2_component_t capture;
	struct v4l2_component_t output;

	uint32_t profile;
	uint32_t level;
	uint32_t iframe_interval;
	uint32_t gop;
	uint32_t bitrate_mode;
	uint32_t target_bitrate;
	uint32_t peak_bitrate;
	uint32_t qp;
	uint32_t bframes;

	const char *devnode;
};

struct decoder_test_t {
	struct test_node node;
	int fd;

	struct v4l2_component_t capture;
	struct v4l2_component_t output;

	uint32_t sizeimage;
	const char *devnode;

	struct platform_t platform;
};

struct camera_test_t {
	struct test_node node;
	const char *devnode;
	struct v4l2_component_t capture;
	unsigned long frame_num;
};

struct test_file_t {
	struct test_node node;
	struct pitcher_unit_desc desc;

	int chnno;
	unsigned long frame_count;
	char *filename;
	char *mode;
	FILE *filp;
	int fd;
	void *virt;
	unsigned long size;
	unsigned long offset;
	int end;

	unsigned long frame_num;
	int loop;
};

struct convert_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	uint32_t width;
	uint32_t height;
	uint32_t ifmt;
	int end;
};

struct parser_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;

	int chnno;
	unsigned long frame_count;
	unsigned long frame_num;
	char *filename;
	char *mode;
	int fd;
	void *virt;
	unsigned long size;
	unsigned long offset;
	int end;
	int loop;
	int show;

	Parser p;
};

struct mxc_vpu_test_option
{
	const char *name;
	uint32_t arg_num;
	const char *desc;
};

struct mxc_vpu_test_subcmd {
	const char *subcmd;
	struct mxc_vpu_test_option *option;
	int type;
	int (*parse_option)(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[]);
	struct test_node *(*alloc_node)(void);
};

static int stop_enc(struct v4l2_component_t *component);
static int stop_dec(struct v4l2_component_t *component);

static uint32_t bitmask;
static struct test_node *nodes[MAX_NODE_COUNT];

#define FORCE_EXIT_MASK		0x8000
static int g_exit;

static void force_exit(void)
{
	g_exit |= FORCE_EXIT_MASK;
}

static int terminate(void)
{
	g_exit++;

	if (g_exit >= 3)
		force_exit();

	return 0;
}

static int is_force_exit(void)
{
	if (g_exit & FORCE_EXIT_MASK)
		return true;

	return false;
}

int is_termination(void)
{
	return g_exit;
}

static void sig_handler(int sign)
{
	switch (sign) {
	case SIGINT:
	case SIGTERM:
		terminate();
		break;
	case SIGALRM:
		break;
	}
}

static int subscribe_event(int fd)
{
	struct v4l2_event_subscription sub;

	memset(&sub, 0, sizeof(sub));

	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);

	sub.type = V4L2_EVENT_EOS;
	ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);

	return 0;
}

static int unsubcribe_event(int fd)
{
	struct v4l2_event_subscription sub;
	int ret;

	memset(&sub, 0, sizeof(sub));

	sub.type = V4L2_EVENT_ALL;
	ret = ioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
	if (ret < 0) {
		PITCHER_LOG("fail to unsubscribe\n");
		return -1;
	}

	return 0;
}

static int is_source_end(int chnno)
{
	int source;

	if (chnno < 0)
		return true;

	if (pitcher_chn_poll_input(chnno))
		return false;

	source = pitcher_get_source(chnno);
	if (source < 0)
		return true;

	if (!pitcher_is_active(source))
		return true;

	return false;
}

static int is_camera_finish(struct v4l2_component_t *component)
{
	struct camera_test_t *camera;
	int is_end = false;

	if (!component)
		return true;

	camera = container_of(component, struct camera_test_t, capture);

	if (camera->frame_num > 0 &&
			component->frame_count >= camera->frame_num)
		is_end = true;

	if (is_termination())
		is_end = true;

	if (is_end)
		PITCHER_LOG("stop camera\n");
	return is_end;
}

static int is_encoder_output_finish(struct v4l2_component_t *component)
{
	struct encoder_test_t *encoder;
	int is_end = false;

	if (!component)
		return true;

	encoder = container_of(component, struct encoder_test_t, output);

	if (is_source_end(encoder->output.chnno))
		stop_enc(component);

	if (component->eos_received) {
		encoder->capture.eos_received = true;
		component->eos_received = false;
	}
	if (is_force_exit() || encoder->capture.end) {
		is_end = true;
		if (!component->frame_count)
			force_exit();
	}

	return is_end;
}

static int is_encoder_capture_finish(struct v4l2_component_t *component)
{
	if (!component)
		return true;

	if (component->eos_received) {
		component->eos_received = false;
		return true;
	}

	return is_force_exit();
}

static void switch_fmt_to_tile(unsigned int *fmt)
{
	switch (*fmt) {
	case V4L2_PIX_FMT_NV12:
		*fmt = V4L2_PIX_FMT_NV12_TILE;
	default:
		break;
	}
}

static void sync_decoder_node_info(struct decoder_test_t *decoder)
{
	assert(decoder);

	decoder->node.width = decoder->capture.width;
	decoder->node.height = decoder->capture.height;
	decoder->node.pixelformat = decoder->capture.pixelformat;
	if (decoder->platform.type == IMX_8X)
		switch_fmt_to_tile(&decoder->node.pixelformat);
}

static int handle_decoder_resolution_change(struct decoder_test_t *decoder)
{
	int ret;
	int chnno;

	if (!decoder->capture.resolution_change)
		return RET_OK;

	if (decoder->capture.chnno < 0) {
		ret = pitcher_register_chn(decoder->node.context,
						&decoder->capture.desc,
						&decoder->capture);
		if (ret < 0) {
			PITCHER_ERR("regisger %s fail\n",
					decoder->capture.desc.name);
			return ret;
		}
		decoder->capture.chnno = ret;
	}

	chnno = decoder->capture.chnno;
	if (pitcher_get_status(chnno) == PITCHER_STATE_STOPPED) {
		decoder->capture.width = 0;
		decoder->capture.height = 0;
		ret = pitcher_start_chn(chnno);
		if (ret < 0) {
			force_exit();
			return ret;
		}

		sync_decoder_node_info(decoder);
		PITCHER_LOG("decoder capture : %d x %d, count = %ld\n",
			decoder->capture.width, decoder->capture.height,
			decoder->capture.frame_count);
		decoder->capture.resolution_change = false;

		return RET_OK;
	}

	if (!decoder->capture.end) {
		decoder->capture.end = true;
		return RET_OK;
	}

	return RET_OK;
}

static int is_decoder_output_finish(struct v4l2_component_t *component)
{
	struct decoder_test_t *decoder;
	int is_end = false;
	int ret = 0;

	if (!component)
		return true;

	decoder = container_of(component, struct decoder_test_t, output);

	if (is_source_end(decoder->output.chnno))
		stop_dec(component);

	if (component->eos_received) {
		decoder->capture.eos_received = true;
		component->eos_received = false;
	}
	if (component->resolution_change) {
		component->resolution_change = false;
		decoder->capture.resolution_change = true;
	}
	if (decoder->capture.resolution_change) {
		ret = handle_decoder_resolution_change(decoder);
		if (ret < 0)
			is_end = true;
	}

	if (is_force_exit() ||
	    (decoder->capture.end && !decoder->capture.resolution_change)) {
		PITCHER_LOG("decoder output finish, capture: %d, %d\n",
				decoder->capture.end,
				decoder->capture.resolution_change);
		is_end = true;
		if (!component->frame_count)
			force_exit();
	}

	return is_end;
}

static int is_decoder_capture_finish(struct v4l2_component_t *component)
{
	if (!component)
		return true;

	if (component->eos_received) {
		if (!component->resolution_change)
			component->eos_received = false;
		return true;
	}

	return is_force_exit();
}

static int start_enc(struct v4l2_component_t *component)
{
	PITCHER_LOG("start encoder\n");

	return RET_OK;
}

static int stop_enc(struct v4l2_component_t *component)
{
	struct v4l2_encoder_cmd cmd;
	int ret;


	if (!component || component->fd < 0 || !component->enable)
		return -RET_E_INVAL;

	PITCHER_LOG("stop encoder\n");

	cmd.cmd = V4L2_ENC_CMD_STOP;
	ret = ioctl(component->fd, VIDIOC_ENCODER_CMD, &cmd);
	if (ret < 0)
		PITCHER_ERR("stop enc fail\n");
	component->enable = false;

	return RET_OK;
}

static int start_dec(struct v4l2_component_t *component)
{
	PITCHER_LOG("start decoder\n");
	return RET_OK;
}

static int stop_dec(struct v4l2_component_t *component)
{
	struct v4l2_decoder_cmd cmd;
	int ret;

	if (!component || component->fd < 0 || !component->enable)
		return -RET_E_INVAL;

	PITCHER_LOG("stop decoder\n");
	cmd.cmd = V4L2_DEC_CMD_STOP;
	ret = ioctl(component->fd, VIDIOC_DECODER_CMD, &cmd);
	if (ret < 0)
		PITCHER_ERR("stop dec fail\n");
	component->enable = false;

	return RET_OK;
}

struct mxc_vpu_test_option ifile_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"name", 1, "--name <filename>\n\t\t\tassign input file name"},
	{"fmt",  1, "--fmt <fmt>\n\t\t\tassign input file pixel format, support nv12, i420"},
	{"size", 2, "--size <width> <height>\n\t\t\tassign input file resolution"},
	{"framenum", 1, "--framenum <number>\n\t\t\tset input frame number"},
	{"loop", 1, "--loop <loop times>\n\t\t\tset input loops times"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option ofile_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"name", 1, "--name <filename>\n\t\t\tassign output file name"},
	{"source", 1, "--source <key no>\n\t\t\tset output file source key"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option camera_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"device", 1, "--device <devnode>\n\t\t\tassign camera video device node"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign camera pixel format, support nv12, i420"},
	{"size", 2, "--size <width> <height>\n\t\t\tassign camera resolution"},
	{"framerate", 1, "--framerate <f>\n\t\t\tset frame rate(fps)"},
	{"framenum", 1, "--framenum <number>\n\t\t\tset frame number"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option encoder_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset h264 encoder input key number"},
	{"device", 1, "--device <devnode>\n\t\t\tassign encoder video device node"},
	{"size", 2, "--size <width> <height>\n\t\t\tset h264 encoder output size"},
	{"framerate", 1, "--framerate <f>\n\t\t\tset frame rate(fps)"},
	{"profile", 1, "--profile <profile>\n\t\t\tset h264 profile, 0 : baseline, 2 : main, 4 : high"},
	{"level", 1, "--level <level>\n\t\t\tset h264 level, 0~15, 14:level_5_0(default)"},
	{"gop", 1, "--gop <gop>\n\t\t\tset group of picture"},
	{"mode", 1, "--mode <mode>\n\t\t\tset h264 mode, 0:vbr, 1:cbr(default)"},
	{"qp", 1, "--qp <qp>\n\t\t\tset quantizer parameter, 0~51"},
	{"bitrate", 1, "--bitrate <br>\n\t\t\tset encoder target bitrate, the unit is b"},
	{"peak", 1, "--peak <br>\n\t\t\tset encoder peak bitrate, the unit is b"},
	{"bframes", 1, "--bframes <number>\n\t\t\tset the number of b frames"},
	{"crop", 4, "--crop <left> <top> <width> <height>\n\t\t\tset h264 crop position and size"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign encode pixel format, support h264, h265"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option decoder_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset h264 encoder input key number"},
	{"device", 1, "--device <devnode>\n\t\t\tassign encoder video device node"},
	{"bs", 1, "--bs <bs count>\n\t\t\tSpecify the count of input buffer block size, the unit is Kb."},
	{"framemode", 1, "--framemode <level>\n\t\t\tSpecify input frame mode, 0: frame level, 1: non-frame level"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign encode pixel format, support nv12, nv21, i420"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option convert_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign output pixel format"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option parser_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"name", 1, "--name <filename>\n\t\t\tassign parse file name"},
	{"fmt",  1, "--fmt <fmt>\n\t\t\tassign input file pixel format, current support h264, h265"},
	{"size", 2, "--size <width> <height>\n\t\t\tset size"},
	{"framenum", 1, "--framenum <number>\n\t\t\tset input/parse frame number"},
	{"loop", 1, "--loop <loop times>\n\t\t\tset input loops times"},
	{"show", 0, "--show\n\t\t\tshow size and offset of per frame"},
	{NULL, 0, NULL},
};

static int get_pixelfmt_from_str(const char *str)
{
	if (!str)
		return -RET_E_INVAL;

	if (!strcasecmp(str, "nv12"))
		return V4L2_PIX_FMT_NV12;
	if (!strcasecmp(str, "nv21"))
		return V4L2_PIX_FMT_NV21;
	if (!strcasecmp(str, "i420"))
		return V4L2_PIX_FMT_YUV420;
	if (!strcasecmp(str, "yuv420p"))
		return V4L2_PIX_FMT_YUV420;
	if (!strcasecmp(str, "na12"))
		return V4L2_PIX_FMT_NV12_TILE;
	if (!strcasecmp(str, "nt12"))
		return V4L2_PIX_FMT_NV12_TILE_10BIT;
	if (!strcasecmp(str, "h264"))
		return V4L2_PIX_FMT_H264;
	if (!strcasecmp(str, "h265"))
		return V4L2_PIX_FMT_HEVC;
	if (!strcasecmp(str, "mpeg2"))
		return V4L2_PIX_FMT_MPEG2;
	if (!strcasecmp(str, "mpeg4"))
		return V4L2_PIX_FMT_MPEG4;
	if (!strcasecmp(str, "h263"))
		return V4L2_PIX_FMT_H263;
	if (!strcasecmp(str, "jpeg"))
		return V4L2_PIX_FMT_JPEG;
	if (!strcasecmp(str, "vc1l"))
		return V4L2_PIX_FMT_VC1_ANNEX_L;
	if (!strcasecmp(str, "vc1g"))
		return V4L2_PIX_FMT_VC1_ANNEX_G;
	if (!strcasecmp(str, "xvid"))
		return V4L2_PIX_FMT_XVID;
	if (!strcasecmp(str, "vp9"))
		return V4L2_PIX_FMT_VP9;
	if (!strcasecmp(str, "vp8"))
		return V4L2_PIX_FMT_VP8;
	if (!strcasecmp(str, "vp6"))
		return VPU_PIX_FMT_VP6;
	if (!strcasecmp(str, "avs"))
		return VPU_PIX_FMT_AVS;
	if (!strcasecmp(str, "rv"))
		return VPU_PIX_FMT_RV;
	if (!strcasecmp(str, "spk"))
		return VPU_PIX_FMT_SPK;
	if (!strcasecmp(str, "divx"))
		return VPU_PIX_FMT_DIVX;

	PITCHER_ERR("unsupport pixelformat : %s\n", str);
	return -RET_E_INVAL;
}

static void free_camera_node(struct test_node *node)
{
	struct camera_test_t *camera;

	if (!node)
		return;

	camera = container_of(node, struct camera_test_t, node);

	PITCHER_LOG("camera frame_count = %ld\n", camera->capture.frame_count);
	SAFE_CLOSE(camera->capture.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(camera->capture.fd, close);
	SAFE_RELEASE(camera, pitcher_free);
}

static int init_camera_node(struct test_node *node)
{
	struct camera_test_t *camera;
	int ret;

	if (!node)
		return -RET_E_INVAL;

	camera = container_of(node, struct camera_test_t, node);
	if (!camera->devnode)
		return -RET_E_INVAL;
	camera->capture.fd = open(camera->devnode, O_RDWR | O_NONBLOCK);
	if (!camera->capture.fd) {
		PITCHER_ERR("open %s fail\n", camera->devnode);
		return -RET_E_OPEN;
	}

	camera->capture.desc = pitcher_v4l2_capture;
	camera->capture.desc.fd = camera->capture.fd;
	camera->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	camera->capture.memory = V4L2_MEMORY_MMAP;
	camera->capture.pixelformat = camera->node.pixelformat;
	camera->capture.width = camera->node.width;
	camera->capture.height = camera->node.height;
	camera->capture.framerate = camera->node.framerate;
	camera->capture.sizeimage = 0;
	camera->capture.bytesperline = camera->node.width;
	camera->capture.is_end = is_camera_finish;

	snprintf(camera->capture.desc.name, sizeof(camera->capture.desc.name),
			"camera.%d", camera->node.key);
	camera->capture.buffer_count = 4;
	ret = pitcher_register_chn(camera->node.context,
					&camera->capture.desc,
					&camera->capture);
	if (ret < 0) {
		PITCHER_ERR("regisger %s fail\n", camera->capture.desc.name);
		SAFE_CLOSE(camera->capture.fd, close);
		return ret;
	}
	camera->capture.chnno = ret;

	return RET_OK;
}

static int get_camera_chnno(struct test_node *node)
{
	struct camera_test_t *camera;

	if (!node)
		return -RET_E_INVAL;

	camera = container_of(node, struct camera_test_t, node);
	return camera->capture.chnno;
}

static struct test_node *alloc_camera_node(void)
{
	struct camera_test_t *camera;

	camera = pitcher_calloc(1, sizeof(*camera));
	if (!camera)
		return NULL;

	camera->node.key = -1;
	camera->node.source = -1;
	camera->node.type = TEST_TYPE_CAMERA;
	camera->node.pixelformat = DEFAULT_FMT;
	camera->node.width = DEFAULT_WIDTH;
	camera->node.height = DEFAULT_HEIGHT;
	camera->node.framerate = DEFAULT_FRAMERATE;
	camera->node.get_source_chnno = get_camera_chnno;
	camera->node.init_node = init_camera_node;
	camera->node.free_node = free_camera_node;
	camera->capture.chnno = -1;

	return &camera->node;
}

static int parse_camera_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct camera_test_t *camera;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	camera = container_of(node, struct camera_test_t, node);

	if (!strcasecmp(option->name, "key")) {
		camera->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "device")) {
		camera->devnode = argv[0];
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt < 0)
			return fmt;
		camera->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		camera->node.width = strtol(argv[0], NULL, 0);
		camera->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framerate")) {
		camera->node.framerate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "framenum")) {
		camera->frame_num = strtol(argv[0], NULL, 0);
	}

	return RET_OK;
}

static void free_encoder_node(struct test_node *node)
{
	struct encoder_test_t *encoder;

	if (!node)
		return;
	encoder = container_of(node, struct encoder_test_t, node);

	PITCHER_LOG("encoder frame count : %ld -> %ld\n",
			encoder->output.frame_count,
			encoder->capture.frame_count);
	if (encoder->capture.frame_count) {
		uint64_t count = encoder->capture.frame_count;
		uint64_t fps;
		uint64_t ts_delta;

		if (count > encoder->output.frame_count)
			count = encoder->output.frame_count;
		ts_delta = encoder->capture.ts_e - encoder->capture.ts_b;
		fps = count * 1000000000 * 1000 / ts_delta;
		PITCHER_LOG("encoder frame fps : %ld.%ld; time:%ld.%lds; count = %ld\n",
				fps / 1000, fps % 1000,
				ts_delta / 1000000000,
				(ts_delta % 1000000000) / 1000000,
				count);
	}

	unsubcribe_event(encoder->fd);
	SAFE_CLOSE(encoder->output.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(encoder->capture.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(encoder->fd, close);
	SAFE_RELEASE(encoder, pitcher_free);
}

static int set_encoder_source(struct test_node *node, struct test_node *src)
{
	struct encoder_test_t *encoder;

	if (!node || !src)
		return -RET_E_INVAL;

	encoder = container_of(node, struct encoder_test_t, node);

	encoder->output.pixelformat = src->pixelformat;
	encoder->output.width = src->width;
	encoder->output.height = src->height;
	encoder->output.bytesperline = src->width;
	if (src->type == TEST_TYPE_CAMERA) {
		encoder->output.memory = V4L2_MEMORY_USERPTR;
		encoder->node.frame_skip = true;
	}

	return RET_OK;
}

static int get_encoder_source_chnno(struct test_node *node)
{
	struct encoder_test_t *encoder;

	if (!node)
		return -RET_E_INVAL;

	encoder = container_of(node, struct encoder_test_t, node);

	return encoder->capture.chnno;
}

static int get_encoder_sink_chnno(struct test_node *node)
{
	struct encoder_test_t *encoder;

	if (!node)
		return -RET_E_INVAL;

	encoder = container_of(node, struct encoder_test_t, node);

	return encoder->output.chnno;
}

static void validate_h264_profile_level(struct encoder_test_t *encoder)
{
	if (encoder->profile == UINT_MAX)
		encoder->profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	if (encoder->level == UINT_MAX)
		encoder->level = V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
}

static void validate_h265_profile_level(struct encoder_test_t *encoder)
{
	if (encoder->profile == UINT_MAX)
		encoder->profile = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN;
	if (encoder->level == UINT_MAX)
		encoder->level = V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1;
}

static void validate_vpx_profile_level(struct encoder_test_t *encoder)
{
	if (encoder->profile == UINT_MAX)
		encoder->profile = V4L2_MPEG_VIDEO_VP8_PROFILE_0;
}

static int set_encoder_parameters(struct encoder_test_t *encoder)
{
	int fd;
	int profile_id = 0;
	int level_id = 0;

	if (!encoder || encoder->fd < 0)
		return -RET_E_INVAL;

	fd = encoder->fd;

	switch (encoder->capture.pixelformat) {
	case V4L2_PIX_FMT_H264:
		profile_id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
		level_id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
		validate_h264_profile_level(encoder);
		break;
	case V4L2_PIX_FMT_HEVC:
		profile_id = V4L2_CID_MPEG_VIDEO_HEVC_PROFILE;
		level_id = V4L2_CID_MPEG_VIDEO_HEVC_LEVEL;
		validate_h265_profile_level(encoder);
		break;
	case V4L2_PIX_FMT_VP8:
		profile_id = V4L2_CID_MPEG_VIDEO_VP8_PROFILE;
		validate_vpx_profile_level(encoder);
		break;
	default:
		return -RET_E_INVAL;
	}
	set_ctrl(fd, profile_id, encoder->profile);
	if (level_id)
		set_ctrl(fd, level_id, encoder->level);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, encoder->bitrate_mode);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE, encoder->target_bitrate);
	if (encoder->peak_bitrate)
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, encoder->peak_bitrate);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, encoder->gop);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_B_FRAMES, encoder->bframes);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, encoder->qp);

	return RET_OK;
}

static int init_encoder_node(struct test_node *node)
{
	struct encoder_test_t *encoder;
	int ret;
	struct v4l2_capability cap;

	if (!node)
		return -RET_E_INVAL;

	encoder = container_of(node, struct encoder_test_t, node);
	encoder->capture.pixelformat = encoder->node.pixelformat;
	if (encoder->devnode) {
		encoder->fd = open(encoder->devnode, O_RDWR | O_NONBLOCK);
		PITCHER_LOG("open %s\n", encoder->devnode);
		ret = check_v4l2_device_type(encoder->fd,
						encoder->output.pixelformat,
						encoder->capture.pixelformat);
		if (ret == FALSE) {
			SAFE_CLOSE(encoder->fd, close);
			PITCHER_ERR("open encoder device node fail\n");
			return -RET_E_OPEN;
		}
	} else {
		encoder->fd = lookup_v4l2_device_and_open(encoder->output.pixelformat,
						encoder->capture.pixelformat);
		if (encoder->fd < 0) {
			PITCHER_ERR("open encoder device node fail\n");
			return -RET_E_OPEN;
		}
	}

	ioctl(encoder->fd, VIDIOC_QUERYCAP, &cap);
	if (is_v4l2_splane(&cap)) {
		encoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		encoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else {
		encoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		encoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	}

	subscribe_event(encoder->fd);

	encoder->output.desc = pitcher_v4l2_output;
	encoder->capture.desc = pitcher_v4l2_capture;
	encoder->capture.fd = encoder->fd;
	encoder->output.fd = encoder->fd;
	encoder->output.desc.fd = encoder->fd;
	encoder->capture.desc.fd = encoder->fd;
	encoder->output.desc.events |= encoder->capture.desc.events;
	encoder->capture.desc.events = 0;
	encoder->output.start = start_enc;
	/*encoder->output.stop = stop_enc;*/

	encoder->capture.pixelformat = encoder->node.pixelformat;
	encoder->capture.width = encoder->node.width;
	encoder->capture.height = encoder->node.height;
	encoder->capture.framerate = encoder->node.framerate;
	encoder->capture.sizeimage =
		encoder->output.width * encoder->output.height;
	encoder->capture.bytesperline = encoder->node.width;
	encoder->capture.is_end = is_encoder_capture_finish;
	encoder->capture.buffer_count = 4;
	snprintf(encoder->capture.desc.name, sizeof(encoder->capture.desc.name),
			"encoder capture.%d", encoder->node.key);

	if (!encoder->output.width)
		encoder->output.width = encoder->node.width;
	if (!encoder->output.height)
		encoder->output.height = encoder->node.height;
	if (!encoder->output.bytesperline)
		encoder->output.bytesperline = encoder->node.width;
	encoder->output.framerate = encoder->node.framerate;
	encoder->output.sizeimage = 0;
	encoder->output.is_end = is_encoder_output_finish;
	encoder->output.buffer_count = 4;
	snprintf(encoder->output.desc.name, sizeof(encoder->output.desc.name),
			"encoder output.%d", encoder->node.key);

	if (encoder->output.crop.left + encoder->output.crop.width > encoder->output.width) {
		PITCHER_LOG("invalid crop (%d, %d) %d x %d\n",
				encoder->output.crop.left,
				encoder->output.crop.top,
				encoder->output.crop.width,
				encoder->output.crop.height);
		SAFE_CLOSE(encoder->fd, close);
		return -RET_E_INVAL;
	}
	if (encoder->output.crop.top + encoder->output.crop.height > encoder->output.height) {
		PITCHER_LOG("invalid crop (%d, %d) %d x %d\n",
				encoder->output.crop.left,
				encoder->output.crop.top,
				encoder->output.crop.width,
				encoder->output.crop.height);
		SAFE_CLOSE(encoder->fd, close);
		return -RET_E_INVAL;
	}

	if (encoder->capture.width > encoder->output.width)
		encoder->capture.width = encoder->output.width;
	if (encoder->capture.height > encoder->output.height)
		encoder->capture.height = encoder->output.height;
	if (encoder->output.crop.width && encoder->capture.width > encoder->output.crop.width)
		encoder->capture.width = encoder->output.crop.width;
	if (encoder->output.crop.height &&
			encoder->capture.height > encoder->output.crop.height)
		encoder->capture.height = encoder->output.crop.height;

	ret = pitcher_register_chn(encoder->node.context,
				&encoder->capture.desc,
				&encoder->capture);
	if (ret < 0) {
		PITCHER_ERR("regisger %s fail\n", encoder->capture.desc.name);
		SAFE_CLOSE(encoder->output.chnno, pitcher_unregister_chn);
		SAFE_CLOSE(encoder->fd, close);
		return ret;
	}
	encoder->capture.chnno = ret;

	ret = pitcher_register_chn(encoder->node.context,
				&encoder->output.desc,
				&encoder->output);
	if (ret < 0) {
		PITCHER_ERR("regisger %s fail\n", encoder->capture.desc.name);
		SAFE_CLOSE(encoder->capture.chnno, pitcher_unregister_chn);
		SAFE_CLOSE(encoder->fd, close);
		return ret;
	}
	encoder->output.chnno = ret;

	return set_encoder_parameters(encoder);
}

static struct test_node *alloc_encoder_node(void)
{
	struct encoder_test_t *encoder;

	encoder = pitcher_calloc(1, sizeof(*encoder));
	if (!encoder)
		return NULL;

	encoder->node.key = -1;
	encoder->node.source = -1;
	encoder->fd = -1;
	encoder->node.type = TEST_TYPE_ENCODER;
	encoder->node.pixelformat = V4L2_PIX_FMT_H264;
	encoder->node.width = DEFAULT_WIDTH;
	encoder->node.height = DEFAULT_HEIGHT;
	encoder->node.framerate = DEFAULT_FRAMERATE;
	encoder->node.get_source_chnno = get_encoder_source_chnno;
	encoder->node.get_sink_chnno = get_encoder_sink_chnno;
	encoder->node.set_source = set_encoder_source;
	encoder->node.init_node = init_encoder_node;
	encoder->node.free_node = free_encoder_node;
	encoder->bitrate_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
	encoder->profile = UINT_MAX;
	encoder->level = UINT_MAX;
	encoder->gop = 30;
	encoder->bframes = 0;
	encoder->qp = 25;
	encoder->target_bitrate = 2 * 1024 * 1024;
	encoder->output.chnno = -1;
	encoder->capture.chnno = -1;

	encoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	encoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	encoder->output.memory = V4L2_MEMORY_MMAP;
	encoder->capture.memory = V4L2_MEMORY_MMAP;
	encoder->output.pixelformat = V4L2_PIX_FMT_NV12;
	encoder->capture.pixelformat = encoder->node.pixelformat;

	return &encoder->node;
}

static int parse_encoder_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct encoder_test_t *encoder;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	encoder = container_of(node, struct encoder_test_t, node);
	if (!strcasecmp(option->name, "key")) {
		encoder->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		encoder->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "device")) {
		encoder->devnode = argv[0];
	} else if (!strcasecmp(option->name, "size")) {
		encoder->node.width = strtol(argv[0], NULL, 0);
		encoder->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framerate")) {
		encoder->node.framerate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "profile")) {
		encoder->profile = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "level")) {
		encoder->level = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "gop")) {
		encoder->gop = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "bframes")) {
		encoder->bframes = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "mode")) {
		encoder->bitrate_mode = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "qp")) {
		encoder->qp = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "bitrate")) {
		encoder->target_bitrate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "peak")) {
		encoder->peak_bitrate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "crop")) {
		encoder->output.crop.left = strtol(argv[0], NULL, 0);
		encoder->output.crop.top = strtol(argv[1], NULL, 0);
		encoder->output.crop.width = strtol(argv[2], NULL, 0);
		encoder->output.crop.height = strtol(argv[3], NULL, 0);
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt > 0)
			encoder->node.pixelformat = fmt;
	}

	return RET_OK;
}

static int get_deocder_source_chnno(struct test_node *node)
{
	struct decoder_test_t *decoder;

	if (!node)
		return -RET_E_INVAL;

	decoder = container_of(node, struct decoder_test_t, node);

	return decoder->capture.chnno;
}

static int get_deocder_sink_chnno(struct test_node *node)
{
	struct decoder_test_t *decoder;

	if (!node)
		return -RET_E_INVAL;

	decoder = container_of(node, struct decoder_test_t, node);

	return decoder->output.chnno;
}

static int set_decoder_source(struct test_node *node, struct test_node *src)
{
	struct decoder_test_t *decoder;

	if (!node || !src)
		return -RET_E_INVAL;

	decoder = container_of(node, struct decoder_test_t, node);

	switch (src->pixelformat) {
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_H264_MVC:
	case V4L2_PIX_FMT_HEVC:
	case V4L2_PIX_FMT_MPEG2:
	case V4L2_PIX_FMT_MPEG4:
	case V4L2_PIX_FMT_H263:
	case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_XVID:
	case V4L2_PIX_FMT_VP8:
	case V4L2_PIX_FMT_VP9:
	case VPU_PIX_FMT_VP6:
	case VPU_PIX_FMT_AVS:
	case VPU_PIX_FMT_RV:
	case VPU_PIX_FMT_SPK:
	case VPU_PIX_FMT_DIVX:
		break;
	default:
		return -RET_E_NOT_SUPPORT;
	}

	decoder->output.pixelformat = src->pixelformat;
	decoder->output.width = src->width;
	decoder->output.height = src->height;

	return RET_OK;
}

static void free_decoder_node(struct test_node *node)
{
	struct decoder_test_t *decoder;

	if (!node)
		return;

	decoder = container_of(node, struct decoder_test_t, node);

	PITCHER_LOG("decoder frame count : %ld -> %ld\n",
			decoder->output.frame_count,
			decoder->capture.frame_count);
	if (decoder->capture.frame_count) {
		uint64_t count = decoder->capture.frame_count;
		uint64_t fps;
		uint64_t ts_delta;

		ts_delta = decoder->capture.ts_e - decoder->capture.ts_b;
		fps = count * 1000000000 * 1000 / ts_delta;
		PITCHER_LOG("decoder frame fps : %ld.%ld; time:%ld.%lds; count = %ld\n",
				fps / 1000, fps % 1000,
				ts_delta / 1000000000,
				(ts_delta % 1000000000) / 1000000,
				count);
	}
	unsubcribe_event(decoder->fd);
	SAFE_CLOSE(decoder->output.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(decoder->capture.chnno, pitcher_unregister_chn);
	SAFE_CLOSE(decoder->fd, close);
	SAFE_RELEASE(decoder, pitcher_free);
}

static int init_decoder_platform(struct decoder_test_t *decoder)
{
	struct v4l2_capability cap;

	ioctl(decoder->fd, VIDIOC_QUERYCAP, &cap);
	if (!strcasecmp((const char*)cap.driver, "vpu B0") ||
	    !strcasecmp((const char*)cap.driver, "imx vpu decoder")) {
		decoder->platform.type = IMX_8X;
		decoder->platform.set_decoder_parameter = set_decoder_parameter_8x;
	} else if (!strcasecmp((const char*)cap.driver, "vsi_v4l2")) {
		decoder->platform.type = IMX_8M;
	} else {
		decoder->platform.type = OTHERS;
	}

	decoder->platform.fd = decoder->fd;

	if (decoder->platform.set_decoder_parameter)
		return decoder->platform.set_decoder_parameter(&(decoder->platform));
	else
		return RET_OK;
}

static int init_decoder_node(struct test_node *node)
{
	struct decoder_test_t *decoder;
	struct v4l2_capability cap;
	int ret;

	if (!node)
		return -RET_E_INVAL;

	decoder = container_of(node, struct decoder_test_t, node);
	decoder->capture.pixelformat = decoder->node.pixelformat;
	PITCHER_LOG("decode capture format: %c%c%c%c\n",
			decoder->capture.pixelformat,
			decoder->capture.pixelformat >> 8,
			decoder->capture.pixelformat >> 16,
			decoder->capture.pixelformat >> 24);
	if (decoder->devnode) {
		decoder->fd = open(decoder->devnode, O_RDWR | O_NONBLOCK);
		if (decoder->fd >= 0 &&
		    !check_v4l2_device_type(decoder->fd,
					   decoder->output.pixelformat,
					   decoder->capture.pixelformat))
			SAFE_CLOSE(decoder->fd, close);
	} else {
		decoder->fd = lookup_v4l2_device_and_open(
					decoder->output.pixelformat,
					decoder->capture.pixelformat);
	}
	if (decoder->fd < 0) {
		PITCHER_ERR("open decoder device node fail\n");
		return -RET_E_OPEN;
	}

	ioctl(decoder->fd, VIDIOC_QUERYCAP, &cap);
	if (is_v4l2_splane(&cap)) {
		decoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		decoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	} else {
		decoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		decoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	}

	subscribe_event(decoder->fd);

	decoder->output.desc = pitcher_v4l2_output;
	decoder->capture.desc = pitcher_v4l2_capture;
	decoder->output.fd = decoder->fd;
	decoder->capture.fd = decoder->fd;
	decoder->output.desc.fd = decoder->fd;
	decoder->capture.desc.fd = decoder->fd;
	decoder->output.desc.events |= decoder->capture.desc.events;
	decoder->capture.desc.events = 0;

	decoder->output.start = start_dec;
	/*decoder->output.stop = stop_dec;*/
	decoder->output.sizeimage = decoder->sizeimage;
	decoder->output.is_end = is_decoder_output_finish;
	decoder->output.buffer_count = 4;
	snprintf(decoder->output.desc.name, sizeof(decoder->output.desc.name),
			"decoder output.%d", decoder->node.key);

	decoder->capture.pixelformat = decoder->node.pixelformat;
	decoder->capture.is_end = is_decoder_capture_finish;
	decoder->capture.buffer_count = 4;
	snprintf(decoder->capture.desc.name, sizeof(decoder->capture.desc.name),
			"decoder capture.%d", decoder->node.key);

	ret = pitcher_register_chn(decoder->node.context,
				&decoder->output.desc,
				&decoder->output);
	if (ret < 0) {
		PITCHER_ERR("regisger %s fail\n", decoder->output.desc.name);
		SAFE_CLOSE(decoder->fd, close);
		return ret;
	}
	decoder->output.chnno = ret;

	return init_decoder_platform(decoder);
}

static struct test_node *alloc_decoder_node(void)
{
	struct decoder_test_t *decoder;

	decoder = pitcher_calloc(1, sizeof(*decoder));
	if (!decoder)
		return NULL;

	decoder->node.key = -1;
	decoder->node.source = -1;
	decoder->fd = -1;
	decoder->node.type = TEST_TYPE_DECODER;
	decoder->node.pixelformat = V4L2_PIX_FMT_NV12;
	decoder->sizeimage = DEFAULT_WIDTH * DEFAULT_HEIGHT;

	decoder->node.init_node = init_decoder_node;
	decoder->node.free_node = free_decoder_node;
	decoder->node.set_source = set_decoder_source;
	decoder->node.get_source_chnno = get_deocder_source_chnno;
	decoder->node.get_sink_chnno = get_deocder_sink_chnno;

	decoder->output.chnno = -1;
	decoder->capture.chnno = -1;
	decoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	decoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	decoder->output.memory = V4L2_MEMORY_MMAP;
	decoder->capture.memory = V4L2_MEMORY_MMAP;
	decoder->output.pixelformat = V4L2_PIX_FMT_H264;
	decoder->capture.pixelformat = V4L2_PIX_FMT_NV12;

	return &decoder->node;
}

static int parse_decoder_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct decoder_test_t *decoder;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	decoder = container_of(node, struct decoder_test_t, node);

	if (!strcasecmp(option->name, "key")) {
		decoder->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		decoder->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "device")) {
		decoder->devnode = argv[0];
	} else if (!strcasecmp(option->name, "bs")) {
		uint32_t bs = strtol(argv[0], NULL, 0);

		if (bs < MIN_BS)
			bs = MIN_BS;
		decoder->sizeimage = bs * 1024;
	} else if (!strcasecmp(option->name, "framemode")) {
		decoder->platform.frame_mode = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt < 0)
			return fmt;
		decoder->node.pixelformat = fmt;
	}

	return RET_OK;
}

static int get_file_chnno(struct test_node *node)
{
	struct test_file_t *file;

	if (!node)
		return -RET_E_NULL_POINTER;

	file = container_of(node, struct test_file_t, node);

	return file->chnno;
}

static void free_file_node(struct test_node *node)
{
	struct test_file_t *file;

	if (!node)
		return;

	file = container_of(node, struct test_file_t, node);

	PITCHER_LOG("%s frame count : %ld\n",
			file->filename, file->frame_count);
	SAFE_CLOSE(file->chnno, pitcher_unregister_chn);
	if (file->virt && file->size) {
		munmap(file->virt, file->size);
		file->virt = NULL;
		file->size = 0;
	}
	SAFE_CLOSE(file->fd, close);
	SAFE_RELEASE(file->filp, fclose);
	SAFE_RELEASE(file, pitcher_free);
}

static int ifile_init_plane(struct pitcher_plane *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int ifile_uninit_plane(struct pitcher_plane *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int ifile_recycle_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct test_file_t *file = arg;
	int is_end = false;

	if (!file)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(file->chnno) && !file->end)
		pitcher_put_buffer_idle(file->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

static struct pitcher_buffer *ifile_alloc_buffer(void *arg)
{
	struct test_file_t *file = arg;
	struct pitcher_buffer_desc desc;

	if (!file || file->fd < 0)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size = get_image_size(file->node.pixelformat,
					file->node.width,
					file->node.height);
	desc.init_plane = ifile_init_plane;
	desc.uninit_plane = ifile_uninit_plane;
	desc.recycle = ifile_recycle_buffer;
	desc.arg = file;

	return pitcher_new_buffer(&desc);
}

static int ifile_checkready(void *arg, int *is_end)
{
	struct test_file_t *file = arg;

	if (!file || file->fd < 0)
		return false;

	if (is_termination())
		file->end = true;
	if (is_end)
		*is_end = file->end;

	if (file->end)
		return false;
	if (pitcher_poll_idle_buffer(file->chnno))
		return true;

	return false;
}

static int ifile_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct test_file_t *file = arg;
	struct pitcher_buffer *buffer;
	unsigned long size;

	if (!file || file->fd < 0)
		return -RET_E_INVAL;

	buffer = pitcher_get_idle_buffer(file->chnno);
	if (!buffer)
		return -RET_E_NOT_READY;

	size = buffer->planes[0].size;
	buffer->planes[0].bytesused = 0;

	if (file->offset < file->size) {
		buffer->planes[0].virt = file->virt + file->offset;
		if (size + file->offset <= file->size)
			buffer->planes[0].bytesused = size;
		else
			buffer->planes[0].bytesused = file->size - file->offset;
		file->offset += buffer->planes[0].bytesused;
		if (file->loop && file->offset >= file->size) {
			if (file->loop > 0)
				file->loop--;
			file->offset = 0;
		}
		file->frame_count++;
	} else {
		file->end = true;
	}

	if (file->frame_num > 0 && file->frame_count >= file->frame_num)
		file->end = true;

	if (file->offset >= file->size || file->end) {
		file->end = true;
		buffer->flags |= PITCHER_BUFFER_FLAG_LAST;
	}
	pitcher_push_back_output(file->chnno, buffer);

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return RET_OK;
}

static int init_ifile_node(struct test_node *node)
{
	struct test_file_t *file;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	file = container_of(node, struct test_file_t, node);
	if (!file->filename)
		return -RET_E_INVAL;

	file->fd = open(file->filename, O_RDONLY);
	if (file->fd < 0) {
		PITCHER_ERR("open %s fail\n", file->filename);
		return -RET_E_OPEN;
	}
	file->size = pitcher_get_file_size(file->filename);
	if (file->size <= 0) {
		PITCHER_ERR("invalid input file %s fail\n", file->filename);
		SAFE_CLOSE(file->fd, close);
		return -RET_E_OPEN;
	}
	file->virt = mmap(NULL, file->size, PROT_READ, MAP_SHARED, file->fd, 0);
	if (!file->virt) {
		PITCHER_ERR("mmap input file %s fail\n", file->filename);
		SAFE_CLOSE(file->fd, close);
		return -RET_E_MMAP;
	}

	file->desc.fd = -1;
	file->desc.check_ready = ifile_checkready;
	file->desc.runfunc = ifile_run;
	file->desc.buffer_count = 4;
	file->desc.alloc_buffer = ifile_alloc_buffer;
	snprintf(file->desc.name, sizeof(file->desc.name), "input.%s.%d",
			file->filename, file->node.key);

	ret = pitcher_register_chn(file->node.context, &file->desc, file);
	if (ret < 0) {
		PITCHER_ERR("register file input fail\n");
		munmap(file->virt, file->size);
		SAFE_CLOSE(file->fd, close);
		return ret;
	}
	file->chnno = ret;

	return RET_OK;
}

static struct test_node *alloc_ifile_node(void)
{
	struct test_file_t *file;

	file = pitcher_calloc(1, sizeof(*file));
	if (!file)
		return NULL;

	file->node.key = -1;
	file->node.source = -1;
	file->node.type = TEST_TYPE_FILEIN;
	file->node.pixelformat = DEFAULT_FMT;
	file->node.width = DEFAULT_WIDTH;
	file->node.height = DEFAULT_HEIGHT;
	file->node.framerate = DEFAULT_FRAMERATE;
	file->node.get_source_chnno = get_file_chnno;
	file->node.init_node = init_ifile_node;
	file->node.free_node = free_file_node;
	file->mode = "rb";
	file->chnno = -1;
	file->fd = -1;

	return &file->node;
}

static int parse_ifile_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct test_file_t *file;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	file = container_of(node, struct test_file_t, node);

	if (!strcasecmp(option->name, "key")) {
		file->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "name")) {
		file->filename = argv[0];
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt < 0)
			return fmt;
		file->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		file->node.width = strtol(argv[0], NULL, 0);
		file->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framenum")) {
		file->frame_num = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "loop")) {
		file->loop = strtol(argv[0], NULL, 0);
		PITCHER_LOG("set loop\n");
	}

	return RET_OK;
}

static int ofile_start(void *arg)
{
	struct test_file_t *file = arg;

	if (!file || !file->filp)
		return -RET_E_INVAL;

	file->end = false;

	return RET_OK;
}

static int ofile_checkready(void *arg, int *is_end)
{
	struct test_file_t *file = arg;

	if (!file || !file->filp)
		return false;

	if (is_force_exit())
		file->end = true;
	if (is_source_end(file->chnno))
		file->end = true;
	if (is_end)
		*is_end = file->end;

	return true;
}

static void ofile_insert_header(void *arg, struct pitcher_buffer *buffer)
{
	struct test_file_t *file = arg;
	unsigned long data_len = 0;
	int i;

	switch (file->node.pixelformat) {
	case V4L2_PIX_FMT_VP8:
		for (i = 0; i < buffer->count; i++)
			data_len += buffer->planes[i].bytesused;

		if (file->frame_count == 0)
			vp8_insert_ivf_seqhdr(file->filp, file->node.width,
				file->node.height, file->node.framerate);
		vp8_insert_ivf_pichdr(file->filp, data_len);
		break;
	default:
		break;
	}
}

static int ofile_run(void *arg, struct pitcher_buffer *buffer)
{
	struct test_file_t *file = arg;
	int i;

	if (!file || !file->filp)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;

	ofile_insert_header(file, buffer);

	for (i = 0; i < buffer->count; i++)
		fwrite(buffer->planes[i].virt, 1, buffer->planes[i].bytesused,
				file->filp);

	if (buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		file->end = true;
	file->frame_count++;

	return RET_OK;
}

static int init_ofile_node(struct test_node *node)
{
	struct test_file_t *file;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	file = container_of(node, struct test_file_t, node);
	if (!file->filename)
		return -RET_E_INVAL;

	file->filp = fopen(file->filename, file->mode);
	if (!file->filp) {
		PITCHER_ERR("open %s fail\n", file->filename);
		return -RET_E_OPEN;
	}

	file->desc.fd = -1;
	file->desc.start = ofile_start;
	file->desc.check_ready = ofile_checkready;
	file->desc.runfunc = ofile_run;
	snprintf(file->desc.name, sizeof(file->desc.name), "output.%s.%d",
			file->filename, file->node.key);

	ret = pitcher_register_chn(file->node.context, &file->desc, file);
	if (ret < 0) {
		PITCHER_ERR("register file output fail\n");
		SAFE_RELEASE(file->filp, fclose);
		return ret;
	}
	file->chnno = ret;

	return RET_OK;
}

static int set_ofile_source(struct test_node *node, struct test_node *src)
{
	struct test_file_t *file = NULL;

	if (!node || !src)
		return -RET_E_INVAL;

	file = container_of(node, struct test_file_t, node);
	file->node.width = src->width;
	file->node.height = src->height;
	file->node.pixelformat = src->pixelformat;
	file->node.framerate = src->framerate;

	return RET_OK;
}

static struct test_node *alloc_ofile_node(void)
{
	struct test_file_t *file;

	file = pitcher_calloc(1, sizeof(*file));
	if (!file)
		return NULL;

	file->node.key = -1;
	file->node.source = -1;
	file->node.type = TEST_TYPE_FILEOUT;

	file->node.init_node = init_ofile_node;
	file->node.get_sink_chnno = get_file_chnno;
	file->node.free_node = free_file_node;
	file->node.set_source = set_ofile_source;
	file->mode = "wb";
	file->chnno = -1;
	file->fd = -1;

	return &file->node;
}

static int parse_ofile_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct test_file_t *file;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	file = container_of(node, struct test_file_t, node);
	if (!strcasecmp(option->name, "key"))
		file->node.key = strtol(argv[0], NULL, 0);
	else if (!strcasecmp(option->name, "name"))
		file->filename = argv[0];
	else if (!strcasecmp(option->name, "source"))
		file->node.source = strtol(argv[0], NULL, 0);

	return RET_OK;
}

static int set_convert_source(struct test_node *node,
				struct test_node *src)
{
	struct convert_test_t *cvrt;

	if (!node || !src)
		return -RET_E_INVAL;

	cvrt = container_of(node, struct convert_test_t, node);
	cvrt->node.width = src->width;
	cvrt->node.height = src->height;
	cvrt->ifmt = src->pixelformat;

	return RET_OK;
}

static int recycle_convert_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct convert_test_t *cvrt = arg;
	int is_end = false;

	if (!cvrt)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(cvrt->chnno) && !cvrt->end)
		pitcher_put_buffer_idle(cvrt->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

static int convert_init_plane(struct pitcher_plane *plane, unsigned int index,
				void *arg)
{
	assert(plane);

	if (!plane->size)
		return RET_OK;

	return pitcher_alloc_plane(plane, index, arg);
}

static struct pitcher_buffer *alloc_convert_buffer(void *arg)
{
	struct convert_test_t *cvrt = arg;
	struct pitcher_buffer_desc desc;
	struct test_node *src_node;

	if (!cvrt)
		return NULL;

	src_node = nodes[cvrt->node.source];
	if (!src_node)
		return NULL;
	if (cvrt->node.width != src_node->width
	    || cvrt->node.height != src_node->height)
		set_convert_source(&cvrt->node, src_node);

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size = get_image_size(cvrt->node.pixelformat,
					cvrt->node.width,
					cvrt->node.height);
	desc.init_plane = convert_init_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.recycle = recycle_convert_buffer;
	desc.arg = cvrt;

	return pitcher_new_buffer(&desc);
}

static int convert_start(void *arg)
{
	struct convert_test_t *cvrt = arg;

	cvrt->end = false;

	return RET_OK;
}

static int convert_checkready(void *arg, int *is_end)
{
	struct convert_test_t *cvrt = arg;

	if (!cvrt)
		return false;

	if (is_force_exit())
		cvrt->end = true;
	if (is_source_end(cvrt->chnno))
		cvrt->end = true;
	if (is_end)
		*is_end = cvrt->end;
	if (cvrt->end)
		return false;
	if (!pitcher_chn_poll_input(cvrt->chnno))
		return false;
	if (cvrt->ifmt == cvrt->node.pixelformat)
		return true;
	if (pitcher_poll_idle_buffer(cvrt->chnno))
		return true;

	return false;
}

static int convert_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct convert_test_t *cvrt = arg;

	if (!cvrt || !pbuf)
		return -RET_E_INVAL;

	if (pbuf->planes[0].bytesused == 0) {
		SAFE_RELEASE(pbuf, pitcher_put_buffer);
		return 0;
	}

	if (cvrt->ifmt != cvrt->node.pixelformat) {
		struct pitcher_buffer *buffer;

		buffer = pitcher_get_idle_buffer(cvrt->chnno);
		if (!buffer)
			return -RET_E_NOT_READY;

		convert_frame(pbuf, buffer,
			      cvrt->ifmt, cvrt->node.pixelformat,
			      cvrt->node.width, cvrt->node.height);
		pitcher_push_back_output(cvrt->chnno, buffer);
		SAFE_RELEASE(buffer, pitcher_put_buffer);
		SAFE_RELEASE(pbuf, pitcher_put_buffer);
	} else {
		pitcher_push_back_output(cvrt->chnno, pbuf);
		SAFE_RELEASE(pbuf, pitcher_put_buffer);
	}

	return RET_OK;
}

static int init_convert_node(struct test_node *node)
{
	struct convert_test_t *cvrt;
	int ret;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	cvrt = container_of(node, struct convert_test_t, node);

	switch (cvrt->ifmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12_TILE:
	case V4L2_PIX_FMT_NV12_TILE_10BIT:
		break;
	default:
		return -RET_E_NOT_SUPPORT;
	}

	switch (cvrt->node.pixelformat) {
	case V4L2_PIX_FMT_NV12:
		break;
	default:
		return -RET_E_NOT_SUPPORT;
	}

	if (!cvrt->node.width || !cvrt->node.height) {
		src_node = nodes[cvrt->node.source];
		if (src_node && src_node->type == TEST_TYPE_FILEIN)
			return -RET_E_INVAL;
	}

	cvrt->desc.fd = -1;
	cvrt->desc.start = convert_start;
	cvrt->desc.check_ready = convert_checkready;
	cvrt->desc.runfunc = convert_run;
	cvrt->desc.buffer_count = 4;
	cvrt->desc.alloc_buffer = alloc_convert_buffer;
	snprintf(cvrt->desc.name, sizeof(cvrt->desc.name), "convert.%d",
			cvrt->node.key);

	ret = pitcher_register_chn(cvrt->node.context, &cvrt->desc, cvrt);
	if (ret < 0) {
		PITCHER_ERR("register convert fail\n");
		return ret;
	}
	cvrt->chnno = ret;

	return RET_OK;
}

static void free_convert_node(struct test_node *node)
{
	struct convert_test_t *cvrt;

	if (!node)
		return;

	cvrt = container_of(node, struct convert_test_t, node);
	SAFE_CLOSE(cvrt->chnno, pitcher_unregister_chn);
	SAFE_RELEASE(cvrt, pitcher_free);
}

static int get_convert_chnno(struct test_node *node)
{
	struct convert_test_t *cvrt;

	if (!node)
		return -RET_E_NULL_POINTER;

	cvrt = container_of(node, struct convert_test_t, node);

	return cvrt->chnno;
}

static struct test_node *alloc_convert_node(void)
{
	struct convert_test_t *cvrt;

	cvrt = pitcher_calloc(1, sizeof(*cvrt));
	if (!cvrt)
		return NULL;

	cvrt->node.key = -1;
	cvrt->node.source = -1;
	cvrt->node.type = TEST_TYPE_CONVERT;
	cvrt->chnno = -1;

	cvrt->node.pixelformat = V4L2_PIX_FMT_NV12;
	cvrt->node.init_node = init_convert_node;
	cvrt->node.free_node = free_convert_node;
	cvrt->node.get_source_chnno = get_convert_chnno;
	cvrt->node.get_sink_chnno = get_convert_chnno;
	cvrt->node.set_source = set_convert_source;

	return &cvrt->node;
}

static int parse_convert_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct convert_test_t *cvrt;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	cvrt = container_of(node, struct convert_test_t, node);

	if (!strcasecmp(option->name, "key")) {
		cvrt->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		cvrt->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt < 0)
			return fmt;
		cvrt->node.pixelformat = fmt;
	}

	return RET_OK;
}

static int get_parser_chnno(struct test_node *node)
{
	struct parser_test_t *parser;

	if (!node)
		return -RET_E_NULL_POINTER;

	parser = container_of(node, struct parser_test_t, node);

	return parser->chnno;
}

static int parser_checkready(void *arg, int *is_end)
{
	struct parser_test_t *parser = arg;

	if (!parser || parser->fd < 0)
		return false;

	if (is_termination())
		parser->end = true;
	if (is_end)
		*is_end = parser->end;

	if (parser->end)
		return false;
	if (pitcher_poll_idle_buffer(parser->chnno))
		return true;

	return false;
}

static int parser_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct parser_test_t *parser = arg;
	struct pitcher_buffer *buffer;
	struct pitcher_frame *frame;

	if (!parser || parser->fd < 0)
		return -RET_E_INVAL;

	frame = pitcher_parser_cur_frame(parser->p);

	if (!frame) {
		parser->end = true;
		return RET_OK;
	}

	buffer = pitcher_get_idle_buffer(parser->chnno);
	if (!buffer)
		return -RET_E_NOT_READY;

	if (parser->offset < parser->size) {
		buffer->planes[0].bytesused = frame->size;
		buffer->planes[0].virt = parser->virt + frame->offset;
		parser->offset += buffer->planes[0].bytesused;
		parser->frame_count++;
		pitcher_parser_to_next_frame(parser->p);

		if (frame->flag == PITCHER_BUFFER_FLAG_LAST || parser->offset >= parser->size) {
			if (parser->loop) {
				parser->loop--;
				parser->offset = 0;
				pitcher_parser_seek_to_begin(parser->p);
			} else {
				parser->end = true;
			}
		}
	} else {
		parser->end = true;
	}

	if (parser->frame_num > 0 && parser->frame_count >= parser->frame_num)
		parser->end = true;

	if (parser->end)
		buffer->flags |= PITCHER_BUFFER_FLAG_LAST;

	pitcher_push_back_output(parser->chnno, buffer);

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	return RET_OK;
}

static int parser_init_plane(struct pitcher_plane *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int parser_uninit_plane(struct pitcher_plane *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int parser_recycle_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct parser_test_t *parser = arg;
	int is_end = false;

	if (!parser)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(parser->chnno) && !parser->end)
		pitcher_put_buffer_idle(parser->chnno, buffer);
	else
		is_end = true;

	if (del)
		*del = is_end;

	return RET_OK;
}

static struct pitcher_buffer *parser_alloc_buffer(void *arg)
{
	struct parser_test_t *parser = arg;
	struct pitcher_buffer_desc desc;

	if (!parser || parser->fd < 0)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size = get_image_size(parser->node.pixelformat,
					parser->node.width,
					parser->node.height);
	desc.init_plane = parser_init_plane;
	desc.uninit_plane = parser_uninit_plane;
	desc.recycle = parser_recycle_buffer;
	desc.arg = parser;

	return pitcher_new_buffer(&desc);
}

static int init_parser_node(struct test_node *node)
{
	struct parser_test_t *parser;
	struct pitcher_parser p;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	parser = container_of(node, struct parser_test_t, node);
	if (!parser->filename)
		return -RET_E_INVAL;

	if (is_support_parser(parser->node.pixelformat) == false) {
		PITCHER_LOG("Format %c%c%c%c unsupported parser\n",
			    (parser->node.pixelformat) & 0xff,
			    (parser->node.pixelformat >> 8) & 0xff,
			    (parser->node.pixelformat >> 16) & 0xff,
			    (parser->node.pixelformat >> 24) & 0xff);

		return -RET_E_NOT_SUPPORT;
	}

	parser->fd = open(parser->filename, O_RDONLY);
	if (parser->fd < 0) {
		PITCHER_ERR("open %s fail\n", parser->filename);
		return -RET_E_OPEN;
	}
	parser->size = pitcher_get_file_size(parser->filename);
	if (parser->size <= 0) {
		PITCHER_ERR("invalid input file %s fail\n", parser->filename);
		SAFE_CLOSE(parser->fd, close);
		return -RET_E_OPEN;
	}
	parser->virt = mmap(NULL, parser->size, PROT_READ, MAP_SHARED, parser->fd, 0);
	if (!parser->virt) {
		PITCHER_ERR("mmap input file %s fail\n", parser->filename);
		SAFE_CLOSE(parser->fd, close);
		return -RET_E_MMAP;
	}

	parser->p = pitcher_new_parser();
	if (parser->p == NULL)
		return -RET_E_INVAL;
	p.format = parser->node.pixelformat;
	p.number = parser->frame_num;
	p.virt = parser->virt;
	p.size = parser->size;

	pitcher_init_parser(&p, parser->p);

	if (pitcher_parse(parser->p) != RET_OK) {
		SAFE_RELEASE(parser->p, pitcher_del_parser);
		return -RET_E_INVAL;
	}
	pitcher_parser_seek_to_begin(parser->p);

	if (parser->show)
		pitcher_parser_show((void *)parser->p);

	parser->desc.fd = -1;
	parser->desc.check_ready = parser_checkready;
	parser->desc.runfunc = parser_run;
	parser->desc.buffer_count = 4;
	parser->desc.alloc_buffer = parser_alloc_buffer;
	snprintf(parser->desc.name, sizeof(parser->desc.name), "parser.%s.%d",
			parser->filename, parser->node.key);

	ret = pitcher_register_chn(parser->node.context, &parser->desc, parser);
	if (ret < 0) {
		PITCHER_ERR("register file input fail\n");
		SAFE_RELEASE(parser->p, pitcher_del_parser);
		munmap(parser->virt, parser->size);
		SAFE_CLOSE(parser->fd, close);
		return ret;
	}
	parser->chnno = ret;

	return RET_OK;
}

static void free_parser_node(struct test_node *node)
{
	struct parser_test_t *parser;

	if (!node)
		return;

	parser = container_of(node, struct parser_test_t, node);

	SAFE_CLOSE(parser->chnno, pitcher_unregister_chn);
	if (parser->virt && parser->size) {
		munmap(parser->virt, parser->size);
		parser->virt = NULL;
		parser->size = 0;
	}
	SAFE_CLOSE(parser->fd, close);
	SAFE_RELEASE(parser->p, pitcher_del_parser);
	SAFE_RELEASE(parser, pitcher_free);
}

static struct test_node *alloc_parser_node(void)
{
	struct parser_test_t *parser;

	parser = pitcher_calloc(1, sizeof(*parser));
	if (!parser)
		return NULL;

	parser->node.key = -1;
	parser->node.source = -1;
	parser->node.type = TEST_TYPE_PARSER;
	parser->node.pixelformat = V4L2_PIX_FMT_H264;
	parser->node.get_source_chnno = get_parser_chnno;
	parser->node.init_node = init_parser_node;
	parser->node.free_node = free_parser_node;
	parser->mode = "rb";
	parser->chnno = -1;
	parser->fd = -1;

	return &parser->node;
}

static int parse_parser_option(struct test_node *node,
			       struct mxc_vpu_test_option *option,
			       char *argv[])
{
	struct parser_test_t *parser;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	parser = container_of(node, struct parser_test_t, node);
	if (!strcasecmp(option->name, "key")) {
		parser->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "name")) {
		parser->filename = argv[0];
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = get_pixelfmt_from_str(argv[0]);

		if (fmt < 0)
			return fmt;
		parser->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		parser->node.width = strtol(argv[0], NULL, 0);
		parser->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framenum")) {
		parser->frame_num = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "loop")) {
		parser->loop = strtol(argv[0], NULL, 0);
		PITCHER_LOG("set loop: %d\n", parser->loop);
	} else if (!strcasecmp(option->name, "show")) {
		parser->show = true;
	}

	return RET_OK;
}

struct mxc_vpu_test_subcmd subcmds[] = {
	{
		.subcmd = "ifile",
		.option = ifile_options,
		.type = TEST_TYPE_FILEIN,
		.parse_option = parse_ifile_option,
		.alloc_node = alloc_ifile_node,
	},
	{
		.subcmd = "camera",
		.option = camera_options,
		.type = TEST_TYPE_CAMERA,
		.parse_option = parse_camera_option,
		.alloc_node = alloc_camera_node,
	},
	{
		.subcmd = "encoder",
		.option = encoder_options,
		.type = TEST_TYPE_ENCODER,
		.parse_option = parse_encoder_option,
		.alloc_node = alloc_encoder_node,
	},
	{
		.subcmd = "decoder",
		.option = decoder_options,
		.type = TEST_TYPE_DECODER,
		.parse_option = parse_decoder_option,
		.alloc_node = alloc_decoder_node,
	},
	{
		.subcmd = "ofile",
		.option = ofile_options,
		.type = TEST_TYPE_FILEOUT,
		.parse_option = parse_ofile_option,
		.alloc_node = alloc_ofile_node,
	},
	{
		.subcmd = "convert",
		.option = convert_options,
		.type = TEST_TYPE_CONVERT,
		.parse_option = parse_convert_option,
		.alloc_node = alloc_convert_node,
	},
	{
		.subcmd = "parser",
		.option = parser_options,
		.type = TEST_TYPE_PARSER,
		.parse_option = parse_parser_option,
		.alloc_node = alloc_parser_node,
	},
};

struct mxc_vpu_test_subcmd *find_subcmd(const char *name)
{
	int i = 0;

	if (!name)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(subcmds); i++) {
		if (!subcmds[i].subcmd)
			break;
		if (!strcasecmp(subcmds[i].subcmd, name))
			return &subcmds[i];
	}

	return NULL;
}

struct mxc_vpu_test_option *find_option(struct mxc_vpu_test_option *options,
					const char *name)
{
	int i = 0;

	if (!options || !name)
		return NULL;

	while (1) {
		if (!options[i].name)
			break;
		if (!strcasecmp(options[i].name, name))
			return &options[i];
		i++;
	}

	return NULL;
}

static int get_key_count(void)
{
	int count = MAX_NODE_COUNT;

	if (count > 8 * sizeof(bitmask))
		count = 8 * sizeof(bitmask);

	return count;
}

static int check_key_is_valid(int key)
{
	int count = get_key_count();

	if (key < 0 || key >= count)
		return false;
	if (bitmask & (1 << key))
		return false;

	return true;
}

static int find_idle_key(void)
{
	int i;
	int count = get_key_count();

	for (i = 0; i < count; i++) {
		if (check_key_is_valid(i))
			return i;
	}

	return -1;
}

static void set_key(int key)
{
	int count = get_key_count();

	if (key < 0 || key >= count)
		return;

	bitmask |= (1 << key);
}

static struct test_node *parse_args(struct mxc_vpu_test_subcmd *subcmd,
		int argc, char *argv[], int start, int end)
{
	struct mxc_vpu_test_option *option;
	struct test_node *node = NULL;
	int ret;
	int i;

	if (!subcmd || !subcmd->option || !subcmd->parse_option ||
			!subcmd->alloc_node)
		return NULL;

	node = subcmd->alloc_node();
	if (!node) {
		PITCHER_ERR("unsupport subcmd type : %d\n", subcmd->type);
		return NULL;
	}

	for (i = start; i < end; i++) {
		if (strlen(argv[i]) < 2)
			continue;
		if (argv[i][0] != '-' && argv[i][1] != '-')
			continue;

		option = find_option(subcmd->option, argv[i] + 2);
		if (!option)
			continue;

		if (i + option->arg_num >= argc) {
			PITCHER_ERR("%s need %d arguments\n",
					argv[i] + 2, option->arg_num);
			goto error;
		}
		/*PITCHER_LOG("%s\n", option->desc);*/
		ret = subcmd->parse_option(node, option,
				option->arg_num ? argv + i + 1 : NULL);
		if (ret < 0) {
			PITCHER_ERR("%s parse %s fail\n",
					subcmd->subcmd, option->name);
			goto error;
		}
		i += option->arg_num;
	}

	if (node->key < 0)
		node->key = find_idle_key();
	if (check_key_is_valid(node->key)) {
		set_key(node->key);
	} else {
		PITCHER_ERR("%s invalid key (%d)\n", subcmd->subcmd, node->key);
		goto error;
	}

	return node;
error:
	SAFE_RELEASE(node, pitcher_free);
	return NULL;
}

static int show_help(int argc, char *argv[])
{
	int i;

	printf("Type 'HELP' to see the list. ");
	printf("Type 'HELP NAME' to find out more about subcmd 'NAME'\n");

	for (i = 0; i < ARRAY_SIZE(subcmds); i++) {
		struct mxc_vpu_test_option *option;

		if (argc > 2 && strcasecmp(argv[2], subcmds[i].subcmd))
			continue;
		printf("%s:\n", subcmds[i].subcmd);

		option = subcmds[i].option;
		while (option && option->name) {
			printf("\t%s\n", option->desc);
			option++;
		}
	}

	return 0;
}

static int parse_subcmds(int argc, char *argv[],
				struct test_node *nodes[], unsigned int count)
{
	struct mxc_vpu_test_subcmd *subcmd = NULL;
	struct test_node *node;
	int cmd_index;
	int index = 0;

	while (index < argc) {
		struct mxc_vpu_test_subcmd *curcmd;

		curcmd = find_subcmd(argv[index]);
		if (curcmd) {
			if (subcmd) {
				node = parse_args(subcmd, argc, argv,
							cmd_index + 1, index);
				if (node) {
					nodes[node->key] = node;
					node = NULL;
				} else {
					PITCHER_ERR("parse %s fail\n",
							subcmd->subcmd);
					return -RET_E_INVAL;
				}
			}
			subcmd = curcmd;
			cmd_index = index;
		}

		index++;
	}
	if (subcmd) {
		node = parse_args(subcmd, argc, argv, cmd_index + 1, index);
		if (node) {
			nodes[node->key] = node;
			node = NULL;
		} else {
			PITCHER_ERR("parse %s fail\n", subcmd->subcmd);
			return -RET_E_INVAL;
		}
	}

	return RET_OK;
}

static int connect_node(struct test_node *src, struct test_node *dst)
{
	int schn;
	int dchn;
	int ret;

	if (!src || !src->get_source_chnno || !dst || !dst->get_sink_chnno)
		return -RET_E_NULL_POINTER;

	schn = src->get_source_chnno(src);
	dchn = dst->get_sink_chnno(dst);
	if (schn < 0)
		return RET_OK;
	if (dchn < 0)
		return -RET_E_INVAL;

	PITCHER_LOG("connect <%d, %d>\n", src->key, dst->key);

	ret = pitcher_connect(schn, dchn);
	if (ret < 0)
		return ret;

	if (dst->frame_skip && src->framerate > dst->framerate)
		pitcher_set_skip(schn, dchn,
				src->framerate - dst->framerate,
				src->framerate);

	return RET_OK;
}

static int disconnect_node(struct test_node *src, struct test_node *dst)
{
	int schn;
	int dchn;

	if (!src || !src->get_source_chnno || !dst || !dst->get_sink_chnno)
		return -RET_E_NULL_POINTER;

	schn = src->get_source_chnno(src);
	dchn = dst->get_sink_chnno(dst);
	if (schn < 0 || dchn < 0)
		return -RET_E_INVAL;

	PITCHER_LOG("disconnect <%d, %d>\n", src->key, dst->key);

	return pitcher_disconnect(schn, dchn);
}

static int check_node_is_stopped(struct test_node *node)
{
	int chnno = -1;

	chnno = node->get_sink_chnno ? node->get_sink_chnno(node) : -1;
	if (chnno >= 0 && pitcher_get_status(chnno) != PITCHER_STATE_STOPPED)
		return false;

	chnno = node->get_source_chnno ? node->get_source_chnno(node) : -1;
	if (chnno >= 0 && pitcher_get_status(chnno) != PITCHER_STATE_STOPPED)
		return false;

	return true;
}

static int check_ctrl_ready(void *arg, int *is_end)
{
	int end = true;
	int i;

	if (is_termination())
		end = true;

	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		struct test_node *src;
		struct test_node *dst = nodes[i];
		int dchn;
		int schn;
		int ret = 0;

		if (!dst)
			continue;
		if (dst->source < 0 || dst->source >= MAX_NODE_COUNT)
			continue;
		src = nodes[dst->source];
		if (!src)
			continue;

		schn = src->get_source_chnno(src);
		if (schn < 0)
			continue;
		dchn = dst->get_sink_chnno(dst);
		if (dchn < 0)
			continue;
		if (pitcher_get_source(dchn) != schn) {
			ret = connect_node(src, dst);

			if (ret < 0) {
				PITCHER_ERR("can't connect <%d, %d>\n",
						src->key, dst->key);
				end = true;
				force_exit();
			}
			ret = pitcher_start_chn(schn);
			if (ret < 0) {
				PITCHER_ERR("start %d fail\n", src->key);
				end = true;
				force_exit();
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		if (!nodes[i])
			continue;

		if (!check_node_is_stopped(nodes[i])) {
			end = false;
			break;
		}
	}

	if (is_end)
		*is_end = end;

	return false;
}

static int ctrl_run(void *arg, struct pitcher_buffer *pbuf)
{
	return RET_OK;
}

int main(int argc, char *argv[])
{
	PitcherContext context = NULL;
	struct pitcher_unit_desc desc;
	int ctrl = -1;
	int ret;
	int i;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	printf("mxc_v4l2_vpu_test.out V%d.%d\n", VERSION_MAJOR, VERSION_MINOR);
	if (argc < 2 || !strcasecmp("help", argv[1]))
		return show_help(argc, argv);

	memset(nodes, 0, sizeof(nodes));
	ret = parse_subcmds(argc, argv, nodes, MAX_NODE_COUNT);
	if (ret < 0) {
		PITCHER_ERR("parse parameters fail\n");
		goto exit;
	}

	PITCHER_LOG("init\n");
	context = pitcher_init();
	if (!context) {
		PITCHER_ERR("pitcher init fail\n");
		goto exit;
	}
	memset(&desc, 0, sizeof(desc));
	desc.check_ready = check_ctrl_ready;
	desc.runfunc = ctrl_run;
	ctrl = pitcher_register_chn(context, &desc, NULL);
	if (ctrl < 0) {
		PITCHER_ERR("register ctrl chn fail\n");
		goto exit;
	}

	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		int source;

		if (!nodes[i])
			continue;
		nodes[i]->context = context;
		source = nodes[i]->source;
		if (source >= 0 && source < MAX_NODE_COUNT && nodes[source] &&
				nodes[i]->set_source) {
			ret = nodes[i]->set_source(nodes[i], nodes[source]);
			if (ret < 0)
				goto exit;
		}

		if (nodes[i]->init_node) {
			ret = nodes[i]->init_node(nodes[i]);
			if (ret < 0)
				goto exit;
		}
	}

	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		int source;

		if (!nodes[i])
			continue;

		source = nodes[i]->source;
		if (source < 0 || source >= MAX_NODE_COUNT || !nodes[source])
			continue;

		ret = connect_node(nodes[source], nodes[i]);
		if (ret < 0) {
			PITCHER_ERR("can't connect <%d, %d>\n",
					nodes[source]->key, nodes[i]->key);
			goto exit;
		}
	}
	ret = pitcher_start(context);
	if (ret < 0) {
		PITCHER_ERR("pitcher start fail, ret = %d\n", ret);
		goto exit;
	}
	pitcher_run(context);
	pitcher_stop(context);

	ret = 0;
exit:
	terminate();
	PITCHER_LOG("--------\n");
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		int source;

		if (!nodes[i])
			continue;

		source = nodes[i]->source;
		if (source < 0 || source >= MAX_NODE_COUNT || !nodes[source])
			continue;

		disconnect_node(nodes[source], nodes[i]);
	}
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		if (!nodes[i])
			continue;
		if (nodes[i]->free_node)
			nodes[i]->free_node(nodes[i]);
		nodes[i] = NULL;
	}

	SAFE_CLOSE(ctrl, pitcher_unregister_chn);
	PITCHER_LOG("release\n");
	SAFE_RELEASE(context, pitcher_release);

	PITCHER_LOG("memory : %ld\n", pitcher_memory_count());

	return ret;
}

