/*
 *  Copyright 2017 NXP
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
 * @file mx8_v4l2_cap_fb.c
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

sigset_t sigset;
int quitflag;

int start_streamon();
struct testbuffer {
	unsigned char *start;
	size_t offset;
	unsigned int length;
};

struct video_channel {
	/* v4l2 info */
	int init;
	int on;
	int index;		//video channel index
	int out_width;
	int out_height;
	int cap_fmt;
	int frame_size;
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

struct fb_disp_info {
	int x_res;
	int y_res;
	int bpp;
	int bytesperline;
	int fd_fb;
	int frame_num;
	int frame_size;
	int fb0_size;
	unsigned char *fb0;
	struct fb_var_screeninfo var;
};
struct fb_disp_info fb_disp;

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
	"0.rgb32",
	"1.rgb32",
	"2.rgb32",
	"3.rgb32",
	"4.rgb32",
	"5.rgb32",
	"6.rgb32",
	"7.rgb32",
};

void show_device_cap_list(void);
int prepare_capturing(int ch_id);

static void print_pixelformat(char *prefix, int val)
{
	v4l2_dbg("%s: %c%c%c%c\n", prefix ? prefix : "pixelformat",
	       val & 0xff,
	       (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) & 0xff);
}

void print_help(void)
{
	v4l2_info("CSI Video4Linux capture Device Test\n"
	       "Syntax: ./mx8_cap\n"
	       " -t <time>\n"
	       " -of save to file \n"
	       " -l <device support list>\n"
	       " -cam <device index> 0bxxxx,xxxx\n"
	       " -log <log_level>   output all information, log_level should be 6"
	       "example:\n"
	       "./mx8_cap -cam 1      capture data from video0 and playback\n"
	       "./mx8_cap -cam 3      capture data from video0/1 and playback\n"
	       "./mx8_cap -cam 7 -of  capture data from video0~2 and save to 0~2.rgb32\n"
	       "./mx8_cap -cam 255 -of  capture data from video0~7 and save to 0~7.rgb32\n");
}

int process_cmdline(int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-cam") == 0) {
			g_cam = atoi(argv[++i]);
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

	strcpy(video_ch[ch_id].v4l_dev_name, g_v4l_device[ch_id]);
	strcpy(video_ch[ch_id].save_file_name, g_saved_filename[ch_id]);
	v4l2_dbg("%s, %s init %d\n", __func__,
	       video_ch[ch_id].v4l_dev_name, ch_id);
	return 0;
}

static int mx8_capturing_prepare()
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

static int mx8_qbuf()
{
	unsigned int i;
	unsigned int j;
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers req;
	struct v4l2_plane planes = { 0 };
	for (j = 0; j < 8; j++) {
		if (video_ch[j].on) {
			int fd_v4l = video_ch[j].v4l_dev;
			int mem_type = video_ch[j].mem_type;
			for (i = 0; i < TEST_BUFFER_NUM; i++) {
				memset(&buf, 0, sizeof(buf));
				memset(&planes, 0, sizeof(planes));
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				buf.memory = mem_type;
				buf.m.planes = &planes;
				buf.index = i;
				buf.m.planes->length = video_ch[j].buffers[i].length;
				buf.length = 1;
				if (mem_type == V4L2_MEMORY_USERPTR)
					buf.m.planes->m.userptr =
						(unsigned long)video_ch[j].buffers[i].start;
				else
					buf.m.planes->m.mem_offset =
						video_ch[j].buffers[i].offset;

				if (ioctl(fd_v4l, VIDIOC_QBUF, &buf) < 0) {
					v4l2_err("VIDIOC_QBUF error\n");
					return -1;
				}
			}
		}
	}
}

static int free_buffer(int ch_id)
{
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers req;
	int fd_v4l = video_ch[ch_id].v4l_dev;
	int mem_type = video_ch[ch_id].mem_type;
	int i;

	for (i = 0; i < TEST_BUFFER_NUM; i++) {
		munmap(video_ch[ch_id].buffers[i].start,
				video_ch[ch_id].buffers[i].length);
	}

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = mem_type;
#if 1
	if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
		v4l2_err("free buffer failed (chan_ID:%d)\n", ch_id);
		return -1;
	}
#endif
	return 0;
}

int prepare_capturing(int ch_id)
{
	unsigned int i;
	struct v4l2_buffer buf;
	struct v4l2_requestbuffers req;
	struct v4l2_plane planes = { 0 };
	int fd_v4l = video_ch[ch_id].v4l_dev;
	int mem_type = video_ch[ch_id].mem_type;

	memset(&req, 0, sizeof(req));
	req.count = TEST_BUFFER_NUM;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = mem_type;

	if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
		v4l2_err("VIDIOC_REQBUFS failed\n");
		return -1;
	}

	if (req.count < TEST_BUFFER_NUM) {
		v4l2_err("Can't alloc 3 buffers\n");
		return -1;
	}

	if (mem_type == V4L2_MEMORY_MMAP) {
		for (i = 0; i < TEST_BUFFER_NUM; i++) {
			memset(&buf, 0, sizeof(buf));
			memset(&planes, 0, sizeof(planes));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			buf.memory = mem_type;
			buf.m.planes = &planes;
			buf.length = 1;	/* plane num */
			buf.index = i;

			if (ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
				v4l2_err("VIDIOC_QUERYBUF error\n");
				return -1;
			}

			video_ch[ch_id].buffers[i].length =
			    buf.m.planes->length;
			video_ch[ch_id].buffers[i].offset =
			    (size_t) buf.m.planes->m.mem_offset;
			video_ch[ch_id].buffers[i].start =
			    mmap(NULL,
				 video_ch[ch_id].buffers[i].length,
				 PROT_READ | PROT_WRITE, MAP_SHARED,
				 fd_v4l, video_ch[ch_id].buffers[i].offset);

			memset(video_ch[ch_id].buffers[i].start, 0xFF,
			       video_ch[ch_id].buffers[i].length);
			v4l2_dbg
				("buffer[%d] startAddr=0x%x, offset=0x%x, buf_size=%d\n",
				 i,
				 (unsigned int *)video_ch[ch_id].buffers[i].start,
				 video_ch[ch_id].buffers[i].offset,
				 video_ch[ch_id].buffers[i].length);
		}
	}
	return 0;
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
	int fd_v4l;
	int i;

	v4l2_dbg("Try to open device %s\n", video_ch[ch_id].v4l_dev_name);
	if ((fd_v4l = open(video_ch[ch_id].v4l_dev_name, O_RDWR, 0)) < 0) {
		v4l2_err("unable to open v4l2 %s for capture device.\n",
			   video_ch[ch_id].v4l_dev_name);
		return -1;
	}

	if (ioctl(fd_v4l, VIDIOC_QUERYCAP, &cap) == 0) {
		v4l2_dbg("cap=0x%x\n", cap.capabilities);
		if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
			v4l2_err("%s not support v4l2 capture device.\n",
				   video_ch[ch_id].v4l_dev_name);
			return -1;
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

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = video_ch[ch_id].cap_fmt;
	fmt.fmt.pix_mp.width = video_ch[ch_id].out_width;
	fmt.fmt.pix_mp.height = video_ch[ch_id].out_height;
	fmt.fmt.pix_mp.num_planes = 1;	/* RGB */
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

	v4l2_dbg("\t WxH@fps = %dx%d@%d", fmt.fmt.pix_mp.width,
		   fmt.fmt.pix_mp.height,
		   parm.parm.capture.timeperframe.denominator);
	v4l2_dbg("\t Image size = %d\n", fmt.fmt.pix_mp.plane_fmt[0].sizeimage);

	video_ch[ch_id].frame_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

	video_ch[ch_id].v4l_dev = fd_v4l;
	video_ch[ch_id].on = 1;

	v4l2_dbg("%s, Open v4l_dev=0x%x, channel=%d\n",
		   __func__, video_ch[ch_id].v4l_dev, ch_id);

	return 0;

fail:
	close(fd_v4l);
	return -1;
}

int config_video_channel(void)
{
	int x_out;
	int y_out;
	int x_offset[8];
	int y_offset[8];
	int cam_num, i;

	if (g_cam_num < 1) {
		v4l2_err("Error cam number %d\n", g_cam_num);
		return -1;
	}
	if (g_cam_num == 3 || g_cam_num == 5 || g_cam_num == 7) {
		v4l2_err("Not support %d cam output\n", g_cam_num);
		return -1;
	}

	v4l2_info("xres=%d, y_res=%d\n", fb_disp.x_res, fb_disp.y_res);
	x_offset[0] = 0;
	y_offset[0] = 0;
	if (g_cam_num == 2) {
		x_out = fb_disp.x_res / 2;
		y_out = fb_disp.y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
	} else if (g_cam_num == 4) {
		x_out = fb_disp.x_res / 2;
		y_out = fb_disp.y_res / 2;
		x_offset[1] = x_out;
		y_offset[1] = 0;
		x_offset[2] = 0;
		y_offset[2] = y_out;
		x_offset[3] = x_out;
		y_offset[3] = y_out;
	} else if (g_cam_num == 6) {
		x_out = fb_disp.x_res / 3;
		y_out = fb_disp.y_res / 2;
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
	} else if (g_cam_num == 8) {
		x_out = fb_disp.x_res / 4;
		y_out = fb_disp.y_res / 2;
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

int fb_display_setup(void)
{
	struct fb_var_screeninfo *var = &fb_disp.var;
	char fb_device[100] = "/dev/fb0";
	int fd_fb = 0;
	int fb0_size;
	int frame_size;

	if ((fd_fb = open(fb_device, O_RDWR)) < 0) {
		v4l2_err("Unable to open frame buffer\n");
		goto FAIL;
	}

	if (ioctl(fd_fb, FBIOGET_VSCREENINFO, var) < 0) {
		v4l2_err("FBIOPUT_VSCREENINFO failed\n");
		goto FAIL;
	}

	var->xres_virtual = var->xres;
	var->yres_virtual = 3 * var->yres;

	if (ioctl(fd_fb, FBIOPUT_VSCREENINFO, var) < 0) {
		v4l2_err("FBIOPUT_VSCREENINFO failed\n");
		goto FAIL;
	}

	/* Map the device to memory */
	fb0_size = var->xres * var->yres_virtual * var->bits_per_pixel / 8;
	frame_size = var->xres * var->yres * var->bits_per_pixel / 8;

	v4l2_info("fb0_size = %d\n", fb0_size);
	fb_disp.fb0 = (unsigned char *)mmap(0, fb0_size,
					    PROT_READ | PROT_WRITE,
					    MAP_SHARED, fd_fb, 0);
	if (fb_disp.fb0 == NULL) {
		v4l2_err
		    ("Error: failed to map framebuffer device 0 to memory.\n");
		goto FAIL;
	}

	var->yoffset = var->yres;
	fb_disp.fd_fb = fd_fb;
	fb_disp.fb0_size = fb0_size;
	fb_disp.frame_size = frame_size;
	fb_disp.x_res = var->xres;
	fb_disp.y_res = var->yres;
	fb_disp.bpp = var->bits_per_pixel;
	fb_disp.bytesperline = var->xres * var->bits_per_pixel / 8;

	v4l2_info("fb frame size, WxH=(%d, %d)\n", var->xres, var->yres);
	close(fd_fb);
	return 0;
FAIL:
	close(fd_fb);
	return -1;
}

int set_up_frame(int ch_id)
{
	int out_h, out_w;
	int stride;
	int bufoffset;
	int j;
	int buf_id = video_ch[ch_id].cur_buf_id;
	struct fb_var_screeninfo *var = &fb_disp.var;
	int frame_size = fb_disp.frame_size;

	bufoffset = video_ch[ch_id].x_offset * fb_disp.bpp / 8 +
	    video_ch[ch_id].y_offset * fb_disp.bytesperline;

	out_h = video_ch[ch_id].out_height;
	out_w = video_ch[ch_id].out_width;
	stride = out_w * 4;

	if (var->yoffset == var->yres) {
		/* fb buffer offset 1 frame */
		bufoffset += frame_size;
		for (j = 0; j < out_h; j++)
			memcpy(fb_disp.fb0 + bufoffset +
			       j * fb_disp.bytesperline,
			       video_ch[ch_id].buffers[buf_id].start +
			       j * stride, stride);
	} else {
		/* fb buffer offset 2 frame  */
		bufoffset += fb_disp.frame_size * 2;
		for (j = 0; j < out_h; j++)
			memcpy(fb_disp.fb0 + bufoffset +
			       j * fb_disp.bytesperline,
			       video_ch[ch_id].buffers[buf_id].start +
			       j * stride, stride);
	}
	return 0;
}

int get_video_channel_buffer(int ch_id)
{
	int fd_v4l = video_ch[ch_id].v4l_dev;
	struct v4l2_buffer buf;
	struct v4l2_plane planes = { 0 };

	memset(&buf, 0, sizeof(buf));
	memset(&planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = video_ch[ch_id].mem_type;
	buf.m.planes = &planes;
	buf.length = 1;
	if (ioctl(fd_v4l, VIDIOC_DQBUF, &buf) < 0) {
		v4l2_err("VIDIOC_DQBUF failed.\n");
		return -1;
	}

	video_ch[ch_id].frame_num++;
	video_ch[ch_id].cur_buf_id = buf.index;
	return 0;

}

int put_video_channel_buffer(int ch_id)
{
	int fd_v4l = video_ch[ch_id].v4l_dev;
	struct v4l2_buffer buf;
	struct v4l2_plane planes = { 0 };

	int buf_id = video_ch[ch_id].cur_buf_id;

	memset(&buf, 0, sizeof(buf));
	memset(&planes, 0, sizeof(planes));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = g_mem_type;
	buf.m.planes = &planes;
	buf.index = buf_id;
	buf.m.planes->length = video_ch[ch_id].buffers[buf_id].length;
	buf.length = 1;

	if (ioctl(fd_v4l, VIDIOC_QBUF, &buf) < 0) {
		v4l2_err("VIDIOC_QBUF failed, video=%d\n", ch_id);
		return -1;
	}
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

	if (video_ch[ch_id].pfile) {
		/* Save capture frame to file */
		wsize = fwrite(video_ch[ch_id].buffers[buf_id].start,
			       video_ch[ch_id].frame_size, 1,
			       video_ch[ch_id].pfile);
		if (wsize < 1) {
			v4l2_err
			    ("No space left on device. Stopping after %d frames.\n",
			     video_ch[ch_id].frame_num);
			return -1;
		}
	}
	return 0;
}

int close_save_file(void)
{
	int i;

	for (i = 0; i < 8; i++)
		if (video_ch[i].on && !video_ch[i].pfile)
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

int fb_pan_display(void)
{
	struct fb_var_screeninfo *var = &fb_disp.var;
	int fd_fb = fb_disp.fd_fb;

	if (var->yoffset == var->yres) {
		/* fb buffer offset 1 frame */
		if (ioctl(fd_fb, FBIOPAN_DISPLAY, var) < 0) {
			v4l2_err("FBIOPAN_DISPLAY1 failed\n");
		}
		var->yoffset += var->yres;
		fb_disp.frame_num++;
	} else {
		/* fb buffer offset 2 frame  */
		if (ioctl(fd_fb, FBIOPAN_DISPLAY, var) < 0) {
			v4l2_err("FBIOPAN_DISPLAY2 failed\n");
		}
		var->yoffset -= var->yres;
		fb_disp.frame_num++;
	}
	return 0;
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
		fmt.fmt.pix_mp.num_planes = 1;	/* RGB */
		if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
			v4l2_err("set format failed\n");
			return -1;
		}
	}
	return 0;
}

int v4l_capture_test(void)
{
	struct timeval tv1, tv2;
	int i;
	int ret;
	struct fb_var_screeninfo *var = &fb_disp.var;
	static int first_time_enter = 0;

loop:
	v4l2_dbg(" ===== first_time_enter:%d ===== \n", first_time_enter);
	if (first_time_enter++ > 0) {
		for (i = 0; i < g_cam_max; i++) {
			if (set_vdev_parm(i) < 0)
				return -1;
		}
		first_time_enter = 1;
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
		for (i = 0; i < 8; i++)
			if (video_ch[i].on) {
				ret = get_video_channel_buffer(i);
				if (ret < 0)
					return -1;
				if (!g_saved_to_file) {
					/* Copy video buffer to fb buffer */
					ret = set_up_frame(i);
					if (ret < 0)
						return -1;
				} else {
					/* Performance test, skip write file operation */
					if (!g_performance_test)
						ret = save_to_file(i);
						if (ret < 0)
							return -1;
				}
			}

		if (!g_saved_to_file)
			fb_pan_display();

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

	/* Make sure pan display offset is zero before capture is stopped */
	if (!g_saved_to_file) {
		if (var->yoffset) {
			var->yoffset = 0;
			if (ioctl(fb_disp.fd_fb, FBIOPAN_DISPLAY, var) < 0) {
				v4l2_err("FBIOPAN_DISPLAY failed\n");
				return -1;
			}
		}
	}

	/* stop channels / stream off */
	for (i = 0; i < 8; i++) {
		if (video_ch[i].on)
			if (stop_capturing(i) < 0) {
				v4l2_err("stop_capturing failed, device %d\n", i);
				return -1;
			}
	}

	for (i = 0; i < 8; i++) {
		if (video_ch[i].on)
			if (free_buffer(i) < 0) {
				v4l2_err("stop_capturing failed, device %d\n", i);
				return -1;
			}
	}

	if (g_loop > 0 && !quitflag) {
		v4l2_info(" ======= loop %d done! ========\n", g_loop);
		g_loop--;
		goto loop;
	}

	return 0;
}

int start_streamon()
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

}


int main(int argc, char **argv)
{
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

	memset(video_ch, 0, sizeof(struct video_channel) * 8);

	/* check cam */
	if ((g_cam & 0xFF) == 0)
		return -1;

	/* init all video channel to default value */
	g_cam_num = 0;
	for (i = 0; i < g_cam_max; i++) {
		if (g_cam >> i & 0x1) {
			g_cam_num++;
			init_video_channel(i);
		}
	}

	if (!g_saved_to_file) {
		/* get fb info */
		ret = fb_display_setup();
		if (ret < 0) {
			v4l2_err("fb_display_setup failed\n");
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
		ret = config_video_channel();
		if (ret < 0) {
			v4l2_err("config video failed\n");
			return -1;
		}
	} else {
		/* default video channel config, no resize */
	}

	/* open video device and enable channel */
	for (i = 0; i < g_cam_max; i++) {
		if (g_cam >> i & 0x1) {
			ret = v4l_capture_setup(i);
			if (ret < 0)
				goto FAIL0;
		}
	}

	if (v4l_capture_test() < 0)
		goto FAIL0;

	v4l2_info("success!\n");

	if (!g_saved_filename)
		if (fb_disp.fd_fb > 0)
			close(fb_disp.fd_fb);
	else
		close_save_file();
	close_vdev_file();

	return 0;

FAIL0:
	if (!g_saved_filename)
		if (fb_disp.fd_fb > 0)
			close(fb_disp.fd_fb);
	else
		close_save_file();
	close_vdev_file();
	return -1;
}
