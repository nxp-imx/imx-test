/*
 * Copyright 2018-2023 NXP
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
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <execinfo.h>
#include "pitcher/pitcher_def.h"
#include "pitcher/pitcher.h"
#include "pitcher/pitcher_v4l2.h"
#include "pitcher/parse.h"
#include "pitcher/platform.h"
#include "pitcher/platform_8x.h"
#include "pitcher/convert.h"
#include "pitcher/dmabuf.h"
#include "mxc_v4l2_vpu_enc.h"

#define STRING(x)		#x
#define VERSION_MAJOR		2
#define VERSION_MINOR		3

#define MAX_NODE_COUNT		32
#define DEFAULT_FMT		PIX_FMT_NV12
#define DEFAULT_WIDTH		1920
#define DEFAULT_HEIGHT		1080
#define DEFAULT_FRAMERATE	30
#define MIN_BS			128

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
	uint32_t qp_max;
	uint32_t qp_min;
	uint32_t chroma_qp_index_offset;
	uint32_t bframes;
	uint32_t quality;

	struct v4l2_enc_roi_param roi;
	struct v4l2_enc_ipcm_param ipcm;

	uint32_t force_key;
	uint32_t new_bitrate;
	uint32_t nbr_no;
	uint32_t idrhdr;
	uint32_t cpbsize;

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
	unsigned int trans_type;
};

struct test_file_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	uint32_t alignment;

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
	struct pix_fmt_info format;
};

struct convert_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	uint32_t width;
	uint32_t height;
	struct v4l2_rect crop;
	uint32_t ifmt;
	int end;
	struct pix_fmt_info format;
	struct convert_ctx *ctx;
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
	int mem_type;
	void *virt;
	unsigned long size;
	unsigned long offset;
	int end;
	int loop;
	int show;

	unsigned int skip;
	struct {
		unsigned int enable;
		unsigned int pos_seek;
		unsigned int pos_new;
	} seek;

	Parser p;
};

struct g2d_cvt_test_t {
	struct test_node node;
	struct pitcher_unit_desc desc;
	int chnno;
	uint32_t ifmt;
	struct pix_fmt_info format;
	struct v4l2_rect crop;
	struct convert_ctx *ctx;
	unsigned long frame_count;
	int end;
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

static int flush_enc(struct v4l2_component_t *component);
static int flush_dec(struct v4l2_component_t *component);

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
	force_exit();

	return 0;
}

int is_force_exit(void)
{
	if (g_exit & FORCE_EXIT_MASK)
		return true;

	return false;
}

int is_termination(void)
{
	return g_exit;
}

#define DUMP_STACK_DEPTH_MAX 16
void dump_backtrace(void)
{
	void *stack_trace[DUMP_STACK_DEPTH_MAX] = {0};
	char **stack_strings = NULL;
	int stack_depth = 0;
	int i;

	stack_depth = backtrace(stack_trace, DUMP_STACK_DEPTH_MAX);
	PITCHER_LOG("backtrace() returned %d addresses\n", stack_depth);
	stack_strings = (char **)backtrace_symbols(stack_trace, stack_depth);
	if (stack_strings == NULL) {
		PITCHER_ERR("backtrace_symbols\n");
		return;
	}

	for (i =0; i < stack_depth; i++)
		PITCHER_LOG("[%2d] %s\n", i, stack_strings[i]);
	free(stack_strings);
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
	case SIGSEGV:
		PITCHER_ERR("Segmentation fault\n");
		dump_backtrace();
		exit(-1);
	}
}

static int subscribe_event(int fd)
{
	struct v4l2_event_subscription sub;

	memset(&sub, 0, sizeof(sub));
	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);

	memset(&sub, 0, sizeof(sub));
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

int is_source_end(int chnno)
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

static int change_encoder_dynamically(struct v4l2_component_t *component)
{
	struct encoder_test_t *encoder;
	int fd;
	unsigned long frame_count;

	if (!component)
		return 0;

	encoder = container_of(component, struct encoder_test_t, output);
	fd = component->fd;
	frame_count = component->frame_count;

	if (encoder->force_key && frame_count == encoder->force_key)
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);

	if (encoder->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR &&
			encoder->new_bitrate && frame_count == encoder->nbr_no)
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE, encoder->new_bitrate);

	return 0;
}

static int is_encoder_output_finish(struct v4l2_component_t *component)
{
	struct encoder_test_t *encoder;
	int is_end = false;

	if (!component)
		return true;

	encoder = container_of(component, struct encoder_test_t, output);


	if (is_source_end(encoder->output.chnno))
		flush_enc(component);

	if (component->eos_received) {
		encoder->capture.eos_received = true;
		component->eos_received = false;
	}
	if (is_force_exit() || encoder->capture.end) {
		is_end = true;
		if (!component->frame_count)
			force_exit();
	}

	if (!is_end && pitcher_get_status(encoder->capture.chnno) == PITCHER_STATE_STOPPED) {
		pitcher_start_chn(encoder->capture.chnno);
	}

	return is_end;
}

static int is_encoder_capture_finish(struct v4l2_component_t *component)
{
	if (!component)
		return true;

	return is_force_exit();
}

static void switch_fmt_to_tile(unsigned int *fmt)
{
	switch (*fmt) {
	case PIX_FMT_NV12:
		*fmt = PIX_FMT_NV12_8L128;
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
	decoder->node.bytesperline = decoder->capture.bytesperline;
	if (decoder->platform.type == IMX_8X) {
		switch_fmt_to_tile(&decoder->node.pixelformat);
		if (decoder->capture.format.format != decoder->node.pixelformat) {
			decoder->capture.format.format = decoder->node.pixelformat;
			pitcher_get_pix_fmt_info(&decoder->capture.format, 0);
		}
	}
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
		decoder->capture.pixelformat = PIX_FMT_NONE;
		decoder->capture.width = 0;
		decoder->capture.height = 0;
		ret = pitcher_start_chn(chnno);
		if (ret < 0) {
			force_exit();
			return ret;
		}

		sync_decoder_node_info(decoder);
		PITCHER_LOG("decoder capture: %s %d x %d, count = %ld\n",
			pitcher_get_format_name(decoder->capture.pixelformat),
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

static int seek_decoder(struct decoder_test_t *decoder)
{
	if (!decoder->output.seek)
		return RET_OK;

	if (pitcher_get_status(decoder->capture.chnno) != PITCHER_STATE_ACTIVE)
		return RET_OK;
	if (decoder->capture.frame_count < decoder->node.seek_thd)
		return RET_OK;

	decoder->output.seek = false;
	PITCHER_LOG("seek at %ld frames decoded\n", decoder->capture.frame_count);

	decoder->output.desc.stop(&decoder->output);
	decoder->capture.desc.stop(&decoder->capture);
	decoder->capture.desc.start(&decoder->capture);
	decoder->output.desc.start(&decoder->output);

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
		flush_dec(component);

	if (component->eos_received) {
		if (decoder->capture.chnno >= 0)
			decoder->capture.eos_received = true;
		else
			decoder->capture.end = true;
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
	if (component->seek)
		seek_decoder(decoder);

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

	return is_force_exit();
}

static int flush_enc(struct v4l2_component_t *component)
{
	struct v4l2_encoder_cmd cmd;
	int ret;


	if (!component || component->fd < 0 || !component->enable)
		return -RET_E_INVAL;

	PITCHER_LOG("flush encoder\n");

	cmd.cmd = V4L2_ENC_CMD_STOP;
	ret = ioctl(component->fd, VIDIOC_ENCODER_CMD, &cmd);
	if (ret < 0)
		PITCHER_ERR("stop enc fail\n");
	component->enable = false;

	return RET_OK;
}

static int flush_dec(struct v4l2_component_t *component)
{
	struct v4l2_decoder_cmd cmd;
	int ret;

	if (!component || component->fd < 0 || !component->enable)
		return -RET_E_INVAL;

	PITCHER_LOG("flush decoder\n");
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
	{"fmt",  1, "--fmt <fmt>\n\t\t\tassign input file pixel format, encode support\n\
		     \r\t\t\tinput format nv12, i420, nv21, yuyv, rgb565, bgr565,\n\
		     \r\t\t\trgb555, rgba, bgr32, argb, rgbx"},
	{"size", 2, "--size <width> <height>\n\t\t\tassign input file resolution"},
	{"alignment", 1, "--alignment <alignment>\n\t\t\tassign line alignment"},
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
	{"transtype", 1, "--transtype <type>\n\t\t\tset buffer type of transfer to sink node,\n\
			\r\t\t\t1:mmap, 2: userptr, 3: onvelay(not support), 4: dmabuf(default)"},
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
	{"mode", 1, "--mode <mode>\n\t\t\tset h264 mode, 0:vbr, 1:cbr(default), 2:constant quality 0xf:constant qp"},
	{"qp", 1, "--qp <qp>\n\t\t\tset quantizer parameter, 0~51"},
	{"qprange", 2, "--qprange <qp_min> <qp_max>\n\t\t\tset quantizer parameter range, 0~51"},
	{"chroma_qp_offset", 1, "--chroma_qp_offset <offset>\n\t\t\tset h264/h265 chroma qp index offset, -12~12"},
	{"bitrate", 1, "--bitrate <br>\n\t\t\tset encoder target bitrate, the unit is b"},
	{"peak", 1, "--peak <br>\n\t\t\tset encoder peak bitrate, the unit is b"},
	{"bframes", 1, "--bframes <number>\n\t\t\tset the number of b frames"},
	{"crop", 4, "--crop <left> <top> <width> <height>\n\t\t\tset h264 crop position and size"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign encode pixel format, support h264, h265, vp8, vp9"},
	{"roi", 5, "--roi <left> <top> <width> <height> <qp_delta>\n\t\t\tenable roi"},
	{"ipcm", 4, "--ipcm <left> <top> <width> <height>\n\t\t\tenable ipcm"},
	{"force", 1, "--force <no>\n\t\t\tforce a key frame at position <no>"},
	{"nbr", 2, "--nbr <br> <no>\n\t\t\tset encoder new target bitrate since frame <no>, the unit is b"},
	{"seqhdr", 1, "--seqhdr <set>\n\t\t\tset encoder idr sequence header"},
	{"cpbsize", 1, "--cpbsize <size>\n\t\t\tset encoder coded picture buffer size, the unit is b"},
	{"quality", 1, "--quality <quality>\n\t\t\tset jpeg quality"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option decoder_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset h264 encoder input key number"},
	{"device", 1, "--device <devnode>\n\t\t\tassign encoder video device node"},
	{"bs", 1, "--bs <bs count>\n\t\t\tSpecify the count of input buffer block size, the unit is Kb."},
	{"framemode", 1, "--framemode <mode>\n\t\t\tSpecify input frame mode, 0: frame level(default), 1: non-frame level"},
	{"disreorder", 1, "--disreorder <mode>\n\t\t\tEnable disreorder(low latency) mode, 0: no(default), 1: yes"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign encode pixel format, support nv12, nv21,\n\
		    \r\t\t\ti420, dtrc, dtrc10, P010, nvx2, rfc, rfcx, nv16"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option convert_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign output pixel format, support mutual convert of nv12 and i420"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option g2d_cvt_options[] = {
	{"key", 1, "--key <key>\n\t\t\tassign key number"},
	{"source", 1, "--source <key no>\n\t\t\tset source key number"},
	{"fmt", 1, "--fmt <fmt>\n\t\t\tassign output pixel format, support mutual convert of nv12 and i420"},
	{NULL, 0, NULL},
};

struct mxc_vpu_test_option parser_options[] = {
	{"key",  1, "--key <key>\n\t\t\tassign key number"},
	{"name", 1, "--name <filename>\n\t\t\tassign parse file name"},
	{"fmt",  1, "--fmt <fmt>\n\t\t\tassign input file pixel format, support h264, h265, mpeg2, mpeg4,\n\
		     \r\t\t\th263, jpeg, vc1l, vc1g, xvid, vp9, vp8, vp6, avs, rv, spk, divx"},
	{"size", 2, "--size <width> <height>\n\t\t\tset size"},
	{"framenum", 1, "--framenum <number>\n\t\t\tset input/parse frame number"},
	{"loop", 1, "--loop <loop times>\n\t\t\tset input loops times"},
	{"skip", 1, "--skip <number>\n\t\t\tset skip frame number"},
	{"seek", 3, "--seek <input number> <decode number> <new position>\n\t\t\tseek"},
	{"show", 0, "--show\n\t\t\tshow size and offset of per frame"},
	{NULL, 0, NULL},
};

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
	camera->trans_type = V4L2_MEMORY_DMABUF;

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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		camera->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		camera->node.width = strtol(argv[0], NULL, 0);
		camera->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framerate")) {
		camera->node.framerate = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "framenum")) {
		camera->frame_num = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "transtype")) {
		camera->trans_type = strtol(argv[0], NULL, 0);
		if (camera->trans_type == V4L2_MEMORY_OVERLAY) {
			PITCHER_ERR("V4L2_MEMORY_OVERLAY not support yet, change to V4L2_MEMORY_USERPTR\n");
			camera->trans_type = V4L2_MEMORY_USERPTR;
		}
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
	switch (src->pixelformat) {
	case PIX_FMT_YUYV:
	case PIX_FMT_RGB565:
	case PIX_FMT_BGR565:
	case PIX_FMT_RGB555:
		encoder->output.bytesperline = src->width * 2;
		break;
	case PIX_FMT_RGBA:
	case PIX_FMT_BGR32:
	case PIX_FMT_ARGB:
	case PIX_FMT_RGBX:
		encoder->output.bytesperline = src->width * 4;
		break;
	default:
		encoder->output.bytesperline = src->width;
		break;
	}

	if (encoder->output.bytesperline < src->bytesperline)
		encoder->output.bytesperline = src->bytesperline;

	if (src->type == TEST_TYPE_CAMERA) {
		struct camera_test_t *camera = container_of(src, struct camera_test_t, node);

		encoder->output.memory = camera->trans_type;
		encoder->node.frame_skip = true;
	}
	if (src->type == TEST_TYPE_DECODER) {
		encoder->output.memory = V4L2_MEMORY_DMABUF;
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

static int set_encoder_roi(int fd, struct v4l2_enc_roi_param *param)
{
	struct v4l2_ext_control ctrl;
	struct v4l2_ext_controls ctrls;
	struct v4l2_enc_roi_params roi;
	int roi_count = 0;
	int ret;

	if (!param || !param->enable)
		return -RET_E_INVAL;

	ret = get_ctrl(fd, V4L2_CID_ROI_COUNT, &roi_count);
	if (ret < 0) {
		PITCHER_ERR("get roi count fail\n");
		return ret;
	}

	memset(&ctrls, 0, sizeof(ctrls));
	memset(&ctrl, 0, sizeof(ctrl));
	memset(&roi, 0, sizeof(roi));

	ctrls.controls = &ctrl;
	ctrls.count = 1;

	ctrl.id = V4L2_CID_ROI;
	/*ctrl.string = (char *)&roi;*/
	ctrl.ptr = (void *)&roi;
	ctrl.size = sizeof(roi);

	roi.num_roi_regions = roi_count;
	roi.roi_params[0] = *param;

	ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls);
	if (ret < 0) {
		PITCHER_ERR("set roi fail\n");
		return -RET_E_INVAL;
	}

	memset(&roi, 0, sizeof(roi));
	ret = ioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls);
	if (ret < 0) {
		PITCHER_ERR("set roi fail\n");
		return -RET_E_INVAL;
	}

	PITCHER_LOG("%d [0]%d %d,%d %dx%d %d\n",
			roi.num_roi_regions,
			roi.roi_params[0].enable,
			roi.roi_params[0].rect.left,
			roi.roi_params[0].rect.top,
			roi.roi_params[0].rect.width,
			roi.roi_params[0].rect.height,
			roi.roi_params[0].qp_delta);

	return 0;
}

static int set_encoder_ipcm(int fd, struct v4l2_enc_ipcm_param *param)
{
	struct v4l2_ext_control ctrl;
	struct v4l2_ext_controls ctrls;
	struct v4l2_enc_ipcm_params ipcm;
	int ipcm_count = 0;
	int ret;

	if (!param || !param->enable)
		return -RET_E_INVAL;

	ret = get_ctrl(fd, V4L2_CID_IPCM_COUNT, &ipcm_count);
	if (ret < 0) {
		PITCHER_ERR("get ipcm count fail\n");
		return ret;
	}
	memset(&ctrls, 0, sizeof(ctrls));
	memset(&ctrl, 0, sizeof(ctrl));
	memset(&ipcm, 0, sizeof(ipcm));

	ctrls.controls = &ctrl;
	ctrls.count = 1;

	ctrl.id = V4L2_CID_IPCM;
	ctrl.ptr = (void *)&ipcm;
	ctrl.size = sizeof(struct v4l2_enc_ipcm_params);

	ipcm.num_ipcm_regions = ipcm_count;
	ipcm.ipcm_params[0] = *param;

	ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls);
	if (ret < 0) {
		PITCHER_ERR("set ipcm fail\n");
		return -RET_E_INVAL;
	}

	memset(&ipcm, 0, sizeof(ipcm));
	ret = ioctl(fd, VIDIOC_G_EXT_CTRLS, &ctrls);
	if (ret < 0) {
		PITCHER_ERR("set ipcm fail\n");
		return -RET_E_INVAL;
	}

	PITCHER_LOG("%d [0]%d %d,%d %dx%d\n",
			ipcm.num_ipcm_regions,
			ipcm.ipcm_params[0].enable,
			ipcm.ipcm_params[0].rect.left,
			ipcm.ipcm_params[0].rect.top,
			ipcm.ipcm_params[0].rect.width,
			ipcm.ipcm_params[0].rect.height);
	return 0;
}

static int set_encoder_parameters(struct encoder_test_t *encoder)
{
	int fd;
	int profile_id = 0;
	int level_id = 0;
	int qp_i_id, qp_p_id, qp_b_id;
	int qp_max_id, qp_min_id;
	int cpbsize_id = 0;
	int chroma_offset_id = 0;

	qp_i_id = qp_p_id = qp_b_id = 0;
	qp_max_id = qp_min_id = 0;

	if (!encoder || encoder->fd < 0)
		return -RET_E_INVAL;

	fd = encoder->fd;

	if (encoder->new_bitrate == encoder->target_bitrate) {
		encoder->new_bitrate = 0;
		encoder->nbr_no = 0;
	}
	if (!encoder->nbr_no)
		encoder->new_bitrate = 0;

	switch (encoder->capture.pixelformat) {
	case PIX_FMT_H264:
		profile_id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
		level_id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
		qp_i_id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
		qp_p_id = V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP;
		qp_b_id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP;
		qp_max_id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
		qp_min_id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
		chroma_offset_id = V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET;
		cpbsize_id = V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE;
		validate_h264_profile_level(encoder);
		break;
	case PIX_FMT_H265:
		profile_id = V4L2_CID_MPEG_VIDEO_HEVC_PROFILE;
		level_id = V4L2_CID_MPEG_VIDEO_HEVC_LEVEL;
		qp_i_id = V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP;
		qp_p_id = V4L2_CID_MPEG_VIDEO_HEVC_P_FRAME_QP;
		qp_b_id = V4L2_CID_MPEG_VIDEO_HEVC_B_FRAME_QP;
		qp_max_id = V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP;
		qp_min_id = V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP;
		/* No HEVC cpbsize and chroma_qp_index_offset v4l2-control, use H264  */
		chroma_offset_id = V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET;
		cpbsize_id = V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE;
		validate_h265_profile_level(encoder);
		break;
	case PIX_FMT_VP8:
		profile_id = V4L2_CID_MPEG_VIDEO_VP8_PROFILE;
		validate_vpx_profile_level(encoder);
		qp_i_id = V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP;
		qp_p_id = V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP;
		qp_max_id = V4L2_CID_MPEG_VIDEO_VPX_MAX_QP;
		qp_min_id = V4L2_CID_MPEG_VIDEO_VPX_MIN_QP;
		break;
	case PIX_FMT_JPEG:
		set_ctrl(fd, V4L2_CID_JPEG_COMPRESSION_QUALITY, encoder->quality);
		return 0;
	default:
		return -RET_E_INVAL;
	}
	set_ctrl(fd, profile_id, encoder->profile);
	if (level_id)
		set_ctrl(fd, level_id, encoder->level);
	if (encoder->bitrate_mode == 0xf) {
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 0);
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0);
	} else {
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 1);
		set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, encoder->bitrate_mode);
	}
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE, encoder->target_bitrate);
	if (!encoder->peak_bitrate)
		encoder->peak_bitrate = encoder->target_bitrate;
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, encoder->peak_bitrate);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, encoder->gop);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_B_FRAMES, encoder->bframes);
	set_ctrl(fd, qp_i_id, encoder->qp);
	set_ctrl(fd, qp_p_id, encoder->qp);
	if (encoder->chroma_qp_index_offset)
		set_ctrl(fd, chroma_offset_id, encoder->chroma_qp_index_offset);
	if (qp_b_id)
		set_ctrl(fd, qp_b_id, encoder->qp);
	if (encoder->qp_min)
		set_ctrl(fd, qp_min_id, encoder->qp_min);
	if (encoder->qp_max)
		set_ctrl(fd, qp_max_id, encoder->qp_max);
	if (encoder->roi.enable)
		set_encoder_roi(fd, &encoder->roi);
	if (encoder->ipcm.enable)
		set_encoder_ipcm(fd, &encoder->ipcm);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, encoder->idrhdr);

	if (encoder->cpbsize)
		set_ctrl(fd, cpbsize_id, encoder->cpbsize);

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

	if (encoder->node.pixelformat == PIX_FMT_JPEG)
		encoder->node.framerate = 0;
	encoder->output.desc = pitcher_v4l2_output;
	encoder->capture.desc = pitcher_v4l2_capture;
	encoder->capture.fd = encoder->fd;
	encoder->output.fd = encoder->fd;
	encoder->output.desc.fd = encoder->fd;
	encoder->capture.desc.fd = encoder->fd;
	encoder->output.desc.events |= encoder->capture.desc.events;
	encoder->capture.desc.events = 0;

	encoder->capture.pixelformat = encoder->node.pixelformat;
	encoder->capture.width = encoder->node.width;
	encoder->capture.height = encoder->node.height;
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
	encoder->output.run_hook = change_encoder_dynamically;
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
	encoder->node.pixelformat = PIX_FMT_H264;
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
	encoder->quality = 75;
	encoder->idrhdr = 1;
	encoder->output.chnno = -1;
	encoder->capture.chnno = -1;

	encoder->output.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	encoder->capture.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	encoder->output.memory = V4L2_MEMORY_MMAP;
	encoder->capture.memory = V4L2_MEMORY_MMAP;
	encoder->output.pixelformat = PIX_FMT_NV12;
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
	} else if (!strcasecmp(option->name, "qp_range")) {
		encoder->qp_min = strtol(argv[0], NULL, 0);
		encoder->qp_max = strtol(argv[1], NULL, 0);
	}  else if (!strcasecmp(option->name, "chroma_qp_offset")) {
		encoder->chroma_qp_index_offset = strtol(argv[0], NULL, 0);
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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt != PIX_FMT_NONE)
			encoder->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "roi")) {
		encoder->roi.enable = 1;
		encoder->roi.rect.left = strtol(argv[0], NULL, 0);
		encoder->roi.rect.top = strtol(argv[1], NULL, 0);
		encoder->roi.rect.width = strtol(argv[2], NULL, 0);
		encoder->roi.rect.height = strtol(argv[3], NULL, 0);
		encoder->roi.qp_delta = strtol(argv[4], NULL, 0);
	} else if (!strcasecmp(option->name, "ipcm")) {
		encoder->ipcm.enable = 1;
		encoder->ipcm.rect.left = strtol(argv[0], NULL, 0);
		encoder->ipcm.rect.top = strtol(argv[1], NULL, 0);
		encoder->ipcm.rect.width = strtol(argv[2], NULL, 0);
		encoder->ipcm.rect.height = strtol(argv[3], NULL, 0);
	} else if (!strcasecmp(option->name, "force")) {
		encoder->force_key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "nbr")) {
		encoder->new_bitrate = strtol(argv[0], NULL, 0);
		encoder->nbr_no = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "seqhdr")) {
		encoder->idrhdr = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "cpbsize")) {
		encoder->cpbsize = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "quality")) {
		encoder->quality = strtol(argv[0], NULL, 0);
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
	case PIX_FMT_H264:
	case PIX_FMT_H265:
	case PIX_FMT_MPEG2:
	case PIX_FMT_MPEG4:
	case PIX_FMT_H263:
	case PIX_FMT_JPEG:
	case PIX_FMT_VC1L:
	case PIX_FMT_VC1G:
	case PIX_FMT_XVID:
	case PIX_FMT_VP8:
	case PIX_FMT_VP9:
	case PIX_FMT_VP6:
	case PIX_FMT_AVS:
	case PIX_FMT_RV30:
	case PIX_FMT_RV40:
	case PIX_FMT_SPK:
	case PIX_FMT_DIVX:
		break;
	default:
		return -RET_E_NOT_SUPPORT;
	}

	decoder->output.pixelformat = src->pixelformat;
	decoder->output.width = src->width;
	decoder->output.height = src->height;
	node->seek_thd = src->seek_thd;

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
	    !strcasecmp((const char*)cap.driver, "imx vpu decoder") ||
	    !strcasecmp((const char*)cap.driver, "amphion-vpu")) {
		decoder->platform.type = IMX_8X;
		decoder->platform.set_decoder_parameter = set_decoder_parameter;
		decoder->output.fixed_timestamp = 1;
		decoder->output.timestamp.tv_sec = -1;
		decoder->output.timestamp.tv_usec = 234568;	//-765432000
	} else if (!strcasecmp((const char*)cap.driver, "vsi_v4l2")) {
		decoder->platform.type = IMX_8M;
		decoder->platform.set_decoder_parameter = set_decoder_parameter;
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
	PITCHER_LOG("decode capture format: %s\n",
			pitcher_get_format_name(decoder->capture.pixelformat));
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
	pitcher_set_ignore_pollerr(decoder->output.chnno, true);

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
	decoder->node.pixelformat = PIX_FMT_NONE;
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
	decoder->output.pixelformat = PIX_FMT_H264;
	decoder->capture.pixelformat = PIX_FMT_NV12;

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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		decoder->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "disreorder")) {
		decoder->platform.dis_reorder = strtol(argv[0], NULL, 0);
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

static int ifile_init_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int ifile_uninit_plane(struct pitcher_buf_ref *plane,
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
	desc.plane_size[0] = file->format.size;
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
	buffer->format = &file->format;
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

	memset(&file->format, 0, sizeof(file->format));
	file->format.format = file->node.pixelformat;
	file->format.width = file->node.width;
	file->format.height = file->node.height;
	pitcher_get_pix_fmt_info(&file->format, file->alignment);
	node->bytesperline = file->alignment;
	if (file->node.pixelformat == PIX_FMT_JPEG) {
		file->format.size = file->size;
		file->format.width = file->node.width = 0;
		file->format.height = file->node.height = 0;
	}

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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		file->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		file->node.width = strtol(argv[0], NULL, 0);
		file->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "alignment")) {
		file->alignment = strtol(argv[0], NULL, 0);
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
	case PIX_FMT_VP8:
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

static int ofile_output_by_line(void *arg, struct pitcher_buffer *buffer)
{
	struct test_file_t *file = arg;
	struct pix_fmt_info *format = buffer->format;
	struct v4l2_rect *crop = buffer->crop;
	const struct pixel_format_desc *desc = buffer->format->desc;
	struct pitcher_buf_ref splane;
	int w, h, line;
	int planes_line;
	int i, j;
	unsigned long offset;

	for (i = 0; i < format->num_planes; i++) {
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

		planes_line = format->planes[i].line;
		offset = planes_line * top;
		for (j = 0; j < h; j++) {
			fwrite((uint8_t *)splane.virt + offset + left, 1, line, file->filp);
			offset += planes_line;
		}
	}

	return 0;
}

static int ofile_run(void *arg, struct pitcher_buffer *buffer)
{
	struct test_file_t *file = arg;
	int i;

	if (!file || !file->filp)
		return -RET_E_INVAL;
	if (!buffer)
		return -RET_E_NOT_READY;

	if (!buffer->count || !buffer->planes || !buffer->planes[0].bytesused)
		goto exit;

	ofile_insert_header(file, buffer);

	if (buffer->format &&
	    buffer->format->format < PIX_FMT_COMPRESSED &&
	    buffer->format->format != PIX_FMT_RFC &&
	    buffer->format->format != PIX_FMT_RFCX &&
	    buffer->format->desc->tile_ws == 0 &&
	    buffer->format->desc->tile_hs == 0) {
		ofile_output_by_line(file, buffer);
	} else {
		for (i = 0; i < buffer->count; i++)
			fwrite(buffer->planes[i].virt, 1, buffer->planes[i].bytesused,
					file->filp);
	}

	file->frame_count++;
exit:
	if (buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		file->end = true;

	return RET_OK;
}

static int init_ofile_node(struct test_node *node)
{
	struct test_file_t *file;

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

static int get_ofile_chnno(struct test_node *node)
{
	struct test_file_t *file;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	file = container_of(node, struct test_file_t, node);
	if (file->chnno >= 0)
		return file->chnno;

	src_node = nodes[file->node.source];
	if (src_node->get_source_chnno(src_node) < 0)
		return file->chnno;

	file->chnno = pitcher_register_chn(file->node.context, &file->desc, file);

	return file->chnno;
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
	file->node.get_sink_chnno = get_ofile_chnno;
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
	/* width and height have to align to 8 */
	cvrt->node.width = ALIGN(src->width, 8);
	cvrt->node.height = ALIGN(src->height, 8);
	/*cvrt->node.bytesperline = src->bytesperline;*/
	cvrt->ifmt = src->pixelformat;

	memset(&cvrt->format, 0, sizeof(cvrt->format));
	cvrt->format.format = cvrt->node.pixelformat;
	cvrt->format.width = cvrt->node.width;
	cvrt->format.height = cvrt->node.height;
	if (src->type == TEST_TYPE_DECODER) {
		struct decoder_test_t *decoder;

		decoder = container_of(src, struct decoder_test_t, node);
		memcpy(&cvrt->crop, &decoder->capture.crop, sizeof(cvrt->crop));
	}
	pitcher_get_pix_fmt_info(&cvrt->format, 0);
	cvrt->format.width = src->width;
	cvrt->format.height = src->height;

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

static int convert_init_plane(struct pitcher_buf_ref *plane, unsigned int index,
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

	if (cvrt->node.width != src_node->width || cvrt->node.height != src_node->height)
		set_convert_source(&cvrt->node, src_node);

	memset(&desc, 0, sizeof(desc));
	desc.plane_count = 1;
	desc.plane_size[0] = get_image_size(cvrt->node.pixelformat,
					cvrt->node.width,
					cvrt->node.height,
					0);
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

	if (pbuf->planes[0].bytesused == 0)
		return 0;

	if (cvrt->ifmt != cvrt->node.pixelformat) {
		struct pitcher_buffer *buffer;

		if (!cvrt->ctx)
			cvrt->ctx = pitcher_create_sw_convert();
		if (!cvrt->ctx)
			return -RET_E_INVAL;

		buffer = pitcher_get_idle_buffer(cvrt->chnno);
		if (!buffer)
			return -RET_E_NOT_READY;

		buffer->format = &cvrt->format;
		buffer->crop = &cvrt->crop;
		cvrt->ctx->src = pbuf;
		cvrt->ctx->dst = buffer;
		if (cvrt->ctx->convert_frame)
			cvrt->ctx->convert_frame(cvrt->ctx);
		buffer->planes[0].bytesused = buffer->planes[0].size;

		pitcher_push_back_output(cvrt->chnno, buffer);
		SAFE_RELEASE(buffer, pitcher_put_buffer);
	} else {
		pitcher_push_back_output(cvrt->chnno, pbuf);
	}

	return RET_OK;
}

static int init_convert_node(struct test_node *node)
{
	struct convert_test_t *cvrt;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	cvrt = container_of(node, struct convert_test_t, node);

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

	return RET_OK;
}

static void free_convert_node(struct test_node *node)
{
	struct convert_test_t *cvrt;

	if (!node)
		return;

	cvrt = container_of(node, struct convert_test_t, node);
	if (cvrt->ctx && cvrt->ctx->free)
		SAFE_RELEASE(cvrt->ctx, cvrt->ctx->free);
	SAFE_CLOSE(cvrt->chnno, pitcher_unregister_chn);
	SAFE_RELEASE(cvrt, pitcher_free);
}

static int get_convert_chnno(struct test_node *node)
{
	struct convert_test_t *cvrt;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	cvrt = container_of(node, struct convert_test_t, node);
	if (cvrt->chnno >= 0)
		return cvrt->chnno;

	src_node = nodes[cvrt->node.source];
	if (src_node->get_source_chnno(src_node) < 0)
		return cvrt->chnno;

	cvrt->chnno = pitcher_register_chn(cvrt->node.context, &cvrt->desc, cvrt);

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

	cvrt->node.pixelformat = PIX_FMT_NV12;
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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		cvrt->node.pixelformat = fmt;
	}

	return RET_OK;
}

static int set_g2d_cvt_source(struct test_node *node,
				struct test_node *src)
{
	struct g2d_cvt_test_t *g2dc;

	if (!node || !src)
		return -RET_E_INVAL;

	g2dc = container_of(node, struct g2d_cvt_test_t, node);
	g2dc->node.width = ALIGN(src->width, 2);
	g2dc->node.height = ALIGN(src->height, 2);
	g2dc->ifmt = src->pixelformat;

	memset(&g2dc->format, 0, sizeof(g2dc->format));
	g2dc->format.format = g2dc->node.pixelformat;
	g2dc->format.width = g2dc->node.width;
	g2dc->format.height = g2dc->node.height;
	if (src->type == TEST_TYPE_DECODER) {
		struct decoder_test_t *decoder;

		decoder = container_of(src, struct decoder_test_t, node);
		memcpy(&g2dc->crop, &decoder->capture.crop, sizeof(g2dc->crop));
	}
	pitcher_get_pix_fmt_info(&g2dc->format, 0);
	PITCHER_LOG("set g2d convert source %s %dx%d -> %s\n",
			pitcher_get_format_name(src->pixelformat),
			src->width, src->height,
			pitcher_get_format_name(g2dc->node.pixelformat));

	return RET_OK;
}

static int recycle_g2d_cvt_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct g2d_cvt_test_t *g2dc = arg;
	int is_end = false;

	if (!g2dc)
		return -RET_E_NULL_POINTER;

	if (pitcher_is_active(g2dc->chnno) && !g2dc->end)
		pitcher_put_buffer_idle(g2dc->chnno, buffer);
	else
		is_end = true;
	if (del)
		*del = is_end;

	return RET_OK;
}

static struct pitcher_buffer *alloc_g2d_cvt_buffer(void *arg)
{
	struct g2d_cvt_test_t *g2dc = arg;
	struct test_node *src_node;

	if (!g2dc)
		return NULL;

	src_node = nodes[g2dc->node.source];
	if (!src_node)
		return NULL;
	if (g2dc->ifmt != src_node->pixelformat ||
			g2dc->node.width != src_node->width ||
			g2dc->node.height != src_node->height)
		set_g2d_cvt_source(&g2dc->node, src_node);

	return pitcher_new_dma_buffer(&g2dc->format, recycle_g2d_cvt_buffer, g2dc);
}

static int g2d_cvt_start(void *arg)
{
	struct g2d_cvt_test_t *g2dc = arg;

	g2dc->end = false;
	return RET_OK;
}

static int g2d_cvt_checkready(void *arg, int *is_end)
{
	struct g2d_cvt_test_t *g2dc = arg;

	if (!g2dc)
		return false;

	if (is_force_exit())
		g2dc->end = true;
	if (is_source_end(g2dc->chnno))
		g2dc->end = true;
	if (is_end)
		*is_end = g2dc->end;
	if (g2dc->end)
		return false;
	if (!pitcher_chn_poll_input(g2dc->chnno))
		return false;
	if (g2dc->ifmt == g2dc->node.pixelformat)
		return true;
	if (pitcher_poll_idle_buffer(g2dc->chnno))
		return true;

	return false;
}

static int g2d_cvt_run(void *arg, struct pitcher_buffer *pbuf)
{
	struct g2d_cvt_test_t *g2dc = arg;
	uint32_t i;

	if (!g2dc || !pbuf)
		return -RET_E_INVAL;

	if (pbuf->planes[0].bytesused == 0) {
		g2dc->end = true;
		return 0;
	}

	if (g2dc->ifmt != g2dc->node.pixelformat) {
		struct pitcher_buffer *buffer;

		if (!g2dc->ctx)
			g2dc->ctx = pitcher_create_g2d_convert();
		if (!g2dc->ctx) {
			g2dc->end = true;
			return -RET_E_INVAL;
		}

		buffer = pitcher_get_idle_buffer(g2dc->chnno);
		if (!buffer)
			return -RET_E_NOT_READY;

		buffer->format = &g2dc->format;
		buffer->crop = &g2dc->crop;
		for (i = 0; i < buffer->format->num_planes; i++)
			buffer->planes[i].bytesused = buffer->format->planes[i].size;
		g2dc->ctx->src = pbuf;
		g2dc->ctx->dst = buffer;
		if (g2dc->ctx->convert_frame)
			g2dc->ctx->convert_frame(g2dc->ctx);

		g2dc->frame_count++;
		pitcher_push_back_output(g2dc->chnno, buffer);
		SAFE_RELEASE(buffer, pitcher_put_buffer);
	} else {
		pitcher_push_back_output(g2dc->chnno, pbuf);
	}

	if (pbuf->flags & PITCHER_BUFFER_FLAG_LAST)
		g2dc->end = true;

	return RET_OK;
}

static int init_g2d_cvt_node(struct test_node *node)
{
	struct g2d_cvt_test_t *g2dc;

	if (!node)
		return -RET_E_NULL_POINTER;

	g2dc = container_of(node, struct g2d_cvt_test_t, node);

	g2dc->desc.fd = -1;
	g2dc->desc.start = g2d_cvt_start;
	g2dc->desc.check_ready = g2d_cvt_checkready;
	g2dc->desc.runfunc = g2d_cvt_run;
	g2dc->desc.buffer_count = 4;
	g2dc->desc.alloc_buffer = alloc_g2d_cvt_buffer;
	snprintf(g2dc->desc.name, sizeof(g2dc->desc.name), "g2dc.%d",
			g2dc->node.key);
	g2dc->ctx = pitcher_create_g2d_convert();
	if (!g2dc->ctx)
		return -RET_E_INVAL;

	return RET_OK;
}

static void free_g2d_cvt_node(struct test_node *node)
{
	struct g2d_cvt_test_t *g2dc;

	if (!node)
		return;

	g2dc = container_of(node, struct g2d_cvt_test_t, node);
	PITCHER_LOG("g2d convert frame count : %ld\n", g2dc->frame_count);
	if (g2dc->ctx && g2dc->ctx->free)
		g2dc->ctx->free(g2dc->ctx);
	SAFE_CLOSE(g2dc->chnno, pitcher_unregister_chn);
	SAFE_RELEASE(g2dc, pitcher_free);
}

static int get_g2d_cvt_chnno(struct test_node *node)
{
	struct g2d_cvt_test_t *g2dc;
	struct test_node *src_node;

	if (!node)
		return -RET_E_NULL_POINTER;

	g2dc = container_of(node, struct g2d_cvt_test_t, node);
	if (g2dc->chnno >= 0)
		return g2dc->chnno;

	src_node = nodes[g2dc->node.source];
	if (!src_node || src_node->get_source_chnno(src_node) < 0)
		return g2dc->chnno;

	g2dc->chnno = pitcher_register_chn(g2dc->node.context, &g2dc->desc, g2dc);
	return g2dc->chnno;
}

static struct test_node *alloc_g2d_cvt_node(void)
{
	struct g2d_cvt_test_t *g2dc;

	g2dc = pitcher_calloc(1, sizeof(*g2dc));
	if (!g2dc)
		return NULL;

	g2dc->node.key = -1;
	g2dc->node.source = -1;
	g2dc->node.type = TEST_TYPE_CONVERT;
	g2dc->chnno = -1;

	g2dc->node.pixelformat = PIX_FMT_YUYV;
	g2dc->node.init_node = init_g2d_cvt_node;
	g2dc->node.free_node = free_g2d_cvt_node;
	g2dc->node.get_source_chnno = get_g2d_cvt_chnno;
	g2dc->node.get_sink_chnno = get_g2d_cvt_chnno;
	g2dc->node.set_source = set_g2d_cvt_source;

	return &g2dc->node;
}

static int parse_g2d_cvt_option(struct test_node *node,
				struct mxc_vpu_test_option *option,
				char *argv[])
{
	struct g2d_cvt_test_t *g2dc;

	if (!node || !option || !option->name)
		return -RET_E_INVAL;
	if (option->arg_num && !argv)
		return -RET_E_INVAL;

	g2dc = container_of(node, struct g2d_cvt_test_t, node);

	if (!strcasecmp(option->name, "key")) {
		g2dc->node.key = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "source")) {
		g2dc->node.source = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "fmt")) {
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		g2dc->node.pixelformat = fmt;
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
	unsigned int seek_flag = 0;

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

	if (parser->seek.enable && parser->frame_count == parser->seek.pos_seek)
		seek_flag = 1;

	if (!seek_flag) {
		if (parser->frame_num > 0 && parser->frame_count >= parser->frame_num)
			parser->end = true;

		if (parser->end)
			buffer->flags |= PITCHER_BUFFER_FLAG_LAST;
	} else {
		buffer->flags |= PITCHER_BUFFER_FLAG_SEEK;
		PITCHER_LOG("seek at %ld\n", parser->frame_count);
	}

	if (buffer->planes[0].bytesused > 0) {
		if (parser->frame_count > parser->skip)
			pitcher_push_back_output(parser->chnno, buffer);
	}

	SAFE_RELEASE(buffer, pitcher_put_buffer);

	if (seek_flag) {
		pitcher_parser_seek_to_begin(parser->p);
		parser->offset = 0;
		parser->frame_count = 0;
		parser->seek.enable = 0;
		parser->skip = parser->seek.pos_new;
	}

	return RET_OK;
}

static int parser_init_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int parser_uninit_plane(struct pitcher_buf_ref *plane,
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
	desc.plane_size[0] = get_image_size(parser->node.pixelformat,
					parser->node.width,
					parser->node.height,
					0);
	desc.init_plane = parser_init_plane;
	desc.uninit_plane = parser_uninit_plane;
	desc.recycle = parser_recycle_buffer;
	desc.arg = parser;

	return pitcher_new_buffer(&desc);
}

static int init_parser_memory(struct parser_test_t *parser)
{
	if (parser->node.pixelformat == PIX_FMT_RV30 || parser->node.pixelformat == PIX_FMT_RV40) {
		FILE *fp = NULL;
		size_t size = 0;

		parser->mem_type = MEM_ALLOC;
		parser->virt = (uint8_t *)malloc(parser->size + 0x1000);
		if (!parser->virt)
			return -RET_E_NO_MEMORY;

		fp = fopen(parser->filename, "rb");
		fseek(fp, 0, SEEK_SET);
		size = fread(parser->virt, 1, parser->size, fp);
		fclose(fp);
		if (size != parser->size)
			return -RET_E_INVAL;
	} else {
		parser->mem_type = MEM_MMAP;
		parser->virt = mmap(NULL, parser->size, PROT_READ, MAP_SHARED,
					parser->fd, 0);
		if (!parser->virt) {
			PITCHER_ERR("mmap input file %s fail\n", parser->filename);
			return -RET_E_MMAP;
		}
	}

	return RET_OK;
}

static void free_parser_memory(struct parser_test_t *parser)
{
	if (!parser->virt)
		return;

	if (parser->mem_type == MEM_MMAP)
		munmap(parser->virt, parser->size);
	else
		free(parser->virt);

	parser->virt = NULL;
	parser->size = 0;
}

static int init_parser_node(struct test_node *node)
{
	struct parser_test_t *parser;
	struct pitcher_parser *p;
	int ret;

	if (!node)
		return -RET_E_NULL_POINTER;

	parser = container_of(node, struct parser_test_t, node);
	if (!parser->filename)
		return -RET_E_INVAL;

	if (is_support_parser(parser->node.pixelformat) == false) {
		PITCHER_LOG("Format %s unsupported parser\n",
				pitcher_get_format_name(parser->node.pixelformat));

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

	ret = init_parser_memory(parser);
	if (ret != RET_OK) {
		SAFE_CLOSE(parser->fd, close);
		return ret;
	}

	parser->p = pitcher_new_parser();
	if (parser->p == NULL)
		return -RET_E_INVAL;
	p = parser->p;
	p->filename = parser->filename;
	p->format = parser->node.pixelformat;
	p->number = parser->frame_num;
	p->virt = parser->virt;
	p->size = parser->size;

	pitcher_init_parser(parser->p);

	if (pitcher_parse(parser->p) != RET_OK) {
		SAFE_RELEASE(parser->p, pitcher_del_parser);
		return -RET_E_INVAL;
	}
	pitcher_parser_seek_to_begin(parser->p);

	if (p->width != 0) {
		parser->node.width = p->width;
		parser->node.height = p->height;
	}

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
	free_parser_memory(parser);
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
	parser->node.pixelformat = PIX_FMT_H264;
	parser->node.get_source_chnno = get_parser_chnno;
	parser->node.init_node = init_parser_node;
	parser->node.free_node = free_parser_node;
	parser->mode = "rb";
	parser->chnno = -1;
	parser->fd = -1;
	parser->skip = 0;
	parser->seek.enable = 0;

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
		int fmt = pitcher_get_format_by_name(argv[0]);

		if (fmt == PIX_FMT_NONE)
			return -RET_E_NOT_SUPPORT;
		parser->node.pixelformat = fmt;
	} else if (!strcasecmp(option->name, "size")) {
		parser->node.width = strtol(argv[0], NULL, 0);
		parser->node.height = strtol(argv[1], NULL, 0);
	} else if (!strcasecmp(option->name, "framenum")) {
		parser->frame_num = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "loop")) {
		parser->loop = strtol(argv[0], NULL, 0);
		PITCHER_LOG("set loop: %d\n", parser->loop);
	} else if (!strcasecmp(option->name, "skip")) {
		parser->skip = strtol(argv[0], NULL, 0);
	} else if (!strcasecmp(option->name, "seek")) {
		parser->seek.pos_seek = strtol(argv[0], NULL, 0);
		parser->node.seek_thd = strtol(argv[1], NULL, 0);
		parser->seek.pos_new = strtol(argv[2], NULL, 0);
		parser->seek.enable = 1;
	} else if (!strcasecmp(option->name, "show")) {
		parser->show = true;
	}

	if (parser->seek.enable) {
		if (parser->seek.pos_seek <= parser->skip) {
			parser->seek.enable = 0;
			parser->skip = max(parser->seek.pos_new, parser->skip);
		} else {
			unsigned int cnt = parser->seek.pos_seek - parser->skip;

			if (parser->node.seek_thd > cnt)
				parser->node.seek_thd = cnt;
		}
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
		.subcmd = "g2dc",
		.type = TEST_TYPE_CONVERT,
#ifdef ENABLE_G2D
		.option = g2d_cvt_options,
		.parse_option = parse_g2d_cvt_option,
		.alloc_node = alloc_g2d_cvt_node,
#endif
	},
	{
		.subcmd = "parser",
		.option = parser_options,
		.type = TEST_TYPE_PARSER,
		.parse_option = parse_parser_option,
		.alloc_node = alloc_parser_node,
	},
#ifdef ENABLE_MM_PARSE
	{
		.subcmd = "media",
		.option = mm_extractor_options,
		.type = TEST_TYPE_MM_EXTRACTOR,
		.parse_option = parse_mm_extractor_option,
		.alloc_node = alloc_mm_extractor_node,
	},
#endif
	{
		.subcmd = "dma",
		.type = TEST_TYPE_CONVERT,
		.option = dmanode_options,
		.parse_option = parse_dmanode_option,
		.alloc_node = alloc_dmanode,
	},
	{
		.subcmd = "waylandsink",
		.type = TEST_TYPE_SINK,
#ifdef ENABLE_WAYLAND
		.option = waylandsink_options,
		.parse_option = parse_wayland_sink_option,
		.alloc_node = alloc_wayland_sink_node,
#endif
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
	if (schn < 0)
		return RET_OK;

	PITCHER_LOG("connect <%d, %d>\n", src->key, dst->key);
	if (dst->set_source) {
		ret = dst->set_source(dst, src);
		if (ret < 0)
			return ret;
	}

	if (dst->get_sink_chnno(dst) < 0) {
		if (dst->init_node) {
			ret = dst->init_node(dst);
			if (ret < 0)
				return ret;
		}
	}
	dchn = dst->get_sink_chnno(dst);
	if (dchn < 0)
		return -RET_E_INVAL;
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
		if (pitcher_is_error(schn) || pitcher_is_error(dchn)) {
			PITCHER_ERR("some error occurs\n");
			end = true;
			force_exit();
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

struct test_node *get_test_node(uint32_t key)
{
	if (key >= ARRAY_SIZE(nodes))
		return NULL;

	return nodes[key];
}

int check_source(int source)
{
	if (source < 0 || source >= MAX_NODE_COUNT)
		return -RET_E_INVAL;
	if (!nodes[source])
		return -RET_E_INVAL;
	if (nodes[source]->pixelformat >= PIX_FMT_NB)
		return -RET_E_INVAL;
	if (nodes[source]->pixelformat >= PIX_FMT_COMPRESSED)
		return 0;
	if (!nodes[source]->width || !nodes[source]->height)
		return -RET_E_INVAL;

	return 0;
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
	signal(SIGSEGV, sig_handler);

	printf("mxc_v4l2_vpu_test.out V%d.%d, SHA: %s %s\n",
		VERSION_MAJOR, VERSION_MINOR,
		GIT_SHA, GIT_COMMIT_DATE);

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
		if (source >= 0 && source < MAX_NODE_COUNT && nodes[source])
			continue;

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
