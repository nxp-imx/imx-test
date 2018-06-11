/*
 * Copyright 2009-2015 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Copyright 2018 NXP
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
 * @file mx6s_v4l2_capture.c
 *
 * based on mx8_v4l2_cap_drm.c
 *
 * @brief MX6SL/MX6SX/MX8MQ Video For Linux 2 driver test application
 *
 */

#ifdef __cplusplus
extern "C"{
#endif

#include <asm/types.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <errno.h>
#include <string.h>

#ifdef	GET_CONTI_PHY_MEM_VIA_PXP_LIB
#include "pxp_lib.h"
#endif

#include "../../include/soc_check.h"
#include "../../include/test_utils.h"

sigset_t sigset;
int quitflag;

#define  RGB(v) ({    \
			int value = (v); \
			(value > 0) * value | (255 * (value > 255));\
		})

#define DBG_LEVEL	6
#define INFO_LEVEL	5
#define ERR_LEVEL	4

static int log_level = DBG_LEVEL;

#define v4l2_printf(LEVEL, fmt, args...)	\
do {						\
	if (LEVEL <= log_level)			\
		printf(fmt, ##args);		\
} while(0)

#define v4l2_info(fmt, args...) v4l2_printf(INFO_LEVEL, fmt, ##args)
#define v4l2_dbg(fmt, args...) v4l2_printf(DBG_LEVEL, fmt, ##args)
#define v4l2_err(fmt, args...) v4l2_printf(ERR_LEVEL, fmt, ##args)

#define TEST_BUFFER_NUM 3
#define MAX_V4L2_DEVICE_NR     64
#define MAX_SIZE	3

#define MAX_NUM_CARD	8

struct testbuffer
{
	unsigned char *start;
	size_t offset;
	unsigned int length;
};

struct drm_kms {
	void *fb_base;
	__u32 bytes_per_pixel;
	__u32 bpp;

	__u32 connector_id;
	__u32 stride;
	__u32 size;
	__u32 handle;
	__u32 buf_id;

	__s32 width, height;
	__s32 drm_fd;
	__s32 crtc_id;

	drmModeModeInfo *mode;
	drmModeConnector *connector;
	drmModeEncoder *encoder;
};

struct drm_kms kms;

struct testbuffer buffers[TEST_BUFFER_NUM];
#ifdef	GET_CONTI_PHY_MEM_VIA_PXP_LIB
struct pxp_mem_desc mem[TEST_BUFFER_NUM];
#endif
int g_out_width = 640;
int g_out_height = 480;
int g_cap_fmt = V4L2_PIX_FMT_YUYV;
int g_capture_mode = 0;
int g_timeout = 10;
int g_camera_framerate = 30;	/* 30 fps */
int g_loop = 0;
int g_mem_type = V4L2_MEMORY_MMAP;
int g_frame_size;
char g_v4l_device[100] = "/dev/video0";
char g_saved_filename[100] = "1.yuv";
int  g_saved_to_file = 0;

void dump_drm_clients(const int dev_num)
{
	char cmd[50];

	sprintf(cmd, "cat /sys/kernel/debug/dri/%d/clients", dev_num);

	printf("========================================================\n");
	system(cmd);
	printf("========================================================\n");
	printf("Please ensure there is no other master client\n");
	printf("========================================================\n");
}

static void drm_cleanup(struct drm_kms *kms)
{
	struct drm_mode_destroy_dumb dreq;

	munmap(kms->fb_base, kms->size);

	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = kms->handle;
	drmIoctl(kms->buf_id, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

	drmModeRmFB(kms->drm_fd, kms->buf_id);
	drmModeFreeEncoder(kms->encoder);
	drmModeFreeConnector(kms->connector);
}

int drm_setup(struct drm_kms *kms)
{
	int i = 0, ret;
	char dev_name[15];
	uint64_t  has_dumb;

loop:
	sprintf(dev_name, "/dev/dri/card%d", i++);

	kms->drm_fd = open(dev_name, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (kms->drm_fd < 0) {
		v4l2_err("Open %s fail\n", dev_name);
		return -1;
	}

	if (drmGetCap(kms->drm_fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		v4l2_err("drm device '%s' does not support dumb buffers\n", dev_name);
		close(kms->drm_fd);
		goto loop;
	}

	ret = drmSetMaster(kms->drm_fd);
	if (ret) {
		dump_drm_clients(i - 1);
		goto err0;
	}

	drmModeRes *res;
	drmModeConnector *conn;
	drmModeEncoder *encoder;

	res = drmModeGetResources(kms->drm_fd);
	if (!res) {
		if (i > MAX_NUM_CARD) {
			v4l2_err("Cannot retrieve DRM resources (%d)\n", errno);
			goto err1;
		}
		drmDropMaster(kms->drm_fd);
		close(kms->drm_fd);
		goto loop;
	}
	v4l2_info("Open '%s' success\n", dev_name);

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; i++) {
		/* get information for each connector */
		conn = drmModeGetConnector(kms->drm_fd, res->connectors[i]);
		if (!conn) {
			v4l2_err("Cannot retrieve DRM connector %u:%u (%d)\n",
				i, res->connectors[i], errno);
			continue;
		}

		if (conn->connection == DRM_MODE_CONNECTED &&
					conn->count_modes > 0)
			break;

		drmModeFreeConnector(conn);
	}

	if ((i == res->count_connectors) && (conn == NULL)) {
		v4l2_info("No currently active connector found.\n");
		ret = -errno;
		goto err2;
	}

	/* Get Screen resoultion info */
	kms->width = conn->modes[0].hdisplay;
	kms->height = conn->modes[0].vdisplay;

	v4l2_info("Screen resolution is %d*%d\n", kms->width, kms->height);

	int crtc_id;
	for (i = 0; i < conn->count_encoders; i++) {
		encoder = drmModeGetEncoder(kms->drm_fd, conn->encoders[i]);
		if (encoder == NULL)
			continue;

		int j;
		for (j = 0; j < res->count_crtcs; j++) {
			if (encoder->possible_crtcs & (1 << j)) {
				crtc_id = res->crtcs[j];
				drmModeFreeEncoder(encoder);
				break;
			}
			crtc_id = -1;
		}

		if (j == res->count_crtcs && crtc_id == -1) {
			v4l2_err("cannot find crtc\n");
			drmModeFreeEncoder(encoder);
		}
	}
	kms->crtc_id = crtc_id;

	if (i == conn->count_encoders && encoder == NULL) {
		v4l2_err("Cannot find encoder\n");
		ret = -errno;
		goto err3;
	}

	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;

	memset(&creq, 0, sizeof(creq));
	creq.width = kms->width;
	creq.height = kms->height;
	creq.bpp = 32;

	ret = drmIoctl(kms->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		printf("cannot create dumb buffer (%d)\n", errno);
		return -errno;
	}

	kms->stride = creq.pitch;
	kms->size = creq.size;
	kms->handle = creq.handle;

	ret = drmModeAddFB(kms->drm_fd, kms->width, kms->height, 24, 32,
				kms->stride, kms->handle, &kms->buf_id); /* buf_id */
	if (ret) {
		v4l2_err("Add framebuffer (%d) fail\n", kms->buf_id);
		goto err4;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	ret = drmIoctl(kms->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		v4l2_err("Map dump ioctl fail\n");
		goto err5;
	}

	kms->fb_base = mmap(0, kms->size, PROT_READ | PROT_WRITE, MAP_SHARED,
				kms->drm_fd, mreq.offset);
	if (kms->fb_base == MAP_FAILED) {
		v4l2_err("Cannot mmap dumb buffer (%d)\n", errno);
		goto err6;
	}
	memset(kms->fb_base, 0, kms->size);

	ret = drmModeSetCrtc(kms->drm_fd, kms->crtc_id, kms->buf_id, 0, 0,
				&conn->connector_id, 1, conn->modes);

	/* free resources again */
	drmModeFreeResources(res);
	drmDropMaster(kms->drm_fd);

	kms->bpp = creq.bpp;
	kms->bytes_per_pixel = kms->bpp >> 3;
	kms->bpp = kms->bpp;
	kms->connector = conn;
	kms->encoder = encoder;

	v4l2_info("======== KMS INFO ========\n"
		   "fb_base=%p\n"
		   "w/h=(%d,%d)\n"
		   "bytes_per_pixel=%d\n"
		   "bpp=%d\n"
		   "screen_size=%d\n"
		   "======== KMS END =========\n", kms->fb_base, kms->width,
		   kms->height, kms->bytes_per_pixel, kms->bpp, kms->size);

	return 0;

err6:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = creq.handle;
	drmIoctl(kms->buf_id, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
err5:
	drmModeRmFB(kms->drm_fd, kms->buf_id);
err4:
	drmModeFreeEncoder(encoder);
err3:
	drmModeFreeConnector(conn);
err2:
	drmModeFreeResources(res);
err1:
	drmDropMaster(kms->drm_fd);
err0:
	close(kms->drm_fd);
	return ret;
}


int start_capturing(int fd_v4l)
{
	unsigned int i;
	struct v4l2_buffer buf;
	enum v4l2_buf_type type;
	struct v4l2_requestbuffers req;

	memset(&req, 0, sizeof (req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = g_mem_type;

	if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
		v4l2_err("VIDIOC_REQBUFS failed\n");
		return -1;
	}

	if (g_mem_type == V4L2_MEMORY_MMAP) {
		for (i = 0; i < TEST_BUFFER_NUM; i++) {
			memset(&buf, 0, sizeof (buf));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = g_mem_type;
			buf.index = i;

			if (ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
				v4l2_err("VIDIOC_QUERYBUF error\n");
				return -1;
			}

			buffers[i].length = buf.length;
			buffers[i].offset = (size_t) buf.m.offset;
			buffers[i].start = mmap(NULL, buffers[i].length,
				PROT_READ | PROT_WRITE, MAP_SHARED,
				fd_v4l, buffers[i].offset);
			memset(buffers[i].start, 0xFF, buffers[i].length);
		}
	}

	for (i = 0; i < TEST_BUFFER_NUM; i++)
	{
		memset(&buf, 0, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = g_mem_type;
		buf.index = i;
		buf.length = buffers[i].length;
		if (g_mem_type == V4L2_MEMORY_USERPTR)
			buf.m.userptr = (unsigned long) buffers[i].start;
		else
			buf.m.offset = buffers[i].offset;

		if (ioctl (fd_v4l, VIDIOC_QBUF, &buf) < 0) {
			v4l2_err("VIDIOC_QBUF error\n");
			return -1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl (fd_v4l, VIDIOC_STREAMON, &type) < 0) {
		v4l2_err("VIDIOC_STREAMON error\n");
		return -1;
	}

	return 0;
}

int stop_capturing(int fd_v4l)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	return ioctl (fd_v4l, VIDIOC_STREAMOFF, &type);
}

static int open_video_device(void)
{
	struct v4l2_capability cap;
	int fd_v4l;

	if ((fd_v4l = open(g_v4l_device, O_RDWR, 0)) < 0) {
		v4l2_err("unable to open %s for capture device.\n", g_v4l_device);
	}
	if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
		if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
			v4l2_info("Found v4l2 capture device %s.\n", g_v4l_device);
			return fd_v4l;
		}
	} else
		close(fd_v4l);

	return fd_v4l;
}

static void print_pixelformat(char *prefix, int val)
{
	v4l2_info("%s: %c%c%c%c\n", prefix ? prefix : "pixelformat",
					val & 0xff,
					(val >> 8) & 0xff,
					(val >> 16) & 0xff,
					(val >> 24) & 0xff);
}

void vl42_device_cap_list(void)
{
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmivalenum frmival;
	struct v4l2_frmsizeenum frmsize;
	int fd_v4l = 0;
	char v4l_name[20];
	int i;

	for (i = 0; i < 5; i++) {
		snprintf(v4l_name, sizeof(v4l_name), "/dev/video%d", i);

		if ((fd_v4l = open(v4l_name, O_RDWR, 0)) < 0) {
			v4l2_err("\nunable to open %s for capture device.\n", v4l_name);
		} else
			v4l2_info("\nopen video device %s \n", v4l_name);

		if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
			if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
				v4l2_info("Found v4l2 capture device %s\n", v4l_name);
				fmtdesc.index = 0;
				fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				while (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
					print_pixelformat("pixelformat (output by camera)",
							fmtdesc.pixelformat);
					frmsize.pixel_format = fmtdesc.pixelformat;
					frmsize.index = 0;
					while (ioctl(fd_v4l, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
				        frmival.index = 0;
						frmival.pixel_format = fmtdesc.pixelformat;
						frmival.width = frmsize.discrete.width;
						frmival.height = frmsize.discrete.height;
						while (ioctl(fd_v4l, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0) {
							v4l2_info("CaptureMode=%d, Width=%d, Height=%d %.3f fps\n",
									frmsize.index, frmival.width, frmival.height,
									1.0 * frmival.discrete.denominator / frmival.discrete.numerator);
							frmival.index++;
						}
						frmsize.index++;
					}
					fmtdesc.index++;
				}
			} else
				v4l2_err("Video device %s not support v4l2 capture\n", v4l_name);
		}
		close(fd_v4l);
	}
}

int v4l_capture_setup(void)
{
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmsizeenum frmsize;
	int fd_v4l = 0;

	if ((fd_v4l = open_video_device()) < 0)
	{
		v4l2_err("Unable to open v4l2 capture device.\n");
		return -1;
	}

	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	parm.parm.capture.capturemode = g_capture_mode;
	parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	parm.parm.capture.timeperframe.numerator = 1;
	if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0)
	{
		v4l2_err("VIDIOC_S_PARM failed\n");
		goto fail;
	}

	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
		v4l2_err("VIDIOC ENUM FMT failed \n");
		goto fail;
	}
	print_pixelformat("pixelformat (output by camera)",
			fmtdesc.pixelformat);
	g_cap_fmt = fmtdesc.pixelformat;

	frmsize.pixel_format = fmtdesc.pixelformat;
	frmsize.index = g_capture_mode;
	if (ioctl(fd_v4l, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) {
		v4l2_err("get capture mode %d framesize failed\n", g_capture_mode);
		goto fail;
	}

	g_out_width = frmsize.discrete.width;
	g_out_height = frmsize.discrete.height;

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = g_cap_fmt;
	fmt.fmt.pix.width = g_out_width;
	fmt.fmt.pix.height = g_out_height;
	if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0)
	{
		v4l2_err("set format failed\n");
		goto fail;
	}

	if (ioctl(fd_v4l, VIDIOC_G_FMT, &fmt) < 0)
	{
		v4l2_err("get format failed\n");
		goto fail;
	}

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd_v4l, VIDIOC_G_PARM, &parm) < 0)
	{
		v4l2_err("VIDIOC_G_PARM failed\n");
		parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	}

	v4l2_info("\t WxH@fps = %dx%d@%d", fmt.fmt.pix.width,
			fmt.fmt.pix.height, parm.parm.capture.timeperframe.denominator);
	v4l2_info("\t Image size = %d\n", fmt.fmt.pix.sizeimage);

	g_frame_size = fmt.fmt.pix.sizeimage;

	return fd_v4l;

fail:
	close(fd_v4l);
	return -1;
}

void yuyvtorgb565(unsigned char *yuyv, unsigned char *dst )
{
	int r0, g0, b0;
	int r1, g1, b1;
	int y0, y1, u, v;
	char *src;

	src = (char *)yuyv;
	y0 = *(src+0);
	u = *(src+1);
	y1 = *(src+2);
	v = *(src+3);

	u = u - 128;
	v = v - 128;
	r0 = RGB(y0 + v + (v >> 2) + (v >> 3) + (v >> 5));
	g0 = RGB(y0 - ((u >> 2) + (u >> 4) + (u >> 5)) - (v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
	b0 = RGB(y0 + u + (u >> 1) + (u >> 2) + (u >> 6));

	r1 = RGB(y1 + v + (v >> 2) + (v >> 3) + (v >> 5));
	g1 = RGB(y1 - ((u >> 2) + (u >> 4) + (u >> 5)) - (v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
	b1 = RGB(y1 + u + (u >> 1) + (u >> 2) + (u >> 6));

	*(dst+1) = (r0 & 0xf8) | (g0 >> 5);
	*(dst) = ((g0 & 0x1c) << 3) | (b0 >> 3);

	*(dst+3) = (r1 & 0xf8) | (g1 >> 5);
	*(dst+2) = ((g1 & 0x1c) << 3) | (b1 >> 3);
}

void yuyvtorgb32(unsigned char *yuyv, unsigned char *dst)
{
	int r0, g0, b0;
	int r1, g1, b1;
	int y0, y1, u, v;
	char *src;

	src = (char *)yuyv;
	y0 = *(src+0);
	u = *(src+1);
	y1 = *(src+2);
	v = *(src+3);

	u = u - 128;
	v = v - 128;
	r0 = RGB(y0 + v + (v >> 2) + (v >> 3) + (v >> 5));
	g0 = RGB(y0 - ((u >> 2) + (u >> 4) + (u >> 5)) - (v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
	b0 = RGB(y0 + u + (u >> 1) + (u >> 2) + (u >> 6));

	r1 = RGB(y1 + v + (v >> 2) + (v >> 3) + (v >> 5));
	g1 = RGB(y1 - ((u >> 2) + (u >> 4) + (u >> 5)) - (v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
	b1 = RGB(y1 + u + (u >> 1) + (u >> 2) + (u >> 6));

	*(dst) = b0;
	*(dst+1) = g0;
	*(dst+2) = r0;

	*(dst+4) = b1;
	*(dst+5) = g1;
	*(dst+6) = r1;
}

void yuv32torgb565(unsigned char *yuv, unsigned char *dst )
{
	int r, g, b;
	int y, u, v;
	char *src;

	src = (char *)yuv;
	y = *(src+2);
	u = *(src+1);
	v = *(src+0);

	u = u - 128;
	v = v - 128;
	r = RGB(y + v + (v >> 2) + (v >> 3) + (v >> 5));
	g = RGB(y - ((u >> 2) + (u >> 4) + (u >> 5)) - (v >> 1) + (v >> 3) + (v >> 4) + (v >> 5));
	b = RGB(y + u + (u >> 1) + (u >> 2) + (u >> 6));

	*(dst+1) = (r & 0xf8) | (g >> 5);
	*(dst) = ((g & 0x1c) << 3) | (b >> 3);
}

void software_csc(unsigned char *inbuf, unsigned char *outbuf, int xres, int yres)
{
	unsigned char *yuv;
	unsigned char *rgb;
	int x;

	if (g_cap_fmt == V4L2_PIX_FMT_YUV32) {
		for (x = 0; x < xres*yres; x++) {
			yuv = inbuf + x*4;
			rgb = outbuf + x*2;
			yuv32torgb565(yuv, rgb);
		}
	} else if (g_cap_fmt == V4L2_PIX_FMT_YUYV) {
		for (x = 0; x < xres*yres/2; x++) {
			yuv = inbuf + x*4;
			if (kms.bytes_per_pixel == 2) {
				rgb = outbuf + x*4;
				yuyvtorgb565(yuv, rgb);
			} else if (kms.bytes_per_pixel == 4) {
				rgb = outbuf + x*8;
				yuyvtorgb32(yuv, rgb);
			}
		}
	} else
		v4l2_err("Unsupport format in %s\n", __func__);
}

int v4l_capture_test(int fd_v4l)
{
	struct v4l2_buffer buf;
	int frame_num = 0;
	struct timeval tv1, tv2;
	int j = 0;
	int out_w = 0, out_h = 0;
	int bufoffset;
	FILE * fd_y_file = 0;
	size_t wsize;
	unsigned char *cscbuf = NULL;
	int x_res, y_res;
	int bytes_per_line;
	int stride;
	int ret = -1;

	if (g_saved_to_file) {
		if ((fd_y_file = fopen(g_saved_filename, "wb")) == NULL) {
			v4l2_err("Unable to create y frame recording file\n");
			return -1;
		}
		goto loop; /* skip the fb display */
	}

	out_w = g_out_width;
	out_h = g_out_height;

	if (out_w >= 1920 || out_h >= 1080) {
		v4l2_err("it's using software csc for demo purpse, "
			"not support large resution wxh: %dx%d\n",
			out_w, out_h);
		goto FAIL;
	}

	ret = drm_setup(&kms);
	if (ret < 0) {
		v4l2_err("drm setup failed\n");
		return -1;
	}

	bytes_per_line = kms.bytes_per_pixel * kms.width;
	stride = out_w * kms.bpp >> 3;

	x_res = kms.width;
	y_res = kms.height;

	if (out_w > x_res || out_h > y_res) {
		v4l2_err("The output width or height is exceeding the resolution"
			" of the screen.\n"
			"wxh: %dx%d, screen wxh: %dx%d\n", out_w, out_h,
			x_res, y_res);
		goto FAIL;
	}

	/* allocate buffer for csc */
	cscbuf = malloc(out_w * out_h * kms.bytes_per_pixel);
	if (cscbuf == NULL) {
		v4l2_err("Unable to allocate cssbuf bytes\n");
		goto FAIL;
	}

loop:
	if (start_capturing(fd_v4l) < 0) {
		v4l2_err("start_capturing failed\n");
		goto FAIL;
	}

	gettimeofday(&tv1, NULL);
	do {
		memset(&buf, 0, sizeof (buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = g_mem_type;
		if (ioctl (fd_v4l, VIDIOC_DQBUF, &buf) < 0) {
			v4l2_err("VIDIOC_DQBUF failed.\n");
			break;
		}

		if (g_saved_to_file) {
			/* Save capture frame to file */
			wsize = fwrite(buffers[buf.index].start, g_frame_size, 1, fd_y_file);
			if (wsize < 1) {
				v4l2_err("No space left on device. Stopping after %d frames.\n", frame_num);
				break;
			}
		} else {
			/* Show capture frame to display */
			software_csc(buffers[buf.index].start, cscbuf, out_w, out_h);
			for (j = 0; j < out_h; j++)
				memcpy(kms.fb_base + j * bytes_per_line,
				   cscbuf + j * stride,
				   stride);
		}

		if (ioctl (fd_v4l, VIDIOC_QBUF, &buf) < 0) {
			v4l2_err("VIDIOC_QBUF failed\n");
			break;
		}

		frame_num += 1;
		gettimeofday(&tv2, NULL);
	} while((tv2.tv_sec - tv1.tv_sec < g_timeout) && !quitflag);

	if (stop_capturing(fd_v4l) < 0)
		v4l2_err("stop_capturing failed\n");

	if (g_loop > 0 && !quitflag) {
		v4l2_info("loop %d done!\n", g_loop);
		g_loop--;
		goto loop;
	}

	ret = 0;
FAIL:
	free(cscbuf);

	if (kms.drm_fd > 0) {
		munmap(kms.fb_base, kms.size);
		close(kms.drm_fd);
	}

	if (fd_y_file)
		fclose(fd_y_file);

	return ret;
}

void print_help(void)
{
	printf("MIPI/CSI Video4Linux capture Device Test\n"
		"Syntax: ./mx6s_v4l2_cap_drm.out\n"
		" -ow <capture output width>\n"
		" -oh <capture output height>\n"
		" -m <capture mode, 0-640x480, 1-320x240, etc>\n" \
		" -t <time> -fr <framerate>\n"
		" -d <camera select, /dev/video0, /dev/video1> \n" \
		" -of <save_to_file>\n"
		" -l <device support list>\n"
#ifdef	GET_CONTI_PHY_MEM_VIA_PXP_LIB
		" [-u if defined, means use userp, otherwise mmap]\n"
#endif
	);
}

int process_cmdline(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-m") == 0) {
			g_capture_mode = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-ow") == 0) {
			g_out_width = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-oh") == 0) {
			g_out_height = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-d") == 0) {
			strcpy(g_v4l_device, argv[++i]);
		} else if (strcmp(argv[i], "-t") == 0) {
			g_timeout = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-loop") == 0) {
			g_loop = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-help") == 0) {
			print_help();
			return -1;
		} else if (strcmp(argv[i], "-fr") == 0) {
			g_camera_framerate = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-of") == 0) {
			strcpy(g_saved_filename, argv[++i]);
			g_saved_to_file = 1;
#ifdef	GET_CONTI_PHY_MEM_VIA_PXP_LIB
		} else if (strcmp(argv[i], "-u") == 0) {
			g_mem_type = V4L2_MEMORY_USERPTR;
#endif
		} else if (strcmp(argv[i], "-l") == 0) {
			vl42_device_cap_list();
			return -1;
		} else {
			print_help();
			return -1;
		}
	}

	return 0;
}

static int signal_thread(void *arg)
{
	int sig;

	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	while (1) {
		sigwait(&sigset, &sig);
		if (sig == SIGINT) {
			v4l2_info("Ctrl-C received. Exiting.\n");
		} else {
			v4l2_err("Unknown signal. Still exiting\n");
		}
		quitflag = 1;
		break;
	}
	return 0;
}

#ifdef	GET_CONTI_PHY_MEM_VIA_PXP_LIB
void memfree(int buf_size, int buf_cnt)
{
	int i;
	unsigned int page_size;

	page_size = getpagesize();
	buf_size = (buf_size + page_size - 1) & ~(page_size - 1);

	for (i = 0; i < buf_cnt; i++) {
		if (buffers[i].start) {
			pxp_put_mem(&mem[i]);
			buffers[i].start = NULL;
		}
	}
	pxp_uninit();
}

int memalloc(int buf_size, int buf_cnt)
{
	int i, ret;
        unsigned int page_size;

	ret = pxp_init();
	if (ret < 0) {
		v4l2_err("pxp init err\n");
		return -1;
	}

	for (i = 0; i < buf_cnt; i++) {
		page_size = getpagesize();
		buf_size = (buf_size + page_size - 1) & ~(page_size - 1);
		buffers[i].length = mem[i].size = buf_size;
		ret = pxp_get_mem(&mem[i]);
		if (ret < 0) {
			v4l2_err("Get PHY memory fail\n");
			ret = -1;
			goto err;
		}
		buffers[i].offset = mem[i].phys_addr;
		buffers[i].start = (unsigned char *)mem[i].virt_uaddr;
		if (!buffers[i].start) {
			v4l2_err("mmap fail\n");
			ret = -1;
			goto err;
		}
		v4l2_info("%s, buf_size=0x%x\n", __func__, buf_size);
		v4l2_info("USRP: alloc bufs va=%p, pa=0x%x, size %d\n",
				buffers[i].start, buffers[i].offset, buf_size);
	}

	return ret;
err:
	memfree(buf_size, buf_cnt);
	return ret;
}
#else
void memfree(int buf_size, int buf_cnt) {}
int memalloc(int buf_size, int buf_cnt) { return 0; }
#endif

int main(int argc, char **argv)
{
	int fd_v4l;
	quitflag = 0;
	int ret;
	char *soc_list[] = {"i.MX6SLL", "i.MX6ULL", "i.MX6UL",
			"i.MX7D", "i.MX6SX", "i.MX6SL", "i.MX8MQ",
			"i.MX8MM", " "};

	print_name(argv);

	ret = soc_version_check(soc_list);
	if (ret == 0) {
		v4l2_err("mx6s_v4l2_capture.out not supported on current soc\n");
		return 0;
	}

	pthread_t sigtid;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	pthread_create(&sigtid, NULL, (void *)&signal_thread, NULL);

	/* use input parm  */
	if (process_cmdline(argc, argv) < 0) {
		return -1;
	}

	fd_v4l = v4l_capture_setup();
	if (fd_v4l < 0)
		return -1;

	if (g_mem_type == V4L2_MEMORY_USERPTR)
		if (memalloc(g_frame_size, TEST_BUFFER_NUM) < 0) {
			close(fd_v4l);
		}

	v4l_capture_test(fd_v4l);

	if (!g_saved_to_file) {
		if (kms.drm_fd > 0) {
			drm_cleanup(&kms);
			close(kms.drm_fd);
		}
	} else
		close(fd_v4l);

	if (g_mem_type == V4L2_MEMORY_USERPTR)
		memfree(g_frame_size, TEST_BUFFER_NUM);

	print_result(argv);

	return 0;
}
