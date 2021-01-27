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
 * pitcher_v4l2.h
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#ifndef _INCLUDE_PITCHER_V4L2_H
#define _INCLUDE_PITCHER_V4L2_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <linux/videodev2.h>

#ifndef V4L2_EVENT_CODEC_ERROR
#define V4L2_EVENT_CODEC_ERROR	(V4L2_EVENT_PRIVATE_START + 1)
#endif
#ifndef V4L2_EVENT_SKIP
#define V4L2_EVENT_SKIP		(V4L2_EVENT_PRIVATE_START + 2)
#endif

#define MAX_BUFFER_COUNT	32

struct v4l2_component_t {
	int chnno;
	struct pitcher_unit_desc desc;
	int fd;
	enum v4l2_buf_type type;
	enum v4l2_memory memory;
	uint32_t pixelformat;
	uint32_t width;
	uint32_t height;
	struct v4l2_rect crop;
	uint32_t framerate;
	uint32_t bytesperline;
	uint32_t sizeimage;
	uint32_t num_planes;
	struct pitcher_buffer *buffers[MAX_BUFFER_COUNT];
	struct pitcher_buffer *slots[MAX_BUFFER_COUNT];
	struct pitcher_buffer *errors[MAX_BUFFER_COUNT];
	unsigned int buffer_count;
	unsigned int buffer_index;
	int enable;
	unsigned long frame_count;
	int end;
	int (*start)(struct v4l2_component_t *component);
	int (*stop)(struct v4l2_component_t *component);
	int (*is_end)(struct v4l2_component_t *component);
	void *priv;
	uint64_t ts_b;
	uint64_t ts_e;
	int eos_received;
	int resolution_change;
};

extern struct pitcher_unit_desc pitcher_v4l2_capture;
extern struct pitcher_unit_desc pitcher_v4l2_output;

int lookup_v4l2_device_and_open(unsigned int out_fmt, unsigned int cap_fmt);
int check_v4l2_device_type(int fd, unsigned int out_fmt, unsigned int cap_fmt);
int is_v4l2_mplane(struct v4l2_capability *cap);
int is_v4l2_splane(struct v4l2_capability *cap);
int check_v4l2_support_fmt(int fd, uint32_t type, uint32_t pixelformat);
int set_ctrl(int fd, int id, int value);
int get_ctrl(int fd, int id, int *value);
uint32_t get_image_size(uint32_t fmt, uint32_t width, uint32_t height);
#ifdef __cplusplus
}
#endif
#endif
