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
 * @file mx8_v4l2_m2m_test.c
 *
 * @brief This application is used for test IMX8QXP and IMX8QM  memory
 *        to memory driver
 *
 */

/* Standard Include Files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "../../include/soc_check.h"

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

#define TEST_BUFFER_NUM_IN		1
#define TEST_BUFFER_NUM_OUT		3

#define TEST_WIDTH		640
#define TEST_HEIGHT		480

struct plane_buffer {
	void *start;
	__u32 plane_size;
	__u32 length;
	size_t offset;
};

struct mxc_buffer {
	void *start;
	__u32 length;
	size_t offset;

	struct plane_buffer planes[3];
};

struct rect {
	int width;
	int height;
};

struct mxc_m2m_device {
	int fd;
	FILE *in_file;
	FILE *out_file;

	struct rect src;
	struct rect dst;

	int in_num_planes;
	int o_num_planes;

	int in_frame_num;
	int out_frame_num;

	int in_cur_buf_id;
	int out_cur_buf_id;

	int frames;

	struct mxc_buffer in_buffers[TEST_BUFFER_NUM_IN];
	struct mxc_buffer out_buffers[TEST_BUFFER_NUM_OUT];
};

static sigset_t sigset_v;
static char in_file_name[32] = {"0.rgb32"};
static char out_file_name[32] = {"out.dat"};
static char dev_name[32] = {"/dev/video0"};
static char in_format[32] = {"RGB32"};
static char out_format[32] = {"YUYV"};
static int in_width = TEST_WIDTH;
static int in_height = TEST_HEIGHT;
static int out_width = TEST_WIDTH;
static int out_height = TEST_HEIGHT;
static int log_level = 6;
static int show_device_cap = 0;
static bool quitflag;

static bool g_cap_hfilp;
static bool g_cap_vfilp;
static bool g_performance_test = false;
static int32_t g_cap_alpha;

/*
 *
 */
static int signal_thread(void *arg)
{
	int sig;

	pthread_sigmask(SIG_BLOCK, &sigset_v, NULL);

	while (1) {
		sigwait(&sigset_v, &sig);
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

static void fill_param(struct rect *r, int width, int height)
{
	r->width = width;
	r->height = height;
}

static void print_usage(char *name)
{
	v4l2_info("Usage: %s -d </dev/videoX> "
		   " -i <input_file> -ifmt <RGB> -iw <width> -ih <height> "
		   " -o <out_file> -ofmt <NV12> -ow <width> -oh <height>\n" 
		   " -d </dev/videoX>: video device ,default=>\"/dev/video0\".\n"
		   " -i <file_name>: file_name is input file name. default=>\"0.rgb32\"\n"
		   " -ifmt <fmt>   : fmt is intput file format. You can run \"%s -l\" to acquire supported input format\n"
		   " -iw <input_w> : input_w is input width.\n"
		   " -ih <input_h> : input_h is input height.\n"
		   " -o <file_name>: file_name is input file name. default=>\"out.dat\"\n"
		   " -ofmt <fmt>   : fmt is intput file format.You can run \"%s -l\" to acquire supported out format\n"
		   " -ow <output_w>: output_w is output width. It should be equal to or less than input width\n"
		   " -oh <output_h>: output_h is output height. It should be equal to or less than input height\n"
		   " -hflip <num> enable horizontal flip, num: 0->disable or 1->enable\n"
		   " -vflip <num> enable vertical flip, num: 0->disable or 1->enable\n"
		   " -alpha <num> enable and set global alpha for camera, num equal to 0~255\n"
		   "examples:\n"
		   "\t %s\n"
		   "\t %s -i 0.rgb32 -iw 1280 -ih 800 -ifmt \"XR24\" "
		   "-o out.dat -ow 1280 -oh 800 -ofmt \"NV12\"\n",
		   name, name, name, name, name);
}

static __u32 to_fourcc(char fmt[])
{
	__u32 fourcc;

	if (!strcmp(fmt, "BX24"))
		fourcc = V4L2_PIX_FMT_XRGB32;
	else if (!strcmp(fmt, "BA24"))
		fourcc = V4L2_PIX_FMT_ARGB32;
	else if (!strcmp(fmt, "BGR3"))
		fourcc = V4L2_PIX_FMT_BGR24;
	else if (!strcmp(fmt, "RGB3"))
		fourcc = V4L2_PIX_FMT_RGB24;
	else if (!strcmp(fmt, "RGBP"))
		fourcc = V4L2_PIX_FMT_RGB565;
	else if (!strcmp(fmt, "YUV4"))
		fourcc = V4L2_PIX_FMT_YUV32;
	else if (!strcmp(fmt, "YUYV"))
		fourcc = V4L2_PIX_FMT_YUYV;
	else if (!strcmp(fmt, "NV12"))
		fourcc = V4L2_PIX_FMT_NV12;
	else if (!strcmp(fmt, "YM24"))
		fourcc = V4L2_PIX_FMT_YUV444M;
	else if (!strcmp(fmt, "XR24"))
		fourcc = V4L2_PIX_FMT_XBGR32;
	else if (!strcmp(fmt, "AR24"))
		fourcc = V4L2_PIX_FMT_ABGR32;
	else {
		v4l2_err("Not support format, set default to XR24\n");
		fourcc = V4L2_PIX_FMT_XBGR32;
	}
	return fourcc;
}

static void show_device_cap_list(int fd_v4l)
{
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_frmivalenum frmival;
	struct v4l2_frmsizeenum frmsize;

	v4l2_info("%s enter\n", __func__);

	/* Show capture device */
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	while (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
		v4l2_info("support output pixelformat %.4s\n",
					(char *)&fmtdesc.pixelformat);
					frmsize.pixel_format = fmtdesc.pixelformat;
					frmsize.index = 0;
		while (ioctl (fd_v4l, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
			frmival.index = 0;
			frmival.pixel_format = fmtdesc.pixelformat;
			frmival.width = frmsize.discrete.width;
			frmival.height = frmsize.discrete.height;
			v4l2_err("\t==== w/h=(%d,%d)\n", frmsize.discrete.width, frmsize.discrete.height);
			while (ioctl(fd_v4l, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0) {
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

	/* Show out device */
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	while (ioctl(fd_v4l, VIDIOC_ENUM_FMT, &fmtdesc) >= 0) {
		v4l2_info("support input pixelformat %.4s\n",
					(char *)&fmtdesc.pixelformat);
					frmsize.pixel_format = fmtdesc.pixelformat;
					frmsize.index = 0;
		while (ioctl (fd_v4l, VIDIOC_ENUM_FRAMESIZES, &frmsize) >= 0) {
						frmival.index = 0;
						frmival.pixel_format = fmtdesc.pixelformat;
						frmival.width = frmsize.discrete.width;
						frmival.height = frmsize.discrete.height;
						v4l2_err("\t==== w/h=(%d,%d)\n", frmsize.discrete.width, frmsize.discrete.height);
			while (ioctl(fd_v4l, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0) {
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
}

static int parse_cmdline(int argc, char *argv[])
{
	int i;

	/* Parse command line */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-d") == 0) {
			strcpy(dev_name, argv[++i]);
		} else if (strcmp(argv[i], "-i") == 0) {
			strcpy(in_file_name, argv[++i]);
		} else if (strcmp(argv[i], "-o") == 0) {
			strcpy(out_file_name, argv[++i]);
		} else if (strcmp(argv[i], "-ifmt") == 0) {
			strcpy(in_format, argv[++i]);
		} else if (strcmp(argv[i], "-ofmt") == 0) {
			strcpy(out_format, argv[++i]);
		} else if (strcmp(argv[i], "-iw") == 0) {
			in_width = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-ih") == 0) {
			in_height = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-ow") == 0) {
			out_width = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-oh") == 0) {
			out_height = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-log") == 0) {
			log_level = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-l") == 0) {
			show_device_cap = 1;
		} else if (strcmp(argv[i], "-hflip") == 0) {
			g_cap_hfilp = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-vflip") == 0) {
			g_cap_vfilp = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-alpha") == 0) {
			g_cap_alpha = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-p") == 0) {
			g_performance_test = true;
		} else if (strcmp(argv[i], "-h") == 0) {
			print_usage(argv[0]);
			return -1;
		} else {
			print_usage(argv[0]);
			return -1;
		}
	}
	return 0;
}

static int get_bpp(char format[])
{
	__u32 bpp;
	__u32 fourcc = to_fourcc(format);

	switch(fourcc) {
	case V4L2_PIX_FMT_XRGB32:
	case V4L2_PIX_FMT_XBGR32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_ABGR32:
	case V4L2_PIX_FMT_YUV32:
		bpp = 4;
		break;
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_YUV444M:
		bpp = 3;
		break;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUYV:
		bpp = 2;
		break;
	default:
		v4l2_err("Not support format\n");
	}
	return bpp;
}

static int check_input_parameters(struct mxc_m2m_device *m2m_dev)
{
	long filesize;

	if (m2m_dev->dst.width > m2m_dev->src.width ||
		m2m_dev->dst.height > m2m_dev->src.height) {
		v4l2_err("out w/h need equal to or less than input w/h\n");
		return -1;
	}

	fseek(m2m_dev->in_file, 0, SEEK_END);
	filesize = ftell(m2m_dev->in_file);
	fseek(m2m_dev->in_file, 0, SEEK_SET);

	if (filesize < m2m_dev->src.width * m2m_dev->src.height * get_bpp(in_format)) {
		v4l2_err("input width and height is wrong\n");
		return -1;
	}

	return 0;
}

static int check_device_cap(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_capability capabilities;
	int fd = m2m_dev->fd;

	memset(&capabilities, 0, sizeof(capabilities));
	if (ioctl(fd, VIDIOC_QUERYCAP, &capabilities) < 0) {
		v4l2_err("VIDIOC_QUERYCAP fail\n");
		return -1;
	}

	if (!(capabilities.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
		v4l2_err("The device does not handle memory to memory video capture.\n");
		return -1;
	}

	return 0;
}

static void calculate_frames_of_infile(FILE *f, struct mxc_m2m_device *m2m_dev)
{
	int frames;
	long filesize;

	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	fseek(f, 0, SEEK_SET);

	frames = filesize / (m2m_dev->src.width * m2m_dev->src.height * get_bpp(in_format));

	m2m_dev->frames = frames;
	v4l2_info("Input file size is = %lu, frames is = %d\n", filesize, frames);
}

static int get_fps(struct timeval *start, struct timeval *end, int frame)
{
	int fps;
	int time;

	time = (int)((end->tv_sec - start->tv_sec) * 1000000 + (end->tv_usec - start->tv_usec));
	fps = (frame * 1000000) / time;

	return fps;
}

static int mxc_m2m_open(struct mxc_m2m_device *m2m_dev)
{
	int fd;
	FILE *in, *out;

	fd = open(dev_name, O_RDWR, 0);
	if (fd < 0) {
		v4l2_err("Open %s fail\n", dev_name);
		return -1;
	}
	v4l2_info("Open \"%s\" success\n", dev_name);

	if (show_device_cap) {
		m2m_dev->fd = fd;
		return 0;
	}

	in = fopen(in_file_name, "rb");
	if (in == NULL) {
		v4l2_err("Open %s fail\n", in_file_name);
		close(fd);
		return -1;
	}
	v4l2_info("Open \"%s\" success\n", in_file_name);
	calculate_frames_of_infile(in, m2m_dev);

	out = fopen(out_file_name, "wb");
	if (out == NULL) {
		v4l2_err("Open \"%s\" fail\n", out_file_name);
		fclose(in);
		close(fd);
		return -1;
	}
	v4l2_info("Open \"%s\" success\n", out_file_name);

	m2m_dev->fd = fd;
	m2m_dev->in_file = in;
	m2m_dev->out_file = out;
	return 0;
}

static void mxc_m2m_close(struct mxc_m2m_device *m2m_dev)
{
	close(m2m_dev->fd);
	if (show_device_cap)
		return;
	fclose(m2m_dev->in_file);
	fclose(m2m_dev->out_file);
}

static int fill_in_buffer(int buf_id, struct mxc_m2m_device *m2m_dev)
{
	size_t rsize;
	int j;

	for (j = 0; j < m2m_dev->in_num_planes; j++) {
		rsize = fread(m2m_dev->in_buffers[buf_id].planes[j].start,
					m2m_dev->in_buffers[buf_id].planes[j].length,
					1, m2m_dev->in_file);
		if (rsize < 1) {
			v4l2_err("No more data read from input file\n");
			return -1;
		}
	}
	return 0;
}

static int save_to_file(struct mxc_m2m_device *m2m_dev)
{
	size_t wsize;
	int buf_id = m2m_dev->out_cur_buf_id;
	int j;

	for (j = 0; j < m2m_dev->o_num_planes; j++) {
		wsize = fwrite(m2m_dev->out_buffers[buf_id].planes[j].start,
					   m2m_dev->out_buffers[buf_id].planes[j].length,
					   1, m2m_dev->out_file);
		if (wsize < 1) {
			v4l2_err("No more device space for output file\n");
			return -1;
		}
	}
	return 0;
}

static int mxc_m2m_prepare(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_format in_fmt;
	struct v4l2_format out_fmt;
	struct v4l2_control ctrl;
	int i, fd = m2m_dev->fd;
	int ret;

	memset(&in_fmt, 0, sizeof(in_fmt));
	in_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	in_fmt.fmt.pix_mp.pixelformat = to_fourcc(in_format);
	in_fmt.fmt.pix_mp.width = m2m_dev->src.width;
	in_fmt.fmt.pix_mp.height = m2m_dev->src.height;
	ret = ioctl(fd, VIDIOC_S_FMT, &in_fmt);
	if (ret < 0) {
		v4l2_err("in VIDIOC_S_FMT fail\n");
		return ret;
	}

	memset(&out_fmt, 0, sizeof(out_fmt));
	out_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	out_fmt.fmt.pix_mp.pixelformat = to_fourcc(out_format);
	out_fmt.fmt.pix_mp.width = m2m_dev->dst.width;
	out_fmt.fmt.pix_mp.height = m2m_dev->dst.height;
	ret = ioctl(fd, VIDIOC_S_FMT, &out_fmt);
	if (ret < 0) {
		v4l2_err("out VIDIOC_S_FMT fail\n");
		return ret;
	}

	/* IN G_FMT */
	memset(&in_fmt, 0, sizeof(in_fmt));
	in_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(fd, VIDIOC_G_FMT, &in_fmt);
	if (ret < 0) {
		v4l2_err("in VIDIOC_G_FMT fail\n");
		return ret;
	}
	v4l2_info("in: w/h=(%d,%d) pixelformat=%.4s num_planes=%d\n",
			  in_fmt.fmt.pix_mp.width,
			  in_fmt.fmt.pix_mp.height,
			  (char *)&in_fmt.fmt.pix_mp.pixelformat,
			  in_fmt.fmt.pix_mp.num_planes);
	for (i = 0; i < in_fmt.fmt.pix_mp.num_planes; i++) {
		v4l2_info("\t plane[%d]: bytesperline=%d sizeimage=%d\n", i,
			  in_fmt.fmt.pix_mp.plane_fmt[i].bytesperline,
			  in_fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
	}
	m2m_dev->in_num_planes = in_fmt.fmt.pix_mp.num_planes;

	memset(&out_fmt, 0, sizeof(out_fmt));
	out_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_G_FMT, &out_fmt);
	if (ret < 0) {
		v4l2_err("out VIDIOC_G_FMT fail\n");
		return ret;
	}
	v4l2_info("out: w/h=(%d,%d) pixelformat=%.4s num_planes=%d\n",
			  out_fmt.fmt.pix_mp.width,
			  out_fmt.fmt.pix_mp.height,
			  (char *)&out_fmt.fmt.pix_mp.pixelformat,
			  out_fmt.fmt.pix_mp.num_planes);
	for (i = 0; i < out_fmt.fmt.pix_mp.num_planes; i++) {
		v4l2_info("\t plane[%d]: bytesperline=%d sizeimage=%d\n", i,
				  out_fmt.fmt.pix_mp.plane_fmt[i].bytesperline,
				  out_fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
	}
	m2m_dev->o_num_planes = out_fmt.fmt.pix_mp.num_planes;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CID_HFLIP;
	ctrl.value = (g_cap_hfilp > 0) ? 1 : 0;
	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		v4l2_err("VIDIOC_S_CTRL set hflip failed\n");
		return ret;
	}

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CID_VFLIP;
	ctrl.value = (g_cap_vfilp > 0) ? 1 : 0;
	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		v4l2_err("VIDIOC_S_CTRL set vflip failed\n");
		return ret;
	}

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = V4L2_CID_ALPHA_COMPONENT;
	ctrl.value = g_cap_alpha;
	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		v4l2_err("VIDIOC_S_CTRL set alpha failed\n");
		return ret;
	}

	return 0;
}

static int query_in_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer bufferin;
	struct v4l2_plane *planes;
	int i, fd = m2m_dev->fd;

	planes = malloc(m2m_dev->in_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d planes fail\n", m2m_dev->in_num_planes);
		return -1;
	}

	/* Query Buffer for IN */
	for (i = 0; i < TEST_BUFFER_NUM_IN; i++) {
		memset(&bufferin, 0, sizeof(bufferin));
		memset(planes, 0, m2m_dev->in_num_planes * sizeof(*planes));
		bufferin.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		bufferin.memory = V4L2_MEMORY_MMAP;
		bufferin.m.planes = planes;
		bufferin.length = m2m_dev->in_num_planes;
		bufferin.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &bufferin) < 0) {
			v4l2_err("query buffer[%d] info fail\n", i);
			free(planes);
			return -1;
		}

		for (int j = 0; j < m2m_dev->in_num_planes; j++) {
			m2m_dev->in_buffers[i].planes[j].length = bufferin.m.planes[j].length;
			m2m_dev->in_buffers[i].planes[j].offset =
							(size_t)bufferin.m.planes[j].m.mem_offset;
			m2m_dev->in_buffers[i].planes[j].start = mmap(NULL,
						bufferin.m.planes[j].length,
						PROT_READ | PROT_WRITE, MAP_SHARED,
						fd, (size_t)bufferin.m.planes[j].m.mem_offset);

			v4l2_dbg("in buffer[%d]->planes[%d]:"
					 "startAddr=0x%p, offset=0x%x, buf_size=%d\n",
					 i, j,
					 (unsigned int *)m2m_dev->in_buffers[i].planes[j].start,
					 (unsigned int)m2m_dev->in_buffers[i].planes[j].offset,
					 m2m_dev->in_buffers[i].planes[j].length);
		}
	}

	free(planes);
	return 0;
}

static void unmap_in_buffer(struct mxc_m2m_device *m2m_dev)
{
	int i, j;

	for (i = 0; i < TEST_BUFFER_NUM_IN; i++) {
		for (j = 0; j < m2m_dev->in_num_planes; j++) {
			if (m2m_dev->in_buffers[i].planes[j].start != MAP_FAILED &&
				m2m_dev->in_buffers[i].planes[j].start > 0)
				munmap(m2m_dev->in_buffers[i].planes[j].start,
						m2m_dev->in_buffers[i].planes[j].length);
		}
	}
}

static void unmap_out_buffer(struct mxc_m2m_device *m2m_dev)
{
	int i, j;

	for (i = 0; i < TEST_BUFFER_NUM_OUT; i++) {
		for (j = 0; j < m2m_dev->o_num_planes; j++) {
			if (m2m_dev->out_buffers[i].planes[j].start != MAP_FAILED &&
				m2m_dev->out_buffers[i].planes[j].start > 0)
				munmap(m2m_dev->out_buffers[i].planes[j].start,
						m2m_dev->out_buffers[i].planes[j].length);
		}
	}
}

static int query_out_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer bufferout;
	struct v4l2_plane *planes;
	int i, fd = m2m_dev->fd;

	planes = malloc(m2m_dev->o_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d planes fail\n", m2m_dev->o_num_planes);
		return -1;
	}

	/* Query Buffer for IN */
	for (i = 0; i < TEST_BUFFER_NUM_OUT; i++) {
		memset(&bufferout, 0, sizeof(bufferout));
		memset(planes, 0, m2m_dev->o_num_planes * sizeof(*planes));
		bufferout.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		bufferout.memory = V4L2_MEMORY_MMAP;
		bufferout.m.planes = planes;
		bufferout.length = m2m_dev->o_num_planes;
		bufferout.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &bufferout) < 0) {
			v4l2_err("query buffer[%d] info fail\n", i);
			free(planes);
			return -1;
		}

		for (int j = 0; j < m2m_dev->o_num_planes; j++) {
			m2m_dev->out_buffers[i].planes[j].length = bufferout.m.planes[j].length;
			m2m_dev->out_buffers[i].planes[j].offset =
							(size_t)bufferout.m.planes[j].m.mem_offset;
			m2m_dev->out_buffers[i].planes[j].start = mmap(NULL,
						bufferout.m.planes[j].length,
						PROT_READ | PROT_WRITE, MAP_SHARED,
						fd, (size_t)bufferout.m.planes[j].m.mem_offset);

			v4l2_dbg("out buffer[%d]->planes[%d]:"
					 "startAddr=0x%p, offset=0x%x, buf_size=%d\n",
					 i, j,
					 (unsigned int *)m2m_dev->out_buffers[i].planes[j].start,
					 (unsigned int)m2m_dev->out_buffers[i].planes[j].offset,
					 m2m_dev->out_buffers[i].planes[j].length);
		}
	}

	free(planes);
	return 0;
}

static int request_in_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_requestbuffers bufrequestin;
	int fd = m2m_dev->fd;

	memset(&bufrequestin, 0, sizeof(bufrequestin));
	bufrequestin.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	bufrequestin.memory = V4L2_MEMORY_MMAP;
	bufrequestin.count = TEST_BUFFER_NUM_IN;
	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestin) < 0) {
		v4l2_err("VIDIOC_REQBUFS IN fail\n");
		return -1;
	}

	return 0;
}

static void free_in_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_requestbuffers req;
	int ret, fd = m2m_dev->fd;

	/* Free src buffer */
	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("IN: free v4l2 buffer fail\n");
		return;
	}
}

static int request_out_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_requestbuffers bufrequestout;
	int fd = m2m_dev->fd;

	memset(&bufrequestout, 0, sizeof(bufrequestout));
	bufrequestout.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	bufrequestout.memory = V4L2_MEMORY_MMAP;
	bufrequestout.count = TEST_BUFFER_NUM_OUT;
	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestout) < 0) {
		v4l2_err("VIDIOC_REQBUFS OUT fail\n");
		return -1;
	}

	return 0;
}

static void free_out_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_requestbuffers req;
	int fd = m2m_dev->fd;
	int ret;

	/* Free out buffer */
	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("OUT: free v4l2 buffer fail\n");
		return;
	}
}

static int mxc_m2m_request_buffer(struct mxc_m2m_device *m2m_dev)
{
	int ret;

	ret = request_in_buffer(m2m_dev);
	if (ret < 0)
		return ret;

	ret = request_out_buffer(m2m_dev);
	if (ret < 0)
		goto src;

	ret = query_in_buffer(m2m_dev);
	if (ret < 0)
		goto dst;

	ret = query_out_buffer(m2m_dev);
	if (ret < 0) {
		unmap_in_buffer(m2m_dev);
		goto dst;
	}

	return 0;

dst:
	free_out_buffer(m2m_dev);
src:
	free_in_buffer(m2m_dev);
	return ret;
}

static int mxc_m2m_queue_in_buffer(int buf_id, struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = m2m_dev->fd;
	int j;

	planes = malloc(m2m_dev->in_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", m2m_dev->in_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = m2m_dev->in_num_planes;
	buf.index = buf_id;

	for (j = 0; j < m2m_dev->in_num_planes; j++) {
		buf.m.planes[j].length = m2m_dev->in_buffers[buf_id].planes[j].length;
		buf.m.planes[j].m.mem_offset = m2m_dev->in_buffers[buf_id].planes[j].offset;
		buf.m.planes[j].bytesused = m2m_dev->in_buffers[buf_id].planes[j].length;
	}

	if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
		v4l2_err("buffer[%d] VIDIOC_QBUF IN fail\n", buf_id);
		return -1;
	}

	free(planes);
	return 0;
}

static int mxc_m2m_queue_out_buffer(int buf_id, struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = m2m_dev->fd;
	int j;

	planes = malloc(m2m_dev->o_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", m2m_dev->o_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = m2m_dev->o_num_planes;
	buf.index = buf_id;

	for (j = 0; j < m2m_dev->o_num_planes; j++) {
		buf.m.planes[j].length = m2m_dev->out_buffers[buf_id].planes[j].length;
		buf.m.planes[j].m.mem_offset = m2m_dev->out_buffers[buf_id].planes[j].offset;
		buf.m.planes[j].bytesused = m2m_dev->out_buffers[buf_id].planes[j].length;
	}

	if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
		v4l2_err("buffer[%d] VIDIOC_QBUF OUT fail\n", buf_id);
		return -1;
	}

	free(planes);
	return 0;
}

static int mxc_m2m_dequeue_in_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = m2m_dev->fd;
	int ret;

	planes = malloc(m2m_dev->o_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", m2m_dev->o_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = m2m_dev->in_num_planes;

	ret = ioctl(fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		v4l2_err("VIDIOC_DQBUF error\n");
		free(planes);
		return ret;
	}
	m2m_dev->in_frame_num++;
	m2m_dev->in_cur_buf_id = buf.index;

	free(planes);
	return 0;
}

static int mxc_m2m_dequeue_out_buffer(struct mxc_m2m_device *m2m_dev)
{
	struct v4l2_buffer buf;
	struct v4l2_plane *planes;
	int fd = m2m_dev->fd;
	int ret;

	planes = malloc(m2m_dev->o_num_planes * sizeof(*planes));
	if (!planes) {
		v4l2_err("alloc %d plane fail\n", m2m_dev->o_num_planes);
		return -ENOMEM;
	}

	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = m2m_dev->o_num_planes;

	ret = ioctl(fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		v4l2_err("VIDIOC_DQBUF error\n");
		free(planes);
		return ret;
	}
	m2m_dev->out_frame_num++;
	m2m_dev->out_cur_buf_id = buf.index;

	free(planes);
	return 0;
}

static int mxc_m2m_queue(struct mxc_m2m_device *m2m_dev)
{
	int i, ret;

	for (i = 0; i < TEST_BUFFER_NUM_IN; i++) {
		ret = mxc_m2m_queue_in_buffer(i, m2m_dev);
		if (ret < 0)
			return ret;
	}

	for (i = 0; i < TEST_BUFFER_NUM_OUT; i++) {
		ret = mxc_m2m_queue_out_buffer(i, m2m_dev);
		if (ret < 0)
			return ret;
	}

	return 0;
}
#if 0
static int mxc_m2m_dequeue(struct mxc_m2m_device *m2m_dev)
{
	int ret;

	ret = mxc_m2m_dequeue_in_buffer(m2m_dev);
	if (ret < 0)
		return ret;

	ret = mxc_m2m_dequeue_out_buffer(m2m_dev);
	if (ret < 0)
		return ret;

	return 0;
}
#endif
static int mxc_m2m_in_streamon(struct mxc_m2m_device *m2m_dev)
{
	enum v4l2_buf_type type;
	int ret, fd = m2m_dev->fd;

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		v4l2_err("in VIDIOC_STREAMON error\n");
		return ret;;
	}
	v4l2_info("in start capturing\n");

	return 0;
}

static int mxc_m2m_out_streamon(struct mxc_m2m_device *m2m_dev)
{
	enum v4l2_buf_type type;
	int ret, fd = m2m_dev->fd;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		v4l2_err("out VIDIOC_STREAMON error\n");
		return ret;;
	}
	v4l2_info("out start capturing\n");

	return 0;
}

static int mxc_m2m_streamon(struct mxc_m2m_device *m2m_dev)
{
	int ret;

	ret = mxc_m2m_in_streamon(m2m_dev);
	if (ret < 0)
		return ret;

	ret = mxc_m2m_out_streamon(m2m_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static int mxc_m2m_in_streamoff(struct mxc_m2m_device *m2m_dev)
{
	enum v4l2_buf_type type;
	int fd = m2m_dev->fd;
	int ret;

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		v4l2_err("in VIDIOC_STREAMOFF error\n");
		return ret;;
	}
	v4l2_info("in stop capturing\n");

	return 0;
}

static int mxc_m2m_out_streamoff(struct mxc_m2m_device *m2m_dev)
{
	enum v4l2_buf_type type;
	int fd = m2m_dev->fd;
	int ret;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		v4l2_err("out VIDIOC_STREAMOFF error\n");
		return ret;;
	}
	v4l2_info("out stop capturing\n");

	return 0;

}

static int mxc_m2m_streamoff(struct mxc_m2m_device *m2m_dev)
{
	int ret;

	ret = mxc_m2m_in_streamoff(m2m_dev);
	if (ret < 0)
		return ret;

	ret = mxc_m2m_out_streamoff(m2m_dev);
	if (ret < 0)
		return ret;

	return 0;
}

static void mxc_m2m_free_buffer(struct mxc_m2m_device *m2m_dev)
{
	unmap_in_buffer(m2m_dev);
	free_in_buffer(m2m_dev);

	unmap_out_buffer(m2m_dev);
	free_out_buffer(m2m_dev);
}

static int start_convert(struct mxc_m2m_device *m2m_dev)
{
	int count = 0, fps;
	int ret;
	struct timeval start, end;

	memset(&start, 0, sizeof(start));
	memset(&end, 0, sizeof(end));

	gettimeofday(&start, NULL);
	do {
		ret = mxc_m2m_dequeue_out_buffer(m2m_dev);
		if (ret < 0)
			return -1;
		ret = mxc_m2m_dequeue_in_buffer(m2m_dev);
		if (ret < 0)
			return -1;

		if (!g_performance_test) {
			ret = save_to_file(m2m_dev);
			if (ret < 0)
				return ret;
		}

		if (++count < m2m_dev->frames) {
			if (!g_performance_test)
				fill_in_buffer(m2m_dev->in_cur_buf_id, m2m_dev);
			mxc_m2m_queue_out_buffer(m2m_dev->out_cur_buf_id, m2m_dev);
			mxc_m2m_queue_in_buffer(m2m_dev->in_cur_buf_id, m2m_dev);
		}

	} while (count < m2m_dev->frames && !quitflag);

	gettimeofday(&end, NULL);

	if (g_performance_test) {
		fps = get_fps(&start, &end, m2m_dev->frames);
		printf(">> fps=%d(fps) <<\n", fps);
	}

	return 0;
}


int main(int argc, char *argv[])
{
	char *soc_list[] = { "i.MX8QM", "i.MX8QXP", "i.MX8MP", " " };
	struct mxc_m2m_device *m2m_dev;
	int ret = 0;

	ret = soc_version_check(soc_list);
	if (ret == 0) {
		v4l2_err("not supported on current soc\n");
		return 0;
	}

	pthread_t sigtid;
	sigemptyset(&sigset_v);
	sigaddset(&sigset_v, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset_v, NULL);
	pthread_create(&sigtid, NULL, (void *)&signal_thread, NULL);

	ret = parse_cmdline(argc, argv);
	if (ret < 0)
		return ret;

	m2m_dev = malloc(sizeof(*m2m_dev));
	if (!m2m_dev) {
		v4l2_err("alloc memory for m2m device fail\n");
		return -1;
	}

	fill_param(&m2m_dev->src, in_width, in_height);
	fill_param(&m2m_dev->dst, out_width, out_height);

	ret = mxc_m2m_open(m2m_dev);
	if (ret < 0)
		goto free;

	ret = check_device_cap(m2m_dev);
	if (ret < 0)
		goto close;

	if (show_device_cap) {
		show_device_cap_list(m2m_dev->fd);
		goto close;
	}

	ret = check_input_parameters(m2m_dev);
	if (ret < 0)
		goto close;

	ret = mxc_m2m_prepare(m2m_dev);
	if (ret < 0)
		goto close;

	ret = mxc_m2m_request_buffer(m2m_dev);
	if (ret < 0)
		goto close;

	fill_in_buffer(0, m2m_dev);
	ret = mxc_m2m_queue(m2m_dev);
	if (ret < 0)
		goto free_buf;

	ret = mxc_m2m_streamon(m2m_dev);
	if (ret < 0)
		goto free_buf;

	start_convert(m2m_dev);

	ret = mxc_m2m_streamoff(m2m_dev);
	if (ret < 0)
		goto free_buf;

free_buf:
	mxc_m2m_free_buffer(m2m_dev);
close:
	mxc_m2m_close(m2m_dev);
free:
	free(m2m_dev);
	return ret;
}
