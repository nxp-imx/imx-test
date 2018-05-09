/*
 *  Copyright 2017-2018 NXP
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or late.
 *
 */

/*
 * @file mx8_v4l2_cap_drm.c
 *
 * @brief MX8 Video For Linux 2 driver test application
 *
 */

/* Standard Include Files */
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

#include "../../include/soc_check.h"

static int log_level = 6;

#define DBG_LEVEL	6
#define INFO_LEVEL	5
#define ERR_LEVEL	4

#define v4l2_printf(LEVEL, fmt, args...) \
do {                                      \
	if (LEVEL <= log_level)                \
		printf(fmt, ##args);              \
} while(0)

#define v4l2_info(fmt, args...) v4l2_printf(INFO_LEVEL, fmt, ##args)
#define v4l2_dbg(fmt, args...) v4l2_printf(DBG_LEVEL, fmt, ##args)
#define v4l2_err(fmt, args...) v4l2_printf(ERR_LEVEL, fmt, ##args)

#define TEST_BUFFER_NUM 3
#define MAX_V4L2_DEVICE_NR     64
#define MAX_SIZE	3

#define NUM_PLANES 3
#define MAX_NUM_CARD	8

sigset_t sigset;
int quitflag;

static int start_streamon(void);

struct plane_buffer {
	unsigned char *start;
	size_t offset;
	unsigned int length;
	unsigned int plane_size;
};

struct testbuffer {
	unsigned char *start;
	size_t offset;
	unsigned int length;
	struct plane_buffer planes[NUM_PLANES];
};

struct video_channel {
	/* v4l2 info */
	int init;
	int on;
	int index;		//video channel index
	int out_width;
	int out_height;
	int cap_fmt;
	int v4l_dev;
	char v4l_dev_name[100];
	int mem_type;
	char save_file_name[100];
	FILE *pfile;
	int cur_buf_id;
	struct testbuffer buffers[TEST_BUFFER_NUM];
	int frame_num;

	/* fb info */
	int x_offset;
	int y_offset;
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

struct video_channel video_ch[8];

int g_cam_max = 8;		/* MAX support video channel */
int g_cam_num = 0;		/* Current enablke video channel */
int g_cam = 0;			/* video channel select */
int g_out_width = 1280;
int g_out_height = 800;
int g_cap_fmt = V4L2_PIX_FMT_RGB32;
int g_fb_fmt = V4L2_PIX_FMT_RGB32;
int g_capture_mode = 0;
int g_timeout = 10;
int g_camera_framerate = 30;	/* 30 fps */
int g_loop = 0;
int g_mem_type = V4L2_MEMORY_MMAP;
int g_saved_to_file = 0;
int g_performance_test = 0;
int g_num_planes = 1;
char g_fmt_name[10] = {"rgb32"};

char g_v4l_device[8][100] = {
	"/dev/video0",
	"/dev/video1",
	"/dev/video2",
	"/dev/video3",
	"/dev/video4",
	"/dev/video5",
	"/dev/video6",
	"/dev/video7"
};

char g_saved_filename[8][100] = {
	"0.",
	"1.",
	"2.",
	"3.",
	"4.",
	"5.",
	"6.",
	"7.",
};

static __u32 fmt_array[] = {
	V4L2_PIX_FMT_RGB32,
	V4L2_PIX_FMT_NV12,
};

void show_device_cap_list(void);
int prepare_capturing(int ch_id);

static void print_pixelformat(char *prefix, int val)
{
	v4l2_dbg("%s: %c%c%c%c\n", prefix ? prefix : "pixelformat",
	       val & 0xff,
	       (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) & 0xff);
}

static int get_num_planes_by_fmt(__u32 fmt)
{
	int planes;

	switch (fmt) {
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_YUV32:
		planes = 1;
		break;
	case V4L2_PIX_FMT_NV12:
		planes = 2;
		break;
	default:
		planes = 0;
		printf("Not found support format, planes=%d\n", planes);
	}

	return planes;
}

static void get_fmt_name(__u32 fmt)
{
	switch (fmt) {
	case V4L2_PIX_FMT_RGB32:
		strcpy(g_fmt_name, "rgb32");
		break;
	case V4L2_PIX_FMT_NV12:
		strcpy(g_fmt_name, "nv12");
		break;
	default:
		strcpy(g_fmt_name, "null");
	}
}

void print_help(void)
{
	v4l2_info("CSI Video4Linux capture Device Test\n"
	       "Syntax: ./mx8_cap\n"
	       " -t <time>\n"
	       " -of save to file \n"
	       " -l <device support list>\n"
	       " -cam <device index> 0bxxxx,xxxx\n"
	       " -log <log_level>   output all information, log_level should be 6\n"
	       " -d \"/dev/videoX\" if user use this option, -cam should be 1\n"
	       "example:\n"
	       "./mx8_cap -cam 1      capture data from video0 and playback\n"
	       "./mx8_cap -cam 3      capture data from video0/1 and playback\n"
	       "./mx8_cap -cam 7 -of  capture data from video0~2 and save to 0~2.rgb32\n"
	       "./mx8_cap -cam 255  -of capture data from video0~7 and save to 0~7.rgb32\n"
	       "./mx8_cap -cam 0xff -of capture data from video0~7 and save to 0~7.rgb32\n");
}

int process_cmdline(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-cam") == 0) {
			unsigned long mask;
			mask = strtoul(argv[++i], NULL, 0);
			g_cam = mask;
		} else if (strcmp(argv[i], "-t") == 0) {
			g_timeout = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-loop") == 0) {
			g_loop = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-help") == 0) {
			print_help();
			return -1;
		} else if (strcmp(argv[i], "-of") == 0) {
			g_saved_to_file = 1;
		} else if (strcmp(argv[i], "-l") == 0) {
			show_device_cap_list();
			return -1;
		} else if (strcmp(argv[i], "-p") == 0) {
			g_performance_test = 1;
		} else if (strcmp(argv[i], "-log") == 0) {
			log_level = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-m") == 0) {
			g_capture_mode = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-fr") == 0) {
			g_camera_framerate = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-fmt") == 0) {
			g_cap_fmt = fmt_array[atoi(argv[++i])];
		} else if (strcmp(argv[i], "-d") == 0) {
			if (g_cam != 1) {
				print_help();
				return -1;
			}
			strcpy(g_v4l_device[0], argv[++i]);
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
			printf("Ctrl-C received. Exiting.\n");
		} else {
			printf("Unknown signal. Still exiting\n");
		}
		quitflag = 1;
		break;
	}
	return 0;
}


int init_video_channel(int ch_id)
{
	video_ch[ch_id].init = 1;
	video_ch[ch_id].out_width = g_out_width;
	video_ch[ch_id].out_height = g_out_height;
	video_ch[ch_id].cap_fmt = g_cap_fmt;
	video_ch[ch_id].mem_type = g_mem_type;
	video_ch[ch_id].x_offset = 0;
	video_ch[ch_id].y_offset = 0;

	strcpy(video_ch[ch_id].v4l_dev_name, g_v4l_device[ch_id]);
	strcpy(video_ch[ch_id].save_file_name, g_saved_filename[ch_id]);
	v4l2_dbg("%s, %s init %d\n", __func__,
	       video_ch[ch_id].v4l_dev_name, ch_id);
	return 0;
}

static void dump_drm_clients(const int dev_num)
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

static int drm_setup(struct drm_kms *kms)
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

static int mx8_capturing_prepare(void)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		if (video_ch[i].on) {
			if (prepare_capturing(i) < 0) {
				v4l2_err
				    ("prepare_capturing failed, channel%d\n",
				     i);
				return -1;
			}
		}
	return 0;
}

static int mx8_qbuf(void)
{
	unsigned int i;
	unsigned int j;
	struct v4l2_buffer buf;
	struct v4l2_plane *planes = NULL;

	planes = calloc(g_num_planes, sizeof(*planes));
	if (!planes) {
		v4l2_err("%s, alloc plane mem fail\n", __func__);
		return -1;
	}

	for (j = 0; j < 8; j++) {
		if (video_ch[j].on) {
			int fd_v4l = video_ch[j].v4l_dev;
			int mem_type = video_ch[j].mem_type;
			for (i = 0; i < TEST_BUFFER_NUM; i++) {
				memset(&buf, 0, sizeof(buf));
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				buf.memory = mem_type;
				buf.m.planes = planes;
				buf.index = i;
				buf.length = g_num_planes;
				for (int k = 0; k < g_num_planes; k++) {
					buf.m.planes[k].length = video_ch[j].buffers[i].planes[k].length;

					if (mem_type == V4L2_MEMORY_USERPTR)
						buf.m.planes->m.userptr =
							(unsigned long)video_ch[j].buffers[i].start;
					else
						buf.m.planes[k].m.mem_offset =
							video_ch[j].buffers[i].planes[j].offset;
				}

				if (ioctl(fd_v4l, VIDIOC_QBUF, &buf) < 0) {
					v4l2_err("VIDIOC_QBUF error\n");
					free(planes);
					return -1;
				}
			}
		}
	}

	free(planes);
	return 0;
}

static int free_buffer(int ch_id)
{
	struct v4l2_requestbuffers req;
	int fd_v4l = video_ch[ch_id].v4l_dev;
	int mem_type = video_ch[ch_id].mem_type;
	int i;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		for (int k = 0; k < g_num_planes; k++) {
			munmap(video_ch[ch_id].buffers[i].planes[k].start,
					video_ch[ch_id].buffers[i].planes[k].length);
		}
	}

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = mem_type;

	if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
		v4l2_err("free buffer failed (chan_ID:%d)\n", ch_id);
		return -1;
	}

	return 0;
}

int prepare_capturing(int ch_id)
{
	unsigned int i;
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers req;
	struct v4l2_plane *planes = NULL;
	int fd_v4l = video_ch[ch_id].v4l_dev;
	int mem_type = video_ch[ch_id].mem_type;

	planes = calloc(g_num_planes, sizeof(*planes));
	if (!planes) {
		v4l2_err("%s, alloc plane mem fail\n", __func__);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = mem_type;

	if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
		v4l2_err("VIDIOC_REQBUFS failed\n");
		goto err;
	}

	if (req.count < TEST_BUFFER_NUM) {
		v4l2_err("Can't alloc 3 buffers\n");
		goto err;
	}

	if (mem_type == V4L2_MEMORY_MMAP) {
		for (i = 0; i < TEST_BUFFER_NUM; i++) {
			memset(&buf, 0, sizeof(buf));

			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			buf.memory = mem_type;
			buf.m.planes = planes;
			buf.length = g_num_planes;	/* plane num */
			buf.index = i;

			if (ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
				v4l2_err("VIDIOC_QUERYBUF error\n");
				goto err;
			}

			for (int j = 0; j < g_num_planes; j++) {
				video_ch[ch_id].buffers[i].planes[j].length = buf.m.planes[j].length;
				video_ch[ch_id].buffers[i].planes[j].offset = (size_t)buf.m.planes[j].m.mem_offset;
				video_ch[ch_id].buffers[i].planes[j].start =
				mmap(NULL,
					 video_ch[ch_id].buffers[i].planes[j].length,
					 PROT_READ | PROT_WRITE, MAP_SHARED,
					 fd_v4l, video_ch[ch_id].buffers[i].planes[j].offset);

				v4l2_dbg
					("buffer[%d]->planes[%d] startAddr=0x%x, offset=0x%x, buf_size=%d\n",
					 i, j,
					 (unsigned int *)video_ch[ch_id].buffers[i].planes[j].start,
					 video_ch[ch_id].buffers[i].planes[j].offset,
					 video_ch[ch_id].buffers[i].planes[j].length);
			}
		}
	}

	free(planes);

	return 0;
err:
	free(planes);
	return -1;
}

int start_capturing(int ch_id)
{
	enum v4l2_buf_type type;

	int fd_v4l = video_ch[ch_id].v4l_dev;
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd_v4l, VIDIOC_STREAMON, &type) < 0) {
		v4l2_err("VIDIOC_STREAMON error\n");
		return -1;
	}
	v4l2_dbg("%s channel=%d, v4l_dev=0x%x\n", __func__, ch_id, fd_v4l);
	return 0;
}

int stop_capturing(int ch_id)
{
	enum v4l2_buf_type type;
	int fd_v4l = video_ch[ch_id].v4l_dev;
	int nframe = video_ch[ch_id].frame_num;

	v4l2_dbg("%s channel=%d, v4l_dev=0x%x, frames=%d\n", __func__,
	       ch_id, fd_v4l, nframe);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	return ioctl(fd_v4l, VIDIOC_STREAMOFF, &type);
}

void show_device_cap_list(void)
{
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmivalenum frmival;
	struct v4l2_frmsizeenum frmsize;
	int fd_v4l = 0;
	char v4l_name[20];
	int i;

	for (i = 0; i < 10; i++) {
		snprintf(v4l_name, sizeof(v4l_name), "/dev/video%d", i);

		if ((fd_v4l = open(v4l_name, O_RDWR, 0)) < 0) {
			v4l2_err
			    ("\nunable to open %s for capture device.\n",
			     v4l_name);
		} else
			v4l2_info("\nopen video device %s \n", v4l_name);

		if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
			if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
				v4l2_info
				    ("Found v4l2 MPLANE capture device %s\n",
				     v4l_name);
				fmtdesc.index = 0;
				fmtdesc.type =
				    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				while (ioctl
				       (fd_v4l, VIDIOC_ENUM_FMT,
					&fmtdesc) >= 0) {
					print_pixelformat
					    ("pixelformat (output by camera)",
					     fmtdesc.pixelformat);
					frmsize.pixel_format =
					    fmtdesc.pixelformat;
					frmsize.index = 0;
					while (ioctl
					       (fd_v4l,
						VIDIOC_ENUM_FRAMESIZES,
						&frmsize) >= 0) {
						frmival.index = 0;
						frmival.pixel_format =
						    fmtdesc.pixelformat;
						frmival.width =
						    frmsize.discrete.width;
						frmival.height =
						    frmsize.discrete.height;
						while (ioctl
						       (fd_v4l,
							VIDIOC_ENUM_FRAMEINTERVALS,
							&frmival) >= 0) {
							v4l2_dbg
							    ("CaptureMode=%d, Width=%d, Height=%d %.3f fps\n",
							     frmsize.index,
							     frmival.width,
							     frmival.height,
							     1.0 *
							     frmival.discrete.
							     denominator /
							     frmival.discrete.
							     numerator);
							frmival.index++;
						}
						frmsize.index++;
					}
					fmtdesc.index++;
				}
			} else
				v4l2_err
				    ("Video device %s not support v4l2 capture\n",
				     v4l_name);
		}
		close(fd_v4l);
	}
}

int v4l_capture_setup(int ch_id)
{
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_capability cap;
	struct v4l2_frmsizeenum frmsize;
	int fd_v4l;
	int i;

	v4l2_dbg("Try to open device %s\n", video_ch[ch_id].v4l_dev_name);
	if ((fd_v4l = open(video_ch[ch_id].v4l_dev_name, O_RDWR, 0)) < 0) {
		v4l2_err("unable to open v4l2 %s for capture device.\n",
			   video_ch[ch_id].v4l_dev_name);
		return -1;
	}

	/* Get chipident */
	struct v4l2_dbg_chip_ident chipident;
	memset(&chipident, 0, sizeof(chipident));
	if (ioctl(fd_v4l, VIDIOC_DBG_G_CHIP_IDENT, &chipident) < 0) {
		v4l2_err("get chip ident fail\n");
		goto fail;
	}
	v4l2_info("Get chip ident: %s\n", chipident.match.name);

	if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
		v4l2_dbg("cap=0x%x\n", cap.capabilities);
		if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
			v4l2_err("%s not support v4l2 capture device.\n",
				   video_ch[ch_id].v4l_dev_name);
			goto fail;
		}
	} else {
		close(fd_v4l);
		v4l2_err("VIDIOC_QUERYCAP fail, chan_ID:%d\n", ch_id);
		return -1;
	}

	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	/* enum channels fmt */
	for (i = 0;; i++) {
		fmtdesc.index = i;
		if (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) < 0) {
			v4l2_err("VIDIOC ENUM FMT failed, index=%d \n", i);
			break;
		}
		v4l2_dbg("index=%d\n", fmtdesc.index);
		print_pixelformat("pixelformat (output by camera)",
				  fmtdesc.pixelformat);
	}

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	parm.parm.capture.capturemode = g_capture_mode;
	if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0) {
		v4l2_err("VIDIOC_S_PARM failed, chan_ID:%d\n", ch_id);
		goto fail;
	}

	frmsize.pixel_format = g_cap_fmt;
	frmsize.index = g_capture_mode;
	if (ioctl(fd_v4l, VIDIOC_ENUM_FRAMESIZES, &frmsize) < 0) {
		v4l2_err("get capture mode %d framesize failed\n", g_capture_mode);
		goto fail;
	}

	if (g_cam_num == 1) {
		video_ch[ch_id].out_width = frmsize.discrete.width;
		video_ch[ch_id].out_height = frmsize.discrete.height;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = video_ch[ch_id].cap_fmt;
	fmt.fmt.pix_mp.width = video_ch[ch_id].out_width;
	fmt.fmt.pix_mp.height = video_ch[ch_id].out_height;
	fmt.fmt.pix_mp.num_planes = g_num_planes;	/* RGB */
	if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
		v4l2_err("set format failed, chan_ID:%d\n", ch_id);
		goto fail;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd_v4l, VIDIOC_G_FMT, &fmt) < 0) {
		v4l2_err("get format failed, chan_ID:%d\n", ch_id);
		goto fail;
	}
	v4l2_dbg("video_ch=%d, width=%d, height=%d, \n",
		   ch_id, fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height);
	print_pixelformat("pixelformat", fmt.fmt.pix_mp.pixelformat);

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd_v4l, VIDIOC_G_PARM, &parm) < 0) {
		v4l2_err("VIDIOC_G_PARM failed, chan_ID:%d\n", ch_id);
		parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	}

	v4l2_dbg("\t WxH@fps = %dx%d@%d\n", fmt.fmt.pix_mp.width,
		   fmt.fmt.pix_mp.height,
		   parm.parm.capture.timeperframe.denominator);

	for (int k = 0; k < TEST_BUFFER_NUM; k++) {
		for (int i = 0; i < g_num_planes; i++) {
			video_ch[ch_id].buffers[k].planes[i].plane_size =
				fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
			v4l2_dbg("\t buffer[%d]->plane[%d]->Image size = %d\n",
						k, i, fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
		}
	}

	video_ch[ch_id].v4l_dev = fd_v4l;
	video_ch[ch_id].on = 1;

	v4l2_dbg("%s, Open v4l_dev=0x%x, channel=%d\n",
		   __func__, video_ch[ch_id].v4l_dev, ch_id);

	return 0;

fail:
	close(fd_v4l);
	return -1;
}

int config_video_channel(struct drm_kms *kms)
{
	int x_out;
	int y_out;
	int x_offset[8];
	int y_offset[8];
	int cam_num, i;
	int x_res, y_res;

	x_res = kms->width;
	y_res = kms->height;

	if (g_cam_num < 1) {
		v4l2_err("Error cam number %d\n", g_cam_num);
		return -1;
	}

	v4l2_info("xres=%d, y_res=%d\n", x_res, y_res);
	x_offset[0] = 0;
	y_offset[0] = 0;
	if (g_cam_num == 2) {
		x_out = x_res / 2;
		y_out = y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
	} else if ((g_cam_num == 3) || (g_cam_num == 4)) {
		x_out = x_res / 2;
		y_out = y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
		x_offset[2] = 0;
		y_offset[2] = y_out;
		x_offset[3] = x_out;
		y_offset[3] = y_out;
	} else if ((g_cam_num == 5) || (g_cam_num == 6)) {
		x_out = x_res / 3;
		y_out = y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
		x_offset[2] = x_out * 2;
		y_offset[2] = 0;
		x_offset[3] = 0;
		y_offset[3] = y_out;
		x_offset[4] = x_out;
		y_offset[4] = y_out;
		x_offset[4] = x_out * 2;
		y_offset[4] = y_out;
	} else if ((g_cam_num == 7) || (g_cam_num == 8)) {
		x_out = x_res / 4;
		y_out = y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
		x_offset[2] = x_out * 2;
		y_offset[2] = 0;
		x_offset[3] = x_out * 3;
		y_offset[3] = 0;
		x_offset[4] = 0;
		y_offset[4] = y_out;
		x_offset[5] = x_out;
		y_offset[5] = y_out;
		x_offset[6] = x_out * 2;
		y_offset[6] = y_out;
		x_offset[7] = x_out * 3;
		y_offset[7] = y_out;
	} else {
		/* g_cam_num == 1 */
		/* keep default frame size  */
		return 0;
	}

	cam_num = 0;
	for (i = 0; i < 8; i++) {
		if (video_ch[i].init) {
			video_ch[i].out_height = y_out;
			video_ch[i].out_width = x_out;
			video_ch[i].x_offset = x_offset[cam_num];
			video_ch[i].y_offset = y_offset[cam_num];
			v4l2_info
			    ("ch_id=%d, w=%d, h=%d, x_offset=%d, y_offset=%d\n",
			     i, x_out, y_out, x_offset[cam_num],
			     y_offset[cam_num]);
			cam_num++;
		}
	}
	return 0;
}

int set_up_frame_drm(int ch_id, struct drm_kms *kms)
{
	int out_h, out_w;
	int stride;
	int bufoffset;
	int j;
	int bytes_per_line;
	int buf_id = video_ch[ch_id].cur_buf_id;

	bytes_per_line = kms->bytes_per_pixel * kms->width;
	bufoffset = video_ch[ch_id].x_offset * kms->bpp / 8 +
	    video_ch[ch_id].y_offset * bytes_per_line;

	out_h = video_ch[ch_id].out_height - 1;
	out_w = video_ch[ch_id].out_width;
	stride = out_w * kms->bpp >> 3;

	/* fb buffer offset 1 frame */
	/*bufoffset += 0;*/
	for (j = 0; j < out_h; j++)
		memcpy(kms->fb_base + bufoffset +
			   j * bytes_per_line,
			   video_ch[ch_id].buffers[buf_id].planes[0].start +
			   j * stride, stride);
	return 0;
}

int get_video_channel_buffer(int ch_id)
{
	int fd_v4l = video_ch[ch_id].v4l_dev;
	struct v4l2_buffer buf;
	struct v4l2_plane *planes = NULL;

	planes = calloc(g_num_planes, sizeof(*planes));
	if (!planes) {
		v4l2_err("%s, alloc plane mem fail\n", __func__);
		return -1;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = video_ch[ch_id].mem_type;
	buf.m.planes = planes;
	buf.length = g_num_planes;
	if (ioctl(fd_v4l, VIDIOC_DQBUF, &buf) < 0) {
		v4l2_err("VIDIOC_DQBUF failed.\n");
		free(planes);
		return -1;
	}

	video_ch[ch_id].frame_num++;
	video_ch[ch_id].cur_buf_id = buf.index;

	free(planes);

	return 0;
}

int put_video_channel_buffer(int ch_id)
{
	int fd_v4l = video_ch[ch_id].v4l_dev;
	struct v4l2_buffer buf;
	struct v4l2_plane *planes = NULL;
	int buf_id = video_ch[ch_id].cur_buf_id;

	planes = calloc(g_num_planes, sizeof(*planes));
	if (!planes) {
		v4l2_err("%s, alloc plane mem fail\n", __func__);
		return -1;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = g_mem_type;
	buf.m.planes = planes;
	buf.index = buf_id;

	buf.length = g_num_planes;
	for (int k = 0; k < buf.length; k++) {
		buf.m.planes[k].length =
			video_ch[ch_id].buffers[buf_id].planes[k].length;
	}

	if (ioctl(fd_v4l, VIDIOC_QBUF, &buf) < 0) {
		v4l2_err("VIDIOC_QBUF failed, video=%d\n", ch_id);
		free(planes);
		return -1;
	}
	free(planes);
	return 0;
}

int open_save_file(void)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (video_ch[i].init) {
			if ((video_ch[i].pfile =
			     fopen(video_ch[i].save_file_name, "wb")) == NULL) {
				v4l2_err("Unable to create recording file, \n");
				return -1;
			}
		}
	}
	return 0;
}

int save_to_file(int ch_id)
{
	size_t wsize;
	int buf_id = video_ch[ch_id].cur_buf_id;
	int i;

	if (video_ch[ch_id].pfile) {
		/* Save capture frame to file */
		for (i = 0; i < g_num_planes; i++) {
			wsize = fwrite(video_ch[ch_id].buffers[buf_id].planes[i].start,
					   video_ch[ch_id].buffers[buf_id].planes[i].plane_size, 1,
					   video_ch[ch_id].pfile);

			if (wsize < 1) {
				v4l2_err
					("No space left on device. Stopping after %d frames.\n",
					 video_ch[ch_id].frame_num);
				return -1;
			}
		}
	}
	return 0;
}

int close_save_file(void)
{
	int i;

	for (i = 0; i < 8; i++)
		if (video_ch[i].on && video_ch[i].pfile)
			fclose(video_ch[i].pfile);

	return 0;
}

void close_vdev_file(void)
{
	int i;

	for (i = 0; i < 8; i++)
		if ((video_ch[i].v4l_dev > 0) && video_ch[i].on)
			close(video_ch[i].v4l_dev);
}

int set_vdev_parm(int ch_id)
{
	struct v4l2_format fmt;
	int fd_v4l;

	if (video_ch[ch_id].on) {
		fd_v4l = video_ch[ch_id].v4l_dev;

		memset(&fmt, 0, sizeof(fmt));
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt.fmt.pix_mp.pixelformat = video_ch[ch_id].cap_fmt;
		fmt.fmt.pix_mp.width = video_ch[ch_id].out_width;
		fmt.fmt.pix_mp.height = video_ch[ch_id].out_height;
		fmt.fmt.pix_mp.num_planes = g_num_planes;
		if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
			v4l2_err("set format failed\n");
			return -1;
		}
	}
	return 0;
}

int v4l_capture_test(struct drm_kms *kms)
{
	struct timeval tv1, tv2;
	int i;
	int ret;
	static int first_time_enter = 0;

loop:
	if (first_time_enter++ > 0) {
		v4l2_dbg(" ===== first_time_enter:%d ===== \n", first_time_enter);
		for (i = 0; i < g_cam_max; i++) {
			if (set_vdev_parm(i) < 0)
				return -1;
		}
		first_time_enter = 2;
	}

	if (mx8_capturing_prepare() < 0)
		return -1;

	if (mx8_qbuf() < 0)
		return -1;

	if (start_streamon() < 0)
		return -1;

	gettimeofday(&tv1, NULL);
	do {
		/* DQBuf  */
		for (i = 0; i < 8; i++) {
			if (video_ch[i].on) {
				ret = get_video_channel_buffer(i);
				if (ret < 0)
					return -1;
				if (!g_saved_to_file) {
					/* Copy video buffer to fb buffer */
					ret = set_up_frame_drm(i, kms);
					if (ret < 0)
						return -1;
				} else {
					/* Performance test, skip write file operation */
					if (!g_performance_test) {
						ret = save_to_file(i);
						if (ret < 0)
							return -1;
					}
				}
			}
		}

		/* QBuf  */
		for (i = 0; i < 8; i++)
			if (video_ch[i].on)
				put_video_channel_buffer(i);

		gettimeofday(&tv2, NULL);
	} while ((tv2.tv_sec - tv1.tv_sec < g_timeout) && !quitflag);

	if (g_performance_test) {
		for (i = 0; i < 8; i++) {
			if (video_ch[i].on) {
				v4l2_info("Channel[%d]: frame:%d\n", i, video_ch[i].frame_num);
				v4l2_info("Channel[%d]: Performance = %d(fps)\n",
							i, video_ch[i].frame_num / g_timeout);
				video_ch[i].frame_num = 0;
			}
		}
	}

	/* stop channels / stream off */
	for (i = 0; i < 8; i++) {
		if (video_ch[i].on) {
			if (stop_capturing(i) < 0) {
				v4l2_err("stop_capturing failed, device %d\n", i);
				return -1;
			}
		}
	}

	for (i = 0; i < 8; i++) {
		if (video_ch[i].on) {
			if (free_buffer(i) < 0) {
				v4l2_err("stop_capturing failed, device %d\n", i);
				return -1;
			}
		}
	}

	if (g_loop > 0 && !quitflag) {
		v4l2_info(" ======= loop %d done! ========\n", g_loop);
		g_loop--;
		goto loop;
	}

	return 0;
}

static int start_streamon(void)
{
	int i;
	for (i = 0; i < 8; i++)
		if (video_ch[i].on) {
			if (start_capturing(i) < 0) {
				v4l2_err
				    ("start_capturing failed, channel%d\n", i);
				return -1;
			}
		}

	return 0;
}

int main(int argc, char **argv)
{
	struct drm_kms *kms;
	quitflag = 0;
	int i;
	int ret;
	char *soc_list[] = { "i.MX8QM", "i.MX8QXP", " " };

	ret = soc_version_check(soc_list);
	if (ret == 0) {
		v4l2_err("mx8_cap.out not supported on current soc\n");
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

	if (!g_saved_to_file) {
		kms = malloc(sizeof(*kms));
		if (!kms) {
			v4l2_err("No Memory Space\n");
			return -errno;
		}
	}

	memset(video_ch, 0, sizeof(struct video_channel) * 8);

	/* check cam */
	if ((g_cam & 0xFF) == 0)
		return -1;

	g_num_planes = get_num_planes_by_fmt(g_cap_fmt);
	get_fmt_name(g_cap_fmt);

	/* init all video channel to default value */
	g_cam_num = 0;
	for (i = 0; i < g_cam_max; i++) {
		if (g_cam >> i & 0x1) {
			g_cam_num++;
			strcat(g_saved_filename[i], g_fmt_name);
			init_video_channel(i);
		}
	}

	if (!g_saved_to_file) {
		ret = drm_setup(kms);
		if (ret < 0) {
			v4l2_err("drm setup failed\n");
			free(kms);
			return -1;
		}
	} else {
		/* Open save file */
		ret = open_save_file();
		if (ret < 0) {
			return -1;
		}
	}

	if (!g_saved_to_file) {
		/* config video channel according FB info */
		ret = config_video_channel(kms);
		if (ret < 0) {
			v4l2_err("config video failed\n");
			goto FAIL0;
		}
	} else {
		/* default video channel config, no resize */
	}

	/* open video device and enable channel */
	for (i = 0; i < g_cam_max; i++) {
		if (g_cam >> i & 0x1) {
			ret = v4l_capture_setup(i);
			if (ret < 0)
				goto FAIL1;
		}
	}

	if (v4l_capture_test(kms) < 0)
		goto FAIL1;

	v4l2_info("success!\n");

	if (!g_saved_to_file) {
		if (kms->drm_fd > 0) {
			drm_cleanup(kms);
			close(kms->drm_fd);
		}
		free(kms);
	}
	else {
		close_save_file();
	}
	close_vdev_file();

	return 0;

FAIL1:
	close_vdev_file();
FAIL0:
	if (!g_saved_to_file) {
		if (kms->drm_fd > 0) {
			drm_cleanup(kms);
			close(kms->drm_fd);
		}
		free(kms);
	} else
		close_save_file();

	return -1;
}
