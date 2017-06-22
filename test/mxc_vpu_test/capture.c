
/*
 * Copyright 2004-2013 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/* 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
   in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from 
   this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE 
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/videodev2.h>
#include "vpu_test.h"

#define TEST_BUFFER_NUM 3
#define MAX_CAPTURE_MODES   10

static int cap_fd = -1;
struct capture_testbuffer cap_buffers[TEST_BUFFER_NUM];
static int sizes_buf[MAX_CAPTURE_MODES][2];

static int getCaptureMode(int width, int height)
{
	int i, mode = -1;

	for (i = 0; i < MAX_CAPTURE_MODES; i++) {
		if (width == sizes_buf[i][0] &&
		    height == sizes_buf[i][1]) {
			mode = i;
			break;
		}
	}

	return mode;
}

int
v4l_start_capturing(void)
{
	unsigned int i;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(cap_fd, VIDIOC_QUERYBUF, &buf) < 0) {
			err_msg("VIDIOC_QUERYBUF error\n");
			return -1;
		}

		cap_buffers[i].length = buf.length;
		cap_buffers[i].offset = (size_t) buf.m.offset;
	}

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		buf.m.offset = cap_buffers[i].offset;
		if (ioctl(cap_fd, VIDIOC_QBUF, &buf) < 0) {
			err_msg("VIDIOC_QBUF error\n");
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(cap_fd, VIDIOC_STREAMON, &type) < 0) {
		err_msg("VIDIOC_STREAMON error\n");
		return -1;
	}

	return 0;
}

void
v4l_stop_capturing(void)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(cap_fd, VIDIOC_STREAMOFF, &type);
	close(cap_fd);
	cap_fd = -1;
}

int
v4l_capture_setup(struct encode *enc, int width, int height, int fps)
{
	char v4l_device[80], node[8];
	struct v4l2_format fmt = {0};
	struct v4l2_streamparm parm = {0};
	struct v4l2_requestbuffers req = {0};
	struct v4l2_control ctl;
	struct v4l2_crop crop;
	struct v4l2_dbg_chip_ident chip;
	struct v4l2_frmsizeenum fsize;
	int i, g_input = 1, mode = 0; /* Use different g_input (0 or 1) for each camera */

	if (cap_fd > 0) {
		warn_msg("capture device already opened\n");
		return -1;
	}

	sprintf(node, "%d", enc->cmdl->video_node_capture);
	strcpy(v4l_device, "/dev/video");
	strcat(v4l_device, node);

	if ((cap_fd = open(v4l_device, O_RDWR, 0)) < 0) {
		err_msg("Unable to open %s\n", v4l_device);
		return -1;
	}

	if (ioctl(cap_fd, VIDIOC_DBG_G_CHIP_IDENT, &chip)) {
		printf("VIDIOC_DBG_G_CHIP_IDENT failed.\n");
		return -1;
	}
	info_msg("sensor chip is %s\n", chip.match.name);

	memset(sizes_buf, 0, sizeof(sizes_buf));
	for (i = 0; i < MAX_CAPTURE_MODES; i++) {
		fsize.index = i;
		if (ioctl(cap_fd, VIDIOC_ENUM_FRAMESIZES, &fsize))
			break;
		else {
			sizes_buf[i][0] = fsize.discrete.width;
			sizes_buf[i][1] = fsize.discrete.height;
		}
	}

	ctl.id = V4L2_CID_PRIVATE_BASE;
	ctl.value = 0;
	if (ioctl(cap_fd, VIDIOC_S_CTRL, &ctl) < 0)
	{
		printf("set control failed\n");
		return -1;
	}

	mode = getCaptureMode(width, height);
	if (mode == -1) {
		printf("Not support the resolution in camera\n");
		return -1;
	}
	info_msg("sensor frame size is %dx%d\n", sizes_buf[mode][0],
					       sizes_buf[mode][1]);

	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = fps;
	parm.parm.capture.capturemode = mode;
	if (ioctl(cap_fd, VIDIOC_S_PARM, &parm) < 0) {
		err_msg("set frame rate failed\n");
		close(cap_fd);
		cap_fd = -1;
		return -1;
	}

	if (ioctl(cap_fd, VIDIOC_S_INPUT, &g_input) < 0) {
		printf("VIDIOC_S_INPUT failed\n");
		close(cap_fd);
		return -1;
	}

	crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	crop.c.width = width;
	crop.c.height = height;
	crop.c.top = 0;
	crop.c.left = 0;
	if (ioctl(cap_fd, VIDIOC_S_CROP, &crop) < 0) {
		printf("VIDIOC_S_CROP failed\n");
		close(cap_fd);
		return -1;
	}

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.bytesperline = width;
	fmt.fmt.pix.priv = 0;
	fmt.fmt.pix.sizeimage = 0;
	if (enc->cmdl->chromaInterleave == 0)
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	else
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	if (ioctl(cap_fd, VIDIOC_S_FMT, &fmt) < 0) {
		err_msg("set format failed\n");
		close(cap_fd);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(cap_fd, VIDIOC_REQBUFS, &req) < 0) {
		err_msg("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
		close(cap_fd);
		cap_fd = -1;
		return -1;
	}

	return 0;
}

int
v4l_get_capture_data(struct v4l2_buffer *buf)
{
	memset(buf, 0, sizeof(struct v4l2_buffer));
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	if (ioctl(cap_fd, VIDIOC_DQBUF, buf) < 0) {
		err_msg("VIDIOC_DQBUF failed\n");
		return -1;
	}

	return 0;
}

void
v4l_put_capture_data(struct v4l2_buffer *buf)
{
	ioctl(cap_fd, VIDIOC_QBUF, buf);
}

