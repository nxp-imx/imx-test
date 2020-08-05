/*
 * Copyright 2018 NXP
 *
 * include/pitcher_v4l2.h
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

#define MAX_BUFFER_COUNT	16

struct v4l2_component_t {
	int chnno;
	struct pitcher_unit_desc desc;
	int fd;
	enum v4l2_buf_type type;
	enum v4l2_memory memory;
	uint32_t pixelformat;
	uint32_t width;
	uint32_t height;
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
};

extern struct pitcher_unit_desc pitcher_v4l2_capture;
extern struct pitcher_unit_desc pitcher_v4l2_output;

int lookup_v4l2_device_and_open(unsigned int out_fmt, unsigned int cap_fmt);
int check_v4l2_device_type(int fd, unsigned int out_fmt, unsigned int cap_fmt);
int is_v4l2_mplane(struct v4l2_capability *cap);
int is_v4l2_splane(struct v4l2_capability *cap);
int set_ctrl(int fd, int id, int value);

#ifdef __cplusplus
}
#endif
#endif
