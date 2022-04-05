/*
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

#include <time.h>

#define NUM_BUFS 1

struct encoder_args {
	char *video_device;
	char *test_file;
	int width;
	int height;
	char *fmt;
	int fourcc;
	int hexdump;
	int num_iter;
};

struct pix_fmt_data {
	char name[10];
	char descr[80];
	int fourcc;
};

static struct pix_fmt_data fmt_data[] = {
	{
		.name	= "yuv420",
		.descr	= "2-planes, Y and UV-interleaved, same as NV12",
		.fourcc	= V4L2_PIX_FMT_NV12M
	},
	{
		.name	= "yuv422",
		.descr	= "packed YUYV",
		.fourcc	= V4L2_PIX_FMT_YUYV
	},
	{
		.name	= "rgb24",
		.descr	= "packed RGB (obsolete)",
		.fourcc	= V4L2_PIX_FMT_RGB24
	},
	{
		.name	= "bgr24",
		.descr	= "packed BGR",
		.fourcc	= V4L2_PIX_FMT_BGR24
	},
	{
		.name	= "yuv444",
		.descr	= "packed YUV",
		.fourcc	= V4L2_PIX_FMT_YUV24
	},
	{
		.name	= "gray",
		.descr	= "Y8 or Y12 or Single Component",
		.fourcc	= V4L2_PIX_FMT_GREY
	},
	{
		.name	= "argb",
		.descr	= "packed ARGB (obsolete)",
		.fourcc	= V4L2_PIX_FMT_ARGB32
	},
	{
		.name	= "abgr",
		.descr	= "packed ABGR",
		.fourcc	= V4L2_PIX_FMT_ABGR32
	},
};

void print_usage(char *str)
{
	int i;

	printf("Usage: %s -d </dev/videoX> -f <FILENAME> ", str);
	printf("-w <width> -h <height> ");
	printf("-p <pixel_format> ");
	printf("[-n <iterations>] ");
	printf("[-x]\n");
	printf("Supported pixel formats:\n");
	for (i = 0; i < sizeof(fmt_data) / sizeof(*fmt_data); i++)
		printf("\t%8s: %s\n",
		       fmt_data[i].name,
		       fmt_data[i].descr);
	printf("Optional arguments:\n");
	printf("\t-x: print a hexdump of the result\n");
	printf("\t-n: number of iterations for enqueue/dequeue loop\n");
}


int get_fourcc(char *fmt)
{
	int i;

	for (i = 0; i < sizeof(fmt_data) / sizeof(*fmt_data); i++)
		if (strcmp(fmt_data[i].name, fmt) == 0)
			return fmt_data[i].fourcc;

	return 0;
}

int parse_args(int argc, char **argv, struct encoder_args *ea)
{
	int c;

	memset(ea, 0, sizeof(struct encoder_args));
	opterr = 0;
	ea->num_iter = 1;
	while ((c = getopt(argc, argv, "+d:f:w:h:p:xn:")) != -1)
		switch (c) {
		case 'd':
			ea->video_device = optarg;
			break;
		case 'f':
			ea->test_file = optarg;
			break;
		case 'w':
			ea->width = strtol(optarg, 0, 0);
			break;
		case 'h':
			ea->height = strtol(optarg, 0, 0);
			break;
		case 'p':
			ea->fourcc = get_fourcc(optarg);
			if (ea->fourcc == 0) {
				fprintf(stderr, "Unsupported pixelformat %s\n",
					optarg);
				goto print_usage_and_exit;
			}
			ea->fmt = optarg;
			break;
		case 'x':
			ea->hexdump = 1;
			break;
		case 'n':
			ea->num_iter = strtol(optarg, 0, 0);
			if (ea->num_iter < 1) {
				fprintf(stderr, "Need at least 1 iteration\n");
				goto print_usage_and_exit;
			}
			break;
		case '?':
			if (optopt == 'c')
				fprintf(stderr,
					"Missing argument for option  -%c\n",
					optopt);
			else if (isprint(optopt))
				fprintf(stderr,
					"Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr,
					"Unknown option character `\\x%x'.\n",
					optopt);
			goto print_usage_and_exit;
		default:
			exit(1);
		}

	if (ea->video_device == 0 || ea->test_file == 0 ||
		ea->width == 0 || ea->height == 0 || ea->fmt == 0) {
		goto print_usage_and_exit;
	}

	return 1;

print_usage_and_exit:
	print_usage(argv[0]);
	exit(1);
}

bool v4l2_query_caps(int vdev_fd)
{
	bool is_sp = 0;
	bool is_mp = 0;
	struct v4l2_capability capabilities;

	if (ioctl(vdev_fd, VIDIOC_QUERYCAP, &capabilities) < 0) {
		perror("VIDIOC_QUERYCAP");
		exit(1);
	}

	is_sp = capabilities.capabilities & V4L2_CAP_VIDEO_M2M;
	is_mp = capabilities.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE;
	if (!is_sp && !is_mp) {
		fprintf(stderr,
			"Device doesn't handle M2M video capture\n");
		exit(1);
	}

	return is_mp;
}

void v4l2_s_fmt_out(int vdev_fd, bool is_mp, const struct encoder_args ea,
		    __u32 sizeimage, __u32 pixelformat)
{
	struct v4l2_format out_fmt;

	if (!is_mp) {
		out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		out_fmt.fmt.pix.pixelformat = pixelformat;
		out_fmt.fmt.pix.sizeimage = sizeimage;
		out_fmt.fmt.pix.width = ea.width;
		out_fmt.fmt.pix.height = ea.height;
	} else {
		out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		out_fmt.fmt.pix_mp.pixelformat = pixelformat;
		out_fmt.fmt.pix_mp.num_planes = 2;
		out_fmt.fmt.pix_mp.plane_fmt[0].sizeimage = sizeimage;
		out_fmt.fmt.pix_mp.plane_fmt[1].sizeimage = 0;
		out_fmt.fmt.pix_mp.width = ea.width;
		out_fmt.fmt.pix_mp.height = ea.height;
	}
	if (ioctl(vdev_fd, VIDIOC_S_FMT, &out_fmt) < 0) {
		perror("VIDIOC_S_FMT OUT");
		exit(1);
	}
}

void v4l2_s_fmt_cap(int vdev_fd, bool is_mp, const struct encoder_args ea,
		    __u32 pixelformat)
{
	struct v4l2_format cap_fmt;

	if (!is_mp) {
		cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cap_fmt.fmt.pix.pixelformat = pixelformat;
		cap_fmt.fmt.pix.width = ea.width;
		cap_fmt.fmt.pix.height = ea.height;
	} else {
		cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		cap_fmt.fmt.pix_mp.pixelformat = pixelformat;
		if (pixelformat == V4L2_PIX_FMT_NV12M)
			cap_fmt.fmt.pix_mp.num_planes = 2;
		else
			cap_fmt.fmt.pix_mp.num_planes = 1;
		cap_fmt.fmt.pix_mp.width = ea.width;
		cap_fmt.fmt.pix_mp.height = ea.height;
	}
	if (ioctl(vdev_fd, VIDIOC_S_FMT, &cap_fmt) < 0) {
		perror("VIDIOC_S_FMT OUT");
		exit(1);
	}
}

void v4l2_reqbufs(int vdev_fd, bool is_mp)
{
	struct v4l2_requestbuffers bufreq_cap;
	struct v4l2_requestbuffers bufreq_out;

	/* The reserved array must be zeroed */
	memset(&bufreq_cap, 0, sizeof(bufreq_cap));
	memset(&bufreq_out, 0, sizeof(bufreq_out));

	/* the capture buffer is filled by the driver with data from device */
	bufreq_cap.type = is_mp ?
	    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufreq_cap.memory = V4L2_MEMORY_MMAP;
	bufreq_cap.count = NUM_BUFS;

	/*
	 * the output buffer is filled by the application
	 * and the driver sends it to the device, for processing
	 */
	bufreq_out.type = is_mp ?
	    V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufreq_out.memory = V4L2_MEMORY_MMAP;
	bufreq_out.count = NUM_BUFS;

	printf("IOCTL VIDIOC_REQBUFS\n");
	if (ioctl(vdev_fd, VIDIOC_REQBUFS, &bufreq_cap) < 0) {
		perror("VIDIOC_REQBUFS IN");
		exit(1);
	}
	if (ioctl(vdev_fd, VIDIOC_REQBUFS, &bufreq_out) < 0) {
		perror("VIDIOC_REQBUFS OUT");
		exit(1);
	}
}

void v4l2_querybuf_cap(int vdev_fd, bool is_mp, struct v4l2_buffer *buf)
{
	int plane;

	/* the capture buffer is filled by the driver */
	memset(buf, 0, sizeof(*buf));
	buf->type = is_mp ?
	    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* bytesused set by the driver for capture stream */
	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = 0;
	if (is_mp) {
		buf->length = 2;
		buf->m.planes = calloc(buf->length, sizeof(struct v4l2_plane));
	}
	printf("IOCTL VIDIOC_QUERYBUF IN\n");
	if (ioctl(vdev_fd, VIDIOC_QUERYBUF, buf) < 0) {
		perror("VIDIOC_QUERYBUF");
		exit(1);
	}
	if (!is_mp)
		return;
	printf("\tActual number of planes=%d\n", buf->length);
	for (plane = 0; plane < buf->length; plane++) {
		printf("\tPlane %d bytesused=%d, length=%d, data_offset=%d\n",
		       plane,
		       buf->m.planes[plane].bytesused,
		       buf->m.planes[plane].length,
		       buf->m.planes[plane].data_offset);
	}
}

void v4l2_querybuf_out(int vdev_fd, bool is_mp, struct v4l2_buffer *bufferout,
		       __u32 bytesused)
{
	int plane;

	memset(bufferout, 0, sizeof(*bufferout));
	bufferout->type = is_mp ?
	    V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufferout->memory = V4L2_MEMORY_MMAP;
	/* bytesused ignored for multiplanar */
	bufferout->bytesused = is_mp ? 0 : bytesused;
	bufferout->index = 0;
	if (is_mp) {
		bufferout->length = 2;
		bufferout->m.planes = calloc(bufferout->length,
					     sizeof(struct v4l2_plane));
		bufferout->m.planes[0].bytesused = bytesused;
		bufferout->m.planes[1].bytesused = 0;
	}
	printf("IOCTL VIDIOC_QUERYBUF OUT\n");
	if (ioctl(vdev_fd, VIDIOC_QUERYBUF, bufferout) < 0) {
		perror("VIDIOC_QUERYBUF OUT");
		exit(1);
	}
	if (!is_mp)
		return;
	printf("\tActual number of planes=%d\n", bufferout->length);
	for (plane = 0; plane < bufferout->length; plane++) {
		printf("\tPlane %d bytesused=%d, length=%d, data_offset=%d\n",
		       plane,
		       bufferout->m.planes[plane].bytesused,
		       bufferout->m.planes[plane].length,
		       bufferout->m.planes[plane].data_offset);
	}
}

void v4l2_mmap(int vdev_fd, bool is_mp, struct v4l2_buffer *buf,
	       void *buf_start[])
{
	int i;

	if (!is_mp) {
		buf_start[0] = mmap(NULL,
				    buf->length, /* set by the driver */
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED,
				    vdev_fd,
				    buf->m.offset);
		if (buf_start[0] == MAP_FAILED) {
			perror("mmap in");
			exit(1);
		}
		printf("\tMMAP-ed single-plane\n");
		/* empty capture buffer */
		memset(buf_start[0], 0, buf->length);
		return;
	}
	/* multi-planar */
	for (i = 0; i < buf->length; i++) {
		buf_start[i] = mmap(NULL,
				    buf->m.planes[i].length, /* set by driver */
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED,
				    vdev_fd,
				    buf->m.planes[i].m.mem_offset);
		if (buf_start[i] == MAP_FAILED) {
			perror("mmap in, multi-planar");
			exit(1);
		}
		printf("\tMMAP-ed plane %d\n", i);
		/* empty capture buffer */
		memset(buf_start[i], 0, buf->m.planes[i].length);
	}
}

void v4l2_munmap(int vdev_fd, bool is_mp, struct v4l2_buffer *buf,
		 void *buf_start[])
{
	int i;

	if (!is_mp) {
		munmap(buf_start[0], buf->length);
		printf("MUNMAP-ed single-plane\n");
		return;
	}
	for (i = 0; i < buf->length; i++) {
		munmap(buf_start[i], buf->m.planes[i].length);
		printf("MUNMAP-ed plane %d\n", i);
	}
}

void v4l2_streamon(int vdev_fd, bool is_mp)
{
	int type_cap, type_out;

	if (is_mp) {
		type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else {
		type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	}

	printf("STREAMON IN\n");
	if (ioctl(vdev_fd, VIDIOC_STREAMON, &type_cap) < 0) {
		perror("VIDIOC_STREAMON IN");
		exit(1);
	}
	printf("STREAMON OUT\n");
	if (ioctl(vdev_fd, VIDIOC_STREAMON, &type_out) < 0) {
		perror("VIDIOC_STREAMON OUT");
		exit(1);
	}
}

void v4l2_streamoff(int vdev_fd, bool is_mp)
{
	int type_cap, type_out;

	if (is_mp) {
		type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else {
		type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	}
	if (ioctl(vdev_fd, VIDIOC_STREAMOFF, &type_cap) < 0) {
		perror("VIDIOC_STREAMOFF IN");
		exit(1);
	}
	if (ioctl(vdev_fd, VIDIOC_STREAMOFF, &type_out) < 0) {
		perror("VIDIOC_STREAMOFF OUT");
		exit(1);
	}
}

void v4l2_qbuf_dqbuf_loop(int vdev_fd, int n, struct v4l2_buffer *buf_cap,
			  struct v4l2_buffer *buf_out)
{
	int i;
	struct timeval start, end;
	time_t usecs;
	double fps;

	/*
	 * repeatedly enqueue/dequeue 1 output buffer and 1 capture buffer,
	 * the output buffer is filled once by the application and the result
	 * is expected to be filled by the device in the capture buffer,
	 * this is just to enable a stress test for the driver & the device
	 */
	for (i = 0; i < n; i++) {
		printf("Iteration #%d\n", i);
		printf("\tQBUF IN\n");
		if (ioctl(vdev_fd, VIDIOC_QBUF, buf_cap) < 0) {
			perror("VIDIOC_QBUF IN");
			exit(1);
		}
		printf("\tQBUF OUT\n");
		if (ioctl(vdev_fd, VIDIOC_QBUF, buf_out) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}

		printf("\tDQBUF OUT\n");
		if (ioctl(vdev_fd, VIDIOC_DQBUF, buf_out) < 0) {
			perror("VIDIOC_DQBUF OUT");
			exit(1);
		}
		printf("\tDQBUF IN\n");
		if (ioctl(vdev_fd, VIDIOC_DQBUF, buf_cap) < 0) {
			perror("VIDIOC_DQBUF IN");
			exit(1);
		}

		/*
		 * use capture and output buffer timestamps to measure
		 * performance with as little overhead as possible
		 */
		start = buf_out->timestamp;
		end = buf_cap->timestamp;
		usecs = 1000000 * (end.tv_sec - start.tv_sec) +
				end.tv_usec - start.tv_usec;
		if (!usecs)
			fps = 0;
		else
			fps = (double)1000000 / usecs;
		printf("Iteration #%d misecrosecs=%lu fps=%.02lf\n",
		       i, usecs, fps);
	}
}

void v4l2_fwrite_payload(bool is_mp, const char *filename,
			 struct v4l2_buffer *buf, void *buf_start[])
{
	int plane;
	FILE *fout;

	fout = fopen(filename, "wb");
	if (!is_mp) {
		printf("\tSingle plane payload: %d bytes\n", buf->bytesused);
		fwrite(buf_start[0], buf->bytesused, 1, fout);
	} else {
		for (plane = 0; plane < buf->length; plane++) {
			printf("\tPlane %d payload: %d bytes\n", plane,
			       buf->m.planes[plane].bytesused);
			fwrite(buf_start[plane],
			       buf->m.planes[plane].bytesused, 1, fout);
		}
	}
	fclose(fout);
}

void v4l2_print_payload(bool is_mp, struct v4l2_buffer *buf,
			void *buf_start[])
{
	int i, plane;

	if (!is_mp) {
		printf("Single plane payload:\n");
		for (i = 0; i < buf->bytesused; i++) {
			printf("%02x ", ((char *)buf_start[0])[i]);
			if ((i + 1) % 32 == 0)
				printf("\n");
		}
		return;
	}
	/* multi-planar */
	for (plane = 0; plane < buf->length; plane++) {
		printf("Plane %d payload:\n", plane);
		for (i = 0; i < buf->m.planes[plane].bytesused; i++) {
			printf("%02x ", ((char *)buf_start[plane])[i]);
			if ((i + 1) % 32 == 0)
				printf("\n");
		}
	}
}
