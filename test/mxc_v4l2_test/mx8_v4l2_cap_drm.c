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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/videodev2.h>

#include "../../include/soc_check.h"

/* Helper Macro */
#define NUM_PLANES				3
#define TEST_BUFFER_NUM			3
#define NUM_SENSORS				8
#define NUM_CARDS				8
#define DEFAULT					4
#define WIDTH					1280
#define HEIGHT					800

#define INFO_LEVEL				4
#define DBG_LEVEL				5
#define ERR_LEVEL				6

#define v4l2_printf(LEVEL, fmt, args...)  \
do {                                      \
	if (LEVEL <= log_level)               \
		printf("(%s:%d): "fmt, __func__, __LINE__, ##args);   \
} while(0)

#define v4l2_info(fmt, args...)  \
		v4l2_printf(INFO_LEVEL,"\x1B[36m"fmt"\e[0m", ##args)
#define v4l2_dbg(fmt, args...)   \
		v4l2_printf(DBG_LEVEL, "\x1B[33m"fmt"\e[0m", ##args)
#define v4l2_err(fmt, args...)   \
	    v4l2_printf(ERR_LEVEL, "\x1B[31m"fmt"\e[0m", ##args)

/*
 * DRM modeset releated data structure definition
 */
struct drm_buffer {
	void *fb_base;

	__u32 width;
	__u32 height;
	__u32 stride;
	__u32 size;

	__u32 handle;
	__u32 buf_id;
};

struct drm_device {
	int drm_fd;

	__s32 crtc_id;
	__s32 card_id;
	uint32_t conn_id;

	__u32 bits_per_pixel;
	__u32 bytes_per_pixel;

	drmModeModeInfo mode;
	drmModeCrtc *saved_crtc;

	/* double buffering */
	struct drm_buffer buffers[2];
	__u32 nr_buffer;
	__u32 front_buf;
};

/*
 * V4L2 releated data structure definition
 */
struct plane_buffer {
	__u8 *start;
	__u32 plane_size;
	__u32 length;
	size_t offset;
};

struct testbuffer {
	struct plane_buffer planes[NUM_PLANES];
	__u32 nr_plane;
};

struct video_channel {
	int v4l_fd;
	int saved_file_fd;

	__u32 init;
	__u32 on;
	__u32 index;
	__u32 cap_fmt;
	__u32 mem_type;
	__u32 cur_buf_id;
	__u32 frame_num;

	__u32 out_width;
	__u32 out_height;

	char save_file_name[100];
	char v4l_dev_name[100];

	/* fb info */
	__s32 x_offset;
	__s32 y_offset;

	struct testbuffer buffers[TEST_BUFFER_NUM];
	__u32 nr_buffer;

	struct timeval tv1;
	struct timeval tv2;
};

struct v4l2_device {
	struct video_channel video_ch[NUM_SENSORS];
};

struct media_dev {
	struct drm_device *drm_dev;
	struct v4l2_device *v4l2_dev;
	__u32 total_frames_cnt;
};

/*
 * Global variable definition
 */
static sigset_t sigset;
static uint32_t log_level;
static uint32_t g_cam;
static uint32_t g_cam_num;
static uint32_t g_num_buffers;
static uint32_t g_num_frames;
static uint32_t g_num_planes;
static uint32_t g_capture_mode;
static uint32_t g_camera_framerate;
static uint32_t g_cap_fmt;
static uint32_t g_mem_type;
static uint32_t g_out_width;
static uint32_t g_out_height;

static bool g_saved_to_file;
static bool g_performance_test;
static bool quitflag;

static char g_v4l_device[NUM_SENSORS][100];
static char g_saved_filename[NUM_SENSORS][100];
static char g_fmt_name[10];

/*
 *
 */
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

static void global_vars_init(void)
{
	int i;

	for (i = 0; i < NUM_SENSORS; i++) {
		sprintf(g_v4l_device[i], "/dev/video%d", i);
		sprintf(g_saved_filename[i], "%d.", i);
	}
	sprintf(g_fmt_name, "rgb32");

	log_level = DEFAULT;
	g_cam_num = 0;
	g_num_planes = 1;
	g_num_buffers = TEST_BUFFER_NUM;
	g_num_frames = 300;
	g_out_width = WIDTH;
	g_out_height = HEIGHT;
	g_capture_mode = 0;
	g_camera_framerate = 30;
	g_cap_fmt = V4L2_PIX_FMT_XRGB32;
	g_mem_type = V4L2_MEMORY_MMAP;

	quitflag = false;
	g_saved_to_file = false;
	g_performance_test = false;
}

static void print_help(const char *name)
{
	v4l2_info("CSI Video4Linux capture Device Test\n"
	       "Syntax: %s\n"
	       " -num <numbers of frame need to be captured>\n"
	       " -of save to file \n"
	       " -l <device support list>\n"
	       " -log <log level, 6 will show all message>\n"
	       " -cam <device index> 0bxxxx,xxxx\n"
	       " -d \"/dev/videoX\" if user use this option, -cam should be 1\n"
		   " -p test performance, need to combine with \"-of\" option\n"
		   " -m <mode> specify camera sensor capture mode(mode:0, 1, 2, 3, 4)\n"
		   " -fr <frame_rate> support 15fps and 30fps\n"
		   " -fmt <format> support RGB32 and NV12, 0->RGB32, 1->NV12. NV12 not support playback\n"
	       "example:\n"
	       "./mx8_cap -cam 1        capture data from video0 and playback\n"
	       "./mx8_cap -cam 3        capture data from video0/1 and playback\n"
	       "./mx8_cap -cam 7 -of    capture data from video0~2 and save to 0~2.rgb32\n"
	       "./mx8_cap -cam 255 -of  capture data from video0~7 and save to 0~7.rgb32\n"
	       "./mx8_cap -cam 0xff -of capture data from video0~7 and save to 0~7.rgb32\n"
	       "./mx8_cap -cam 1 -fmt 1 -of capture data from video0 and save to 0.nv12\n"
	       "./mx8_cap -cam 1 -of -p test video0 performace\n", name);
}

static uint32_t fmt_array[] = {
	V4L2_PIX_FMT_XRGB32,
	V4L2_PIX_FMT_NV12,
};

static void show_device_cap_list(const char *name)
{
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmivalenum frmival;
	struct v4l2_frmsizeenum frmsize;
	int fd_v4l = 0;
	char v4l_name[20];
	int i;

	log_level = 6;
	if (g_cam != 0) {
		v4l2_err("only need -l option\n");
		print_help(name);
		log_level = DEFAULT;
		return;
	}

	for (i = 0; i < NUM_SENSORS; i++) {
		snprintf(v4l_name, sizeof(v4l_name), "/dev/video%d", i);

		if ((fd_v4l = open(v4l_name, O_RDWR, 0)) < 0) {
			v4l2_err("unable to open %s for capture device\n", v4l_name);
		} else
			v4l2_info("open video device %s\n", v4l_name);

		if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
			if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
				v4l2_info("Found v4l2 MPLANE capture device %s\n", v4l_name);
				fmtdesc.index = 0;
				fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				while (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
					v4l2_info("pixelformat (output by camera)%.4s\n",
					          (char *)&fmtdesc.pixelformat);
					frmsize.pixel_format = fmtdesc.pixelformat;
					frmsize.index = 0;
					while (ioctl (fd_v4l, VIDIOC_ENUM_FRAMESIZES,
								  &frmsize) >= 0) {
						frmival.index = 0;
						frmival.pixel_format = fmtdesc.pixelformat;
						frmival.width = frmsize.discrete.width;
						frmival.height = frmsize.discrete.height;
						while (ioctl(fd_v4l, VIDIOC_ENUM_FRAMEINTERVALS,
							        &frmival) >= 0) {
							v4l2_info
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
				v4l2_err("Video device %s not support v4l2 capture\n", v4l_name);
		}
		close(fd_v4l);
	}
	log_level = DEFAULT;
}

static int parse_cmdline(int argc, const char *argv[])
{
	int i;

	if (argc < 2) {
		print_help(argv[0]);
		return -1;
	}

	/* Parse command line */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-cam") == 0) {
			unsigned long mask;
			mask = strtoul(argv[++i], NULL, 0);
			g_cam = mask;
		} else if (strcmp(argv[i], "-num") == 0) {
			g_num_frames = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-help") == 0) {
			print_help(argv[0]);
			return -1;
		} else if (strcmp(argv[i], "-of") == 0) {
			g_saved_to_file = true;
		} else if (strcmp(argv[i], "-l") == 0) {
			show_device_cap_list(argv[0]);
			return -1;
		} else if (strcmp(argv[i], "-p") == 0) {
			g_performance_test = true;
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
				print_help(argv[0]);
				return -1;
			}
			strcpy(g_v4l_device[0], argv[++i]);
		} else {
			print_help(argv[0]);
			return -1;
		}
	}
	return 0;
}

static void get_fmt_name(uint32_t fourcc)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_XRGB32:
		strcpy(g_fmt_name, "rgb32");
		break;
	case V4L2_PIX_FMT_NV12:
		strcpy(g_fmt_name, "nv12");
		break;
	default:
		strcpy(g_fmt_name, "null");
	}
}

static int init_video_channel(int ch_id, struct video_channel *video_ch)
{
	video_ch[ch_id].init = 1;
	video_ch[ch_id].out_width = g_out_width;
	video_ch[ch_id].out_height = g_out_height;
	video_ch[ch_id].cap_fmt = g_cap_fmt;
	video_ch[ch_id].mem_type = g_mem_type;
	video_ch[ch_id].x_offset = 0;
	video_ch[ch_id].y_offset = 0;

	strcat(g_saved_filename[ch_id], g_fmt_name);
	strcpy(video_ch[ch_id].v4l_dev_name, g_v4l_device[ch_id]);

	if (g_saved_to_file) {
		strcpy(video_ch[ch_id].save_file_name, g_saved_filename[ch_id]);
		v4l2_info("init channel[%d] save_file_name=%s\n",
				   ch_id, video_ch[ch_id].save_file_name);
	}

	v4l2_info("init channel[%d] v4l2_dev_name=%s w/h=(%d,%d)\n",
			  ch_id,
			  video_ch[ch_id].v4l_dev_name,
			  video_ch[ch_id].out_width,
			  video_ch[ch_id].out_height);
	return 0;

}

static void v4l2_device_init(struct v4l2_device *v4l2)
{
	struct video_channel *video_ch = v4l2->video_ch;
	int i;

	get_fmt_name(g_cap_fmt);

	for (i = 0; i < NUM_SENSORS; i++) {
		if ((g_cam >> i) & 0x01) {
			init_video_channel(i, video_ch);
			++g_cam_num;
		}
	}
}

static int media_device_alloc(struct media_dev *media)
{
	struct v4l2_device *v4l2;
	struct drm_device *drm;

	v4l2 = malloc(sizeof(*v4l2));
	if (v4l2 == NULL) {
		v4l2_err("alloc v4l2 device fail\n");
		return -ENOMEM;
	}
	memset(v4l2, 0, sizeof(*v4l2));
	v4l2_device_init(v4l2);
	media->v4l2_dev = v4l2;

	if (!g_saved_to_file) {
		drm = malloc(sizeof(*drm));
		if (drm == NULL) {
			v4l2_err("alloc DRM device fail\n");
			free(v4l2);
			return -ENOMEM;
		}
		memset(drm, 0, sizeof(*drm));
		media->drm_dev = drm;
	}
	return 0;
}

static void media_device_free(struct media_dev *media)
{
	if (media->v4l2_dev)
		free(media->v4l2_dev);

	if (media->drm_dev && !g_saved_to_file)
		free(media->drm_dev);
}

static int open_save_file(struct video_channel *video_ch)
{
	int i, fd;

	if (g_performance_test)
		return 0;

	for (i = 0; i < NUM_SENSORS; i++) {
		if ((g_cam >> i) & 0x01) {
			fd = open(video_ch[i].save_file_name, O_RDWR | O_CREAT);
			if (fd < 0) {
				 v4l2_err("Channel[%d] unable to create recording file\n", i);
				 while (i)
					close(video_ch[--i].saved_file_fd);
				 return -1;
			}
			video_ch[i].saved_file_fd = fd;
			v4l2_info("open %s success\n", video_ch[i].save_file_name);
		}
	}
	return 0;
}

static int open_drm_device(struct drm_device *drm)
{
	char dev_name[15];
	uint64_t has_dumb;
	int fd, i;

	i = 0;
loop:
	sprintf(dev_name, "/dev/dri/card%d", i++);

	fd = open(dev_name, O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		v4l2_err("Open %s fail\n", dev_name);
		return -1;
	}

	if (drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb) < 0 ||
	    !has_dumb) {
		v4l2_err("drm device '%s' does not support dumb buffers\n", dev_name);
		close(fd);
		goto loop;
	}
	drm->drm_fd = fd;
	drm->card_id = --i;

	v4l2_info("Open %s success\n", dev_name);

	return 0;
}

static int open_v4l2_device(struct v4l2_device *v4l2)
{
	struct video_channel *video_ch = v4l2->video_ch;
	struct v4l2_capability cap;
	bool found;
	int fd, i, ret;

	for (i = 0; i < NUM_SENSORS; i++) {
		if ((g_cam >> i) & 0x01) {
			fd = open(video_ch[i].v4l_dev_name, O_RDWR, 0);
			if (fd < 0) {
				v4l2_err("unable to open v4l2 %s for capture device.\n",
					   video_ch[i].v4l_dev_name);
				while (i)
					close(video_ch[--i].v4l_fd);
				return -1;
			}

			memset(&cap, 0, sizeof(cap));
			ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
			if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
				ret < 0) {
				v4l2_err("not support multi-plane v4l2 capture device\n");
				close(fd);
				continue;
			}
			found = true;
			video_ch[i].v4l_fd = fd;
			v4l2_info("open %s success\n", video_ch[i].v4l_dev_name);
		}
	}
	if (!found) {
		v4l2_info("can't found muti-plane v4l2 capture device\n");
		return -1;
	}
	return 0;
}

static void close_save_file(struct video_channel *video_ch)
{
	int i;

	if (g_performance_test)
		return;

	for (i = 0; i < NUM_SENSORS; i++)
		if (((g_cam >> i) & 0x1) && (video_ch[i].saved_file_fd > 0))
			close(video_ch[i].saved_file_fd);
}

static void close_drm_device(int drm_fd)
{
	if (drm_fd > 0)
		close(drm_fd);
}

static int media_device_open(struct media_dev *media)
{
	struct v4l2_device *v4l2 = media->v4l2_dev;
	struct drm_device *drm = media->drm_dev;
	int ret;

	if (!g_saved_to_file)
		ret = open_drm_device(drm);
	else
		ret = open_save_file(v4l2->video_ch);

	if (ret < 0)
		return ret;

	ret = open_v4l2_device(v4l2);
	if (ret < 0) {
		if (g_saved_to_file)
			close_save_file(v4l2->video_ch);
		else
			close_drm_device(drm->drm_fd);
	}
	return ret;
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

static int modeset_find_crtc(struct drm_device *drm,
				drmModeRes *res, drmModeConnector *conn)
{
	drmModeEncoder *encoder;
	int drm_fd = drm->drm_fd;
	int crtc_id, j, i;

	for (i = 0; i < conn->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm_fd, conn->encoders[i]);
		if (!encoder) {
			v4l2_err("can't retrieve encoders[%d]\n", i);
			continue;
		}

		for (j = 0; j < res->count_crtcs; j++) {
			if (encoder->possible_crtcs & (1 << j)) {
				crtc_id = res->crtcs[j];
				if (crtc_id > 0) {
					drm->crtc_id = crtc_id;
					drmModeFreeEncoder(encoder);
					return 0;
				}
			}
			crtc_id = -1;
		}

		if (j == res->count_crtcs && crtc_id == -1) {
			v4l2_err("cannot find crtc\n");
			drmModeFreeEncoder(encoder);
			continue;
		}
		drmModeFreeEncoder(encoder);
	}
	v4l2_err("cannot find suitable CRTC for connector[%d]\n", conn->connector_id);
	return -ENOENT;
}

static int drm_create_fb(int fd, int index, struct drm_buffer *buf)
{
	struct drm_mode_create_dumb creq;
	struct drm_mode_destroy_dumb dreq;
	struct drm_mode_map_dumb mreq;
	int ret;

	memset(&creq, 0, sizeof(creq));
	creq.width = buf->width;
	creq.height = buf->height;
	creq.bpp = 32;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		v4l2_err("cannot create dumb buffer[%d]\n", index);
		return ret;
	}

	buf->stride = creq.pitch;
	buf->size = creq.size;
	buf->handle = creq.handle;

	ret = drmModeAddFB(fd, buf->width, buf->height, 24, 32,
				buf->stride, buf->handle, &buf->buf_id);
	if (ret < 0) {
		v4l2_err("Add framebuffer (%d) fail\n", index);
		goto destroy_fb;
	}

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = buf->handle;
	ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		v4l2_err("Map buffer[%d] dump ioctl fail\n", index);
		goto remove_fb;
	}

	buf->fb_base = mmap(0, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED,
							fd, mreq.offset);
	if (buf->fb_base == MAP_FAILED) {
		v4l2_err("Cannot mmap dumb buffer[%d]\n", index);
		goto remove_fb;
	}
	memset(buf->fb_base, 0, buf->size);

	return 0;

remove_fb:
	drmModeRmFB(fd, buf->buf_id);
destroy_fb:
	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	v4l2_err("Create DRM buffer[%d] fail\n", index);
	return ret;
}

static void drm_destroy_fb(int fd, int index, struct drm_buffer *buf)
{
	struct drm_mode_destroy_dumb dreq;

	munmap(buf->fb_base, buf->size);
	drmModeRmFB(fd, buf->buf_id);

	memset(&dreq, 0, sizeof(dreq));
	dreq.handle = buf->handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
}

static int modeset_setup_dev(struct drm_device *drm,
				drmModeRes *res, drmModeConnector *conn)
{
	struct drm_buffer *buf = drm->buffers;
	int i, ret;

	ret = modeset_find_crtc(drm, res, conn);
	if (ret < 0)
		return ret;

	memcpy(&drm->mode, &conn->modes[0], sizeof(drm->mode));
	/* Double buffering */
	for (i = 0; i < 2; i++) {
		buf[i].width  = conn->modes[0].hdisplay;
		buf[i].height = conn->modes[0].vdisplay;
		ret = drm_create_fb(drm->drm_fd, i, &buf[i]);
		if (ret < 0) {
			while(i)
				drm_destroy_fb(drm->drm_fd, i - 1, &buf[i-1]);
			return ret;
		}
		v4l2_dbg("DRM bufffer[%d] addr=0x%p size=%d w/h=(%d,%d) buf_id=%d\n",
				 i, buf[i].fb_base, buf[i].size,
				 buf[i].width, buf[i].height, buf[i].buf_id);
	}
	drm->bits_per_pixel = 32;
	drm->bytes_per_pixel = drm->bits_per_pixel >> 3;
	return 0;
}

static int drm_device_prepare(struct drm_device *drm)
{
	drmModeRes *res;
	drmModeConnector *conn;
	int drm_fd = drm->drm_fd;
	int ret, i;

	ret = drmSetMaster(drm_fd);
	if (ret < 0) {
		dump_drm_clients(drm->card_id);
		return ret;
	}

	res = drmModeGetResources(drm_fd);
	if (res == NULL) {
		v4l2_err("Cannot retrieve DRM resources\n");
		drmDropMaster(drm_fd);
		return -errno;
	}

	/* iterate all connectors */
	for (i = 0; i < res->count_connectors; i++) {
		/* get information for each connector */
		conn = drmModeGetConnector(drm_fd, res->connectors[i]);
		if (!conn) {
			v4l2_err("Cannot retrieve DRM connector %u:%u (%d)\n",
				i, res->connectors[i], errno);
			continue;
		}

		/* valid connector? */
		if (conn->connection != DRM_MODE_CONNECTED ||
					conn->count_modes == 0) {
			drmModeFreeConnector(conn);
			continue;
		}

		/* find a valid connector */
		drm->conn_id = conn->connector_id;
		ret = modeset_setup_dev(drm, res, conn);
		if (ret < 0) {
			v4l2_err("mode setup device environment fail\n");
			drmDropMaster(drm_fd);
			drmModeFreeConnector(conn);
			drmModeFreeResources(res);
			return ret;
		}
		drmModeFreeConnector(conn);
	}
	drmModeFreeResources(res);
	return 0;
}

static int config_video_channel(struct video_channel *video_ch,
								struct drm_buffer *buf)
{
	int x_out;
	int y_out;
	int x_offset[8];
	int y_offset[8];
	int cam_num, i;
	int x_res, y_res;

	x_res = buf->width;
	y_res = buf->height;

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

static void v4l2_enum_fmt(struct video_channel *video_ch)
{
	struct v4l2_fmtdesc fmtdesc;
	int fd = video_ch->v4l_fd;
	int ret, i = 0;

	memset(&fmtdesc, 0, sizeof(fmtdesc));
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	do {
		fmtdesc.index = i;
		ret = ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc);
		if (ret < 0) {
			v4l2_err("channel VIDIOC_ENUM_FMT fail\n");
			break;
		}
		v4l2_dbg("index=%d pixelformat=%.4s\n",
				 fmtdesc.index, (char *)&fmtdesc.pixelformat);
		i++;
	} while(1);
}

static void adjust_width_height_for_one_sensor(struct video_channel *video_ch)
{
	struct v4l2_frmsizeenum frmsize;
	int fd = video_ch->v4l_fd;
	int ret;

	memset(&frmsize, 0, sizeof(frmsize));
	frmsize.pixel_format = g_cap_fmt;
	frmsize.index = g_capture_mode;
	ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
	if (ret < 0) {
		v4l2_err("channel VIDIOC_ENUM_FRAMESIZES fail\n");
		return;
	}

	video_ch->out_width  = frmsize.discrete.width;
	video_ch->out_height = frmsize.discrete.height;
}

static void fill_video_channel(struct video_channel *video_ch,
			                   struct v4l2_format *fmt)
{
	int i, j;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		for (j = 0; j < g_num_planes; j++) {
			video_ch->buffers[i].planes[j].plane_size =
				      fmt->fmt.pix_mp.plane_fmt[j].sizeimage;
		}
	}
	video_ch->on = 1;
}

static int v4l2_setup_dev(int ch_id, struct video_channel *video_ch)
{
	struct v4l2_dbg_chip_ident chipident;
	struct v4l2_format fmt;
	struct v4l2_streamparm parm;
	int fd = video_ch[ch_id].v4l_fd;
	int ret;

	memset(&chipident, 0, sizeof(chipident));
	ret = ioctl(fd, VIDIOC_DBG_G_CHIP_IDENT, &chipident);
	if (ret < 0)
		v4l2_err("get chip ident fail\n");
	else
		v4l2_dbg("Get chip ident: %s\n", chipident.match.name);

	v4l2_enum_fmt(&video_ch[ch_id]);

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = g_camera_framerate;
	parm.parm.capture.capturemode = g_capture_mode;
	ret = ioctl(fd, VIDIOC_S_PARM, &parm);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_S_PARM failed\n", ch_id);
		return ret;
	}

	if (g_cam_num == 1)
		adjust_width_height_for_one_sensor(&video_ch[ch_id]);

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = video_ch[ch_id].cap_fmt;
	fmt.fmt.pix_mp.width = video_ch[ch_id].out_width;
	fmt.fmt.pix_mp.height = video_ch[ch_id].out_height;
	/*fmt.fmt.pix_mp.num_planes = g_num_planes; */
	ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_S_FMT fail\n", ch_id);
		return ret;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_G_FMT fail\n", ch_id);
		return ret;
	}
	g_num_planes = fmt.fmt.pix_mp.num_planes;

	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_G_PARM failed\n", ch_id);
		return ret;
	}

	fill_video_channel(&video_ch[ch_id], &fmt);

	v4l2_info("\tplanes=%d WxH@fps = %dx%d@%d\n",
			 fmt.fmt.pix_mp.num_planes,
		     fmt.fmt.pix_mp.width,
		     fmt.fmt.pix_mp.height,
		     parm.parm.capture.timeperframe.denominator);
	return 0;
}

static void get_memory_map_info(struct video_channel *video_ch)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = video_ch->v4l_fd;
	int ret, i, j;

	if (video_ch->mem_type != V4L2_MEMORY_MMAP)
		return;

	planes = malloc(g_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d planes fail\n", g_num_planes);
		return;
	}

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(*planes));

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = video_ch->mem_type;
		buf.m.planes = planes;
		buf.length = g_num_planes;	/* plane num */
		buf.index = i;
		ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			v4l2_err("query buffer[%d] info fail\n", i);
			free(planes);
			return;
		}

		for (j = 0; j < g_num_planes; j++) {
			video_ch->buffers[i].planes[j].length = buf.m.planes[j].length;
			video_ch->buffers[i].planes[j].offset =
				            (size_t)buf.m.planes[j].m.mem_offset;
			video_ch->buffers[i].planes[j].start = mmap(NULL,
					 video_ch->buffers[i].planes[j].length,
					 PROT_READ | PROT_WRITE, MAP_SHARED,
					 fd, video_ch->buffers[i].planes[j].offset);

			v4l2_dbg("V4L2 buffer[%d]->planes[%d]:"
					 "startAddr=0x%p, offset=0x%x, buf_size=%d\n",
					 i, j,
					 (unsigned int *)video_ch->buffers[i].planes[j].start,
					 (unsigned int)video_ch->buffers[i].planes[j].offset,
					 video_ch->buffers[i].planes[j].length);
		}
	}
	free(planes);
}

static void memory_map_info_put(struct video_channel *video_ch)
{
	int i, k;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		for (k = 0; k < g_num_planes; k++) {
			munmap(video_ch->buffers[i].planes[k].start,
				   video_ch->buffers[i].planes[k].length);
		}
	}
}

static int v4l2_create_buffer(int ch_id, struct video_channel *video_ch)
{
	struct v4l2_requestbuffers req;
	int fd = video_ch[ch_id].v4l_fd;
	int ret;

	memset(&req, 0, sizeof(req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = video_ch[ch_id].mem_type;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("chanel[%d] VIDIOC_REQBUFS failed\n", ch_id);
		return ret;
	}

	if (req.count < TEST_BUFFER_NUM) {
		v4l2_err("channel[%d] can't alloc enought buffers\n", ch_id);
		return -ENOMEM;
	}

	get_memory_map_info(&video_ch[ch_id]);

	v4l2_dbg("channel[%d] w/h=(%d,%d) alloc buffer success\n",
			  ch_id, video_ch[ch_id].out_width, video_ch[ch_id].out_height);
	return 0;
}

static void v4l2_destroy_buffer(int ch_id, struct video_channel *video_ch)
{
	struct v4l2_requestbuffers req;
	int fd = video_ch[ch_id].v4l_fd;
	int ret;

	memory_map_info_put(&video_ch[ch_id]);

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = video_ch[ch_id].mem_type;

	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("channel[%d] free v4l2 buffer fail\n", ch_id);
		return;
	}
	v4l2_dbg("channel[%d] free v4l2 buffer success\n", ch_id);
}

static void media_device_cleanup(struct media_dev *media)
{
	struct video_channel *video_ch = media->v4l2_dev->video_ch;
	int i, fd;

	if (!g_saved_to_file) {
		fd = media->drm_dev->drm_fd;
		drmDropMaster(fd);
		drm_destroy_fb(fd, 0, &media->drm_dev->buffers[0]);
		drm_destroy_fb(fd, 1, &media->drm_dev->buffers[1]);
	}

	for (i = 0; i < NUM_SENSORS; i++) {
		if (video_ch[i].on)
			v4l2_destroy_buffer(i, video_ch);
	}
}

static int queue_buffer(int buf_id, struct video_channel *video_ch)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = video_ch->v4l_fd;
	int k, ret;

	planes = malloc(g_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", g_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = video_ch->mem_type;
	buf.m.planes = planes;
	buf.index = buf_id;
	buf.length = g_num_planes;

	for (k = 0; k < g_num_planes; k++) {
		buf.m.planes[k].length = video_ch->buffers[buf_id].planes[k].length;
		if (video_ch->mem_type == V4L2_MEMORY_MMAP)
			buf.m.planes[k].m.mem_offset =
					video_ch->buffers[buf_id].planes[k].offset;
	}

	ret = ioctl(fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		v4l2_err("buffer[%d] VIDIOC_QBUF error\n", buf_id);
		free(planes);
		return ret;
	}

	/*v4l2_dbg("== qbuf ==\n");*/
	free(planes);
	return 0;
}

static int dqueue_buffer(int buf_id, struct video_channel *video_ch)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = video_ch->v4l_fd;
	int ret;

	planes = malloc(g_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", g_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = video_ch->mem_type;
	buf.m.planes = planes;
	buf.length = g_num_planes;

	ret = ioctl(fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		v4l2_err("buffer[%d] VIDIOC_DQBUF error\n", buf_id);
		free(planes);
		return ret;
	}
	video_ch->frame_num++;
	video_ch->cur_buf_id = buf.index;

	setbuf(stdout, NULL);
	printf(" %c", (video_ch->frame_num % 16 == 0 ||
			  video_ch->frame_num == g_num_frames) ? '\n' : '.' );
	free(planes);
	return 0;

}

static int v4l2_device_prepare(struct v4l2_device *v4l2,
							   struct drm_buffer *buf)
{
	struct video_channel *video_ch = v4l2->video_ch;
	int ret, i, j;

	if (!g_saved_to_file) {
		ret = config_video_channel(video_ch, buf);
		if (ret) {
			v4l2_err("config video faied\n");
			return ret;
		}
	}

	for (i = 0; i < NUM_SENSORS; i++) {
		if ((g_cam >> i) & 0x01) {
			ret = v4l2_setup_dev(i, video_ch);
			if (ret < 0) {
				v4l2_err("video_ch[%d] setup fail\n", i);
				return ret;
			}

			ret = v4l2_create_buffer(i, video_ch);
			if (ret < 0) {
				v4l2_err("video_ch[%d] create buffer fail\n", i);
				return ret;
			}

			for (j = 0; j < TEST_BUFFER_NUM; j++) {
				ret = queue_buffer(j, &video_ch[i]);
				if (ret < 0) {
					while (i) {
						if (video_ch[i].on)
							v4l2_destroy_buffer(i, video_ch);
						i--;
					}
					return ret;
				}
			}
		}
	}
	return 0;
}

static int v4l2_device_streamon(int ch_id, struct video_channel *video_ch)
{
	enum v4l2_buf_type type;
	int fd = video_ch[ch_id].v4l_fd;
	int ret;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_STREAMON error\n", ch_id);
		return ret;;
	}
	v4l2_info("channel[%d] v4l_dev=0x%x start capturing\n", ch_id, fd);
	return 0;
}

static int v4l2_device_streamoff(int ch_id, struct video_channel *video_ch)
{
	enum v4l2_buf_type type;
	int fd = video_ch[ch_id].v4l_fd;
	int nframe = video_ch[ch_id].frame_num;
	int ret;

	v4l2_info("channel[%d] v4l2_dev=0x%x frame=%d\n", ch_id, fd, nframe);

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		v4l2_err("channel[%d] VIDIOC_STREAMOFF error\n", ch_id);
		return ret;;
	}
	v4l2_info("channel[%d] v4l_dev=0x%x stop capturing\n", ch_id, fd);
	return 0;
}

static int media_device_prepare(struct media_dev *media)
{
	struct v4l2_device *v4l2 = media->v4l2_dev;
	struct drm_device *drm = media->drm_dev;
	int ret;

	if (!g_saved_to_file) {
		ret = drm_device_prepare(drm);
		if (ret < 0) {
			drmDropMaster(drm->drm_fd);
			return ret;
		}
	}

	ret = v4l2_device_prepare(v4l2, &drm->buffers[0]);
	if (ret < 0) {
		if (!g_saved_to_file) {
			drm_destroy_fb(drm->drm_fd, 0, &drm->buffers[0]);
			drm_destroy_fb(drm->drm_fd, 1, &drm->buffers[1]);
			drmDropMaster(drm->drm_fd);
			return ret;
		}
	}
	return 0;
}

static int media_device_start(struct media_dev *media)
{
	struct v4l2_device *v4l2 = media->v4l2_dev;
	struct video_channel *video_ch = v4l2->video_ch;
	struct drm_device *drm = media->drm_dev;
	struct drm_buffer *buf;
	int i, ret;

	if (!g_saved_to_file) {
		buf = &drm->buffers[drm->front_buf];
		ret = drmModeSetCrtc(drm->drm_fd, drm->crtc_id, buf->buf_id,
							 0, 0, &drm->conn_id, 1, &drm->mode);
		if (ret < 0) {
			v4l2_err("buffer[%d] set CRTC fail\n", buf->buf_id);
			return ret;
		}
		v4l2_dbg("crtc_id=%d conn_id=%d buf_id=%d\n",
				 drm->crtc_id, drm->conn_id,
				 drm->buffers[drm->front_buf].buf_id);

		for (i = 0; i < NUM_SENSORS; i++) {
			if (v4l2->video_ch[i].on) {
				if (video_ch[i].out_width > buf->width ||
					video_ch[i].out_height > buf->height)
					v4l2_info("sensor image dimension is bigger than screen "
							  "resolution, so truncate to adapt to display\n");
			}
		}

	}

	for (i = 0; i < NUM_SENSORS; i++) {
		if (v4l2->video_ch[i].on) {
			ret = v4l2_device_streamon(i, v4l2->video_ch);
			if (ret < 0) {
				while(--i) {
					if (v4l2->video_ch[i].on)
						v4l2_device_streamoff(i, v4l2->video_ch);
				}
				return ret;
			}
		}
	}
	return 0;
}

static int media_device_stop(struct media_dev *media)
{
	struct v4l2_device *v4l2 = media->v4l2_dev;
	int i, ret;

	for (i = 0; i < NUM_SENSORS; i++) {
		if (v4l2->video_ch[i].on) {
			ret = v4l2_device_streamoff(i, v4l2->video_ch);
			if (ret < 0) {
				return ret;
			}
		}
	}
	return 0;
}

static int save_to_file(int ch, struct video_channel *video_ch)
{
	int buf_id = video_ch[ch].cur_buf_id;
	int fd = video_ch[ch].saved_file_fd;
	size_t wsize;
	int i;

	if (fd && !g_performance_test) {
		/* Save capture frame to file */
		for (i = 0; i < g_num_planes; i++) {
			wsize = write(fd, video_ch[ch].buffers[buf_id].planes[i].start,
				    video_ch[ch].buffers[buf_id].planes[i].plane_size);
			if (wsize < 1) {
				v4l2_err("No space left on device. Stopping after %d frames.\n",
						 video_ch[ch].frame_num);
				return -1;
			}
		}
	}
	v4l2_dbg("enter\n");

	return 0;
}

static int display_on_screen(int ch, struct media_dev *media)
{
	struct video_channel *video_ch = media->v4l2_dev->video_ch;
	struct drm_device *drm = media->drm_dev;
	struct drm_buffer *buf = &drm->buffers[drm->front_buf^1];
	int buf_id = video_ch[ch].cur_buf_id;
	static int enter_count = 0;
	int bufoffset;
	int out_h, out_w, stride_v, stride_d;
	int bytes_per_line;
	int cor_offset_x, cor_offset_y;
	int j, ret;

	if (g_cam_num == 1) {
		cor_offset_x = (buf->width >> 1) - (video_ch[ch].out_width >> 1);
		cor_offset_y = (buf->height >> 1) - (video_ch[ch].out_height >> 1);
		cor_offset_x = (cor_offset_x < 0) ? 0 : cor_offset_x;
		cor_offset_y = (cor_offset_y < 0) ? 0 : cor_offset_y;
	} else {
		cor_offset_x = 0;
		cor_offset_y = 0;
	}

	bytes_per_line = drm->bytes_per_pixel * buf->width;
	bufoffset = (video_ch[ch].x_offset + cor_offset_x)* drm->bytes_per_pixel +
				(video_ch[ch].y_offset + cor_offset_y)* bytes_per_line;

	/* V4L2 buffer width and height */
	out_h = video_ch[ch].out_height - 1;
	out_w = video_ch[ch].out_width;

	/* V4L2 buffer stride value */
	stride_v = out_w * drm->bytes_per_pixel;

	/* adjust wight and height of output when image is bigger than screen */
	out_w = (out_w > buf->width) ? buf->width : out_w;
	out_h = (out_h > buf->height - 1) ? buf->height - 1 : out_h;

	/* display screen stride value */
	stride_d = out_w * drm->bytes_per_pixel;

	for (j = 1; j < out_h; j++) {
		memcpy(buf->fb_base + bufoffset + j * bytes_per_line,
			   video_ch[ch].buffers[buf_id].planes[0].start + j * stride_v,
			   stride_d);
	}

	if (!(++enter_count % g_cam_num)) {
		ret = drmModeSetCrtc(drm->drm_fd, drm->crtc_id, buf->buf_id, 0, 0,
					&drm->conn_id, 1, &drm->mode);
		if (ret < 0) {
			v4l2_err("Set Crtc fail\n");
			return ret;
		}
		enter_count = 0;
		drm->front_buf ^= 1;

		/*v4l2_dbg("crtc_id=%d conn_id=%d buf_id=%d\n",*/
				 /*drm->crtc_id, drm->conn_id,*/
				 /*buf->buf_id);*/
	}
	return 0;
}

static void show_performance_test(struct media_dev *media)
{
	struct video_channel *video_ch = media->v4l2_dev->video_ch;
	struct timeval *tv1, *tv2;
	int i, time;

	if (!g_performance_test)
		return;

	for (i = 0; i < NUM_SENSORS; i++) {
		if (video_ch[i].on) {
			tv1 = &video_ch[i].tv1;
			tv2 = &video_ch[i].tv2;
			time = (tv2->tv_sec - tv1->tv_sec);

			v4l2_info("Channel[%d]: time=%d(s) frame=%d Performance=%d(fps)\n", i,
					  time,
					  video_ch[i].frame_num,
					  (video_ch[i].frame_num / time));
			/*video_ch[ch_id].frame_num = 0;*/
		}
	}
}

static int redraw(struct media_dev *media)
{
	struct video_channel *video_ch = media->v4l2_dev->video_ch;
	int i, ret;

	/* Record enter time for every channel */
	for (i = 0; i < NUM_SENSORS; i++) {
		if (video_ch[i].on) {
			gettimeofday(&video_ch[i].tv1, NULL);
		}
	}

	media->total_frames_cnt = 0;
	do {
		for (i = 0; i < NUM_SENSORS; i++) {
			if (video_ch[i].on) {
				/* DQBUF */
				ret = dqueue_buffer(i, &video_ch[i]);
				if (ret < 0)
					return -1;
				/* Save to file or playback */
				if (!g_saved_to_file) {
					/* Playback */
					ret = display_on_screen(i, media);
					if (ret < 0)
						return -1;
				} else {
					/* Save to file */
					ret = save_to_file(i, video_ch);
					if (ret < 0)
						return -1;
				}
			}
		}

		/* QBUF */
		for (i = 0; i < NUM_SENSORS; i++) {
			if (video_ch[i].on)
				queue_buffer(video_ch[i].cur_buf_id, &video_ch[i]);
		}
	} while(++media->total_frames_cnt < g_num_frames && !quitflag);

	/* Record exit time for every channel */
	for (i = 0; i < NUM_SENSORS; i++) {
		if (video_ch[i].on)
			gettimeofday(&video_ch[i].tv2, NULL);
	}
	return 0;
}

static void close_v4l2_device(struct v4l2_device *v4l2)
{
	struct video_channel *video_ch = v4l2->video_ch;
	int i;

	for (i = 0; i < NUM_SENSORS; i++) {
		if (((g_cam >> i) & 0x1) && (video_ch[i].v4l_fd > 0))
			close(video_ch[i].v4l_fd);
	}
}

static void media_device_close(struct media_dev *media)
{
	struct v4l2_device *v4l2 = media->v4l2_dev;

	if (!g_saved_to_file)
		close_drm_device(media->drm_dev->drm_fd);
	else
		close_save_file(v4l2->video_ch);

	close_v4l2_device(v4l2);
}

/*
 * Main function
 */
int main(int argc, const char *argv[])
{
	struct media_dev media;
	char *soc_list[] = { "i.MX8QM", "i.MX8QXP", " " };
	int ret;

	ret = soc_version_check(soc_list);
	if (ret == 0) {
		v4l2_err("not supported on current soc\n");
		return 0;
	}

	global_vars_init();

	pthread_t sigtid;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	pthread_create(&sigtid, NULL, (void *)&signal_thread, NULL);

	ret = parse_cmdline(argc, argv);
	if (ret < 0) {
		v4l2_err("%s failed to parse arguments\n", argv[0]);
		return ret;
	}

	ret = media_device_alloc(&media);
	if (ret < 0) {
		v4l2_err("No enough memory\n");
		return -ENOMEM;
	}

	ret = media_device_open(&media);
	if (ret < 0)
		goto free;

	ret = media_device_prepare(&media);
	if (ret < 0)
		goto close;

	ret = media_device_start(&media);
	if (ret < 0)
		goto cleanup;

	ret = redraw(&media);
	if (ret < 0)
		goto stop;

	show_performance_test(&media);

stop:
	media_device_stop(&media);

cleanup:
	media_device_cleanup(&media);
close:
	media_device_close(&media);
free:
	media_device_free(&media);
	if (ret == 0)
		v4l2_info("=*= success =*=\n");
	return ret;
}
