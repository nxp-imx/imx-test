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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
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
#define TEST_BUFFER_NUM_OUT		1

struct plane_buffer {
	__u8 *start;
	__u32 plane_size;
	__u32 length;
	size_t offset;
};

struct mxc_buffer {
	__u8 *start;
	__u32 length;
	size_t offset;

	struct plane_buffer planes[3];
};

FILE *in_file, *out_file;
static struct mxc_buffer in_buffer;
static struct mxc_buffer out_buffer;

static char in_file_name[32] = {"0.rgb32"};
static char out_file_name[32] = {"out.dat"};
static char dev_name[32] = {"/dev/video0"};
static char in_format[32] = {"RGB32"};
static char out_format[32] = {"YUYV"};
static int in_width = 1280;
static int in_height = 800;
static int out_width = 1280;
static int out_height = 800;
static int log_level = 6;
static int show_device_cap = 0;
static int g_num_planes = 1;

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
		   "examples:\n"
		   "\t %s\n"
		   "\t %s -i 0.rgb32 -iw 1280 -ih 800 -ifmt \"RGB32\" "
		   "-o out.dat -ow 1280 -oh 800 -ofmt \"NV12\"\n",
		   name, name, name, name, name);
}

static __u32 to_fourcc(char fmt[])
{
	__u32 fourcc;

	if (!strcmp(fmt, "RGB32"))
		fourcc = V4L2_PIX_FMT_RGB32;
	else if (!strcmp(fmt, "ARGB32"))
		fourcc = V4L2_PIX_FMT_ARGB32;
	else if (!strcmp(fmt, "BGR32"))
		fourcc = V4L2_PIX_FMT_BGR32;
	else if (!strcmp(fmt, "RGB24"))
		fourcc = V4L2_PIX_FMT_RGB24;
	else if (!strcmp(fmt, "BGR24"))
		fourcc = V4L2_PIX_FMT_BGR24;
	else if (!strcmp(fmt, "RGB565"))
		fourcc = V4L2_PIX_FMT_RGB565;
	else if (!strcmp(fmt, "YUV444"))
		fourcc = V4L2_PIX_FMT_YUV32;
	else if (!strcmp(fmt, "YUYV"))
		fourcc = V4L2_PIX_FMT_YUYV;
	else if (!strcmp(fmt, "NV12"))
		fourcc = V4L2_PIX_FMT_NV12;
	else {
		v4l2_err("Not support format, set default to RGB32\n");
		fourcc = V4L2_PIX_FMT_RGB32;
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
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
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

	/* Show out device */
	fmtdesc.index = 0;
	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
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
	case V4L2_PIX_FMT_RGB32:
	case V4L2_PIX_FMT_ARGB32:
	case V4L2_PIX_FMT_BGR32:
	case V4L2_PIX_FMT_YUV32:
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
		bpp = 4;
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

static int check_input_parameters(int fd)
{
	long filesize;

	if (out_width > in_width || out_height > in_height) {
		v4l2_err("out w/h need equal to or less than input w/h\n");
		return -1;
	}

	fseek(in_file, 0, SEEK_END);
	filesize = ftell(in_file);
	fseek(in_file, 0, SEEK_SET);

	if (filesize < in_width * in_height * get_bpp(in_format)) {
		v4l2_err("input width and height is wrong\n");
		return -1;
	}

	return 0;
}

static void free_src_resource(int fd)
{
	struct v4l2_requestbuffers req;
	int ret;

	/* Free src buffer */
	if (in_buffer.start != MAP_FAILED && in_buffer.start > 0)
		munmap(in_buffer.start, in_buffer.length);

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("IN: free v4l2 buffer fail\n");
		return;
	}
}

static void free_dst_resource(int fd)
{
	struct v4l2_requestbuffers req;
	int i, ret;

	/* Free out buffer */
	for (i = 0; i < g_num_planes; i++) {
		if (out_buffer.planes[i].start != MAP_FAILED &&
			out_buffer.planes[i].start > 0)
			munmap(out_buffer.planes[i].start, out_buffer.planes[i].length);
	}

	memset(&req, 0, sizeof(req));
	req.count = 0;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req);
	if (ret < 0) {
		v4l2_err("OUT: free v4l2 buffer fail\n");
		return;
	}
}

int main(int argc, char *argv[])
{
	char *soc_list[] = { "i.MX8QM", "i.MX8QXP", " " };
	int ret;

	ret = soc_version_check(soc_list);
	if (ret == 0) {
		v4l2_err("not supported on current soc\n");
		return 0;
	}

	ret = parse_cmdline(argc, argv);
	if (ret < 0)
		return ret;

	int fd;
	fd = open(dev_name, O_RDWR, 0);
	if (fd < 0) {
		v4l2_err("Open %s fail\n", dev_name);
		return -1;
	}

	in_file = fopen(in_file_name, "rb");
	if (in_file == NULL) {
		v4l2_err("Open %s fail\n", in_file_name);
		close(fd);
		return -1;
	}

	out_file = fopen(out_file_name, "wb");
	if (in_file == NULL) {
		v4l2_err("Open %s fail\n", in_file_name);
		fclose(in_file);
		close(fd);
		return -1;
	}

	ret = check_input_parameters(fd);
	if (ret < 0)
		goto close;

	struct v4l2_capability capabilities;
	if (ioctl(fd, VIDIOC_QUERYCAP, &capabilities) < 0) {
		v4l2_err("VIDIOC_QUERYCAP fail\n");
		goto close;
	}

	if (!(capabilities.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)) {
		v4l2_err("The device does not handle memory to memory video capture.\n");
		goto close;
	}

	if (show_device_cap) {
		show_device_cap_list(fd);
		goto close;
	}

	/* IN S_FMT */
	struct v4l2_format in_fmt;

	memset(&in_fmt, 0, sizeof(in_fmt));
	in_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	in_fmt.fmt.pix.pixelformat = to_fourcc(in_format);
	in_fmt.fmt.pix.width = in_width;
	in_fmt.fmt.pix.height = in_height;
	ret = ioctl(fd, VIDIOC_S_FMT, &in_fmt);
	if (ret < 0) {
		v4l2_err("in VIDIOC_S_FMT fail\n");
		goto close;
	}

	struct v4l2_format out_fmt;

	memset(&out_fmt, 0, sizeof(out_fmt));
	out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	out_fmt.fmt.pix_mp.pixelformat = to_fourcc(out_format);
	out_fmt.fmt.pix_mp.width = out_width;
	out_fmt.fmt.pix_mp.height = out_height;
	ret = ioctl(fd, VIDIOC_S_FMT, &out_fmt);
	if (ret < 0) {
		v4l2_err("out VIDIOC_S_FMT fail\n");
		goto close;
	}

	/* IN G_FMT */
	memset(&in_fmt, 0, sizeof(in_fmt));
	in_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_G_FMT, &in_fmt);
	if (ret < 0) {
		v4l2_err("in VIDIOC_G_FMT fail\n");
		return ret;
	}
	v4l2_info("in: w/h=(%d,%d) pixelformat=%.4s bytesperline=%d sizeimage=%d\n",
			  in_fmt.fmt.pix.width,
			  in_fmt.fmt.pix.height,
			  (char *)&in_fmt.fmt.pix.pixelformat,
			  in_fmt.fmt.pix.bytesperline,
			  in_fmt.fmt.pix.sizeimage);

	memset(&out_fmt, 0, sizeof(out_fmt));
	out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
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
	int i;
	for (i = 0; i < out_fmt.fmt.pix_mp.num_planes; i++) {
		v4l2_info("\t plane[%d]: bytesperline=%d sizeimage=%d\n", i,
				  out_fmt.fmt.pix_mp.plane_fmt[i].bytesperline,
				  out_fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
	}
	g_num_planes = out_fmt.fmt.pix_mp.num_planes;

	/* REQBUF IN */
	struct v4l2_requestbuffers bufrequestin;
	struct v4l2_buffer bufferin;

	memset(&bufrequestin, 0, sizeof(bufrequestin));
	bufrequestin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufrequestin.memory = V4L2_MEMORY_MMAP;
	bufrequestin.count = TEST_BUFFER_NUM_IN;
	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestin) < 0) {
		v4l2_err("VIDIOC_REQBUFS IN fail\n");
		goto close;
	}

	/* Query Buffer for IN */
	memset(&bufferin, 0, sizeof(bufferin));
	bufferin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufferin.memory = V4L2_MEMORY_MMAP;
	bufferin.index = 0;
	if (ioctl(fd, VIDIOC_QUERYBUF, &bufferin) < 0) {
		v4l2_err("VIDIOC_QUERYBUF IN fail\n");
		goto free_src;
	}

	in_buffer.length = bufferin.length;
	in_buffer.offset = bufferin.m.offset;
	in_buffer.start = mmap(NULL, bufferin.length,
					 PROT_READ | PROT_WRITE, MAP_SHARED,
					 fd,
					 bufferin.m.offset);
	if (in_buffer.start == MAP_FAILED) {
		v4l2_err("mmap in fail\n");
		goto free_src;
	}

	memset(in_buffer.start, 0, bufferin.length);
	v4l2_info("in: buffer->planes[0]:"
			  "startAddr=0x%p, offset=0x%x, buf_size=%d, bytesused=%d\n",
			  (unsigned int *)in_buffer.start,
			  (unsigned int)in_buffer.offset,
			  in_buffer.length, bufferin.bytesused);

	/* Request Buffer for OUT */
	struct v4l2_requestbuffers bufrequestout;
	struct v4l2_buffer bufferout;
	struct v4l2_plane *planes;

	/* REQBUF OUT */
	planes = malloc(g_num_planes * sizeof(*planes));
	if (planes == NULL) {
		v4l2_err("alloc memory for plane fail\n");
		goto free_src;
	}
	memset(planes, 0, g_num_planes * sizeof(*planes));

	memset(&bufrequestout, 0, sizeof(bufrequestout));
	bufrequestout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	bufrequestout.memory = V4L2_MEMORY_MMAP;
	bufrequestout.count = TEST_BUFFER_NUM_OUT;
	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestout) < 0) {
		v4l2_err("VIDIOC_REQBUFS OUT fail\n");
		goto free;
	}

	/* QUERYBUF OUT */
	memset(&bufferout, 0, sizeof(bufferout));
	bufferout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	bufferout.memory = V4L2_MEMORY_MMAP;
	bufferout.m.planes = planes;
	bufferout.length = g_num_planes;
	bufferout.index = 0;
	if (ioctl(fd, VIDIOC_QUERYBUF, &bufferout) < 0) {
		v4l2_err("VIDIOC_QUERYBUF OUT fail\n");
		goto free_dst;
	}

	int j;
	for (j = 0; j < g_num_planes; j++) {
		out_buffer.planes[j].length = bufferout.m.planes[j].length;
		out_buffer.planes[j].offset = bufferout.m.planes[j].m.mem_offset;
		out_buffer.planes[j].start = mmap(NULL,
					bufferout.m.planes[j].length,
					PROT_READ | PROT_WRITE, MAP_SHARED,
					fd,
					bufferout.m.planes[j].m.mem_offset);
		if (out_buffer.planes[j].start == MAP_FAILED) {
			v4l2_err("mmap out fail\n");
			goto free_dst;
		}

		v4l2_info("out: buffer->planes[%d]:"
			  "startAddr=0x%p, offset=0x%x, buf_size=%d\n", j,
			  (unsigned int *)out_buffer.planes[j].start,
			  (unsigned int)out_buffer.planes[j].offset,
			  out_buffer.planes[j].length);
	}

	/* QBUF IN */
	memset(&bufferin, 0, sizeof(bufferin));
	bufferin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufferin.memory = V4L2_MEMORY_MMAP;
	bufferin.index = 0;
	if (ioctl(fd, VIDIOC_QBUF, &bufferin) < 0) {
		v4l2_err("VIDIOC_QBUF IN fail\n");
		goto free_dst;
	}

	/* QBUF OUT */
	memset(&bufferout, 0, sizeof(bufferout));
	bufferout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	bufferout.memory = V4L2_MEMORY_MMAP;
	bufferout.m.planes = planes;
	bufferout.index = 0;
	bufferout.length = g_num_planes;

	for (j = 0; j < g_num_planes; j++) {
		bufferout.m.planes[j].length = out_buffer.planes[j].length;
		bufferout.m.planes[j].m.mem_offset = out_buffer.planes[j].offset;
		bufferout.m.planes[j].bytesused = out_buffer.planes[j].length;
	}
	if (ioctl(fd, VIDIOC_QBUF, &bufferout) < 0) {
		v4l2_err("VIDIOC_QBUF OUT fail\n");
		goto free;
	}

	fread(in_buffer.start, in_buffer.length, 1, in_file);

	enum v4l2_buf_type in_type, out_type;

	/* STREAM ON */
	in_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &in_type) < 0) {
		v4l2_err("VIDIOC_STREAMON IN fail\n");
		goto free_dst;
	}

	out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ioctl(fd, VIDIOC_STREAMON, &out_type) < 0) {
		v4l2_err("VIDIOC_STREAMON OUT fail\n");
		goto free_dst;
	}

	size_t wsize;
	memset(&bufferout, 0, sizeof(bufferout));
	bufferout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	bufferout.memory = V4L2_MEMORY_MMAP;
	bufferout.m.planes = planes;
	bufferout.length = g_num_planes;
	if (ioctl(fd, VIDIOC_DQBUF, &bufferout) < 0) {
		v4l2_err("VIDIOC_STREAMON OUT fail\n");
		goto free_dst;
	}

	memset(&bufferin, 0, sizeof(bufferin));
	bufferin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufferin.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_DQBUF, &bufferin) < 0) {
		v4l2_err("VIDIOC_DQBUF IN fail\n");
		goto free_dst;
	}

	for (j = 0; j < g_num_planes; j++) {
		wsize = fwrite(out_buffer.planes[j].start,
					out_buffer.planes[j].length, 1, out_file);
		if (wsize < 1) {
			v4l2_err("No space left on device\n");
			goto finish;
		}
	}

finish:
	/* STREAM OFF */
	in_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMOFF, &in_type) < 0) {
		v4l2_err("VIDIOC_STREAMOFF IN fail\n");
		goto free;
	}

	out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ioctl(fd, VIDIOC_STREAMOFF, &out_type) < 0) {
		v4l2_err("VIDIOC_STREAMOFF OUT fail\n");
		goto free;
	}

	v4l2_info("\"%s\"->\"%s\" success\n", in_format, out_format);

free_dst:
	free_dst_resource(fd);
free:
	free(planes);
free_src:
	free_src_resource(fd);
close:
	fclose(in_file);
	fclose(out_file);
	close(fd);
	return ret;
}
