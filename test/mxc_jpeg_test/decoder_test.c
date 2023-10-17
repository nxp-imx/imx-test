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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "mxc_jpeg_test.h"

int subscribe_source_change_event(int fd)
{
	struct v4l2_event_subscription sub;

	memset(&sub, 0, sizeof(sub));
	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	return ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
}

int unsubcribe_event(int fd)
{
	struct v4l2_event_subscription sub;
	int ret;

	memset(&sub, 0, sizeof(sub));

	sub.type = V4L2_EVENT_ALL;
	ret = ioctl(fd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
	if (ret < 0) {
		fprintf(stderr, "fail to unsubscribe v4l2 event\n");
		return -1;
	}

	return 0;
}

int poll_event(int fd, short events, int timeout)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = fd;
	pfd.events = events;
	ret = poll(&pfd, 1, timeout);
	if (ret <= 0)
		return 0;

	if (pfd.revents & events)
		return 1;

	return 0;
}

int check_source_change(int fd)
{
	struct v4l2_event evt;
	int ret;

	poll_event(fd, POLLPRI, 3000);
	memset(&evt, 0, sizeof(struct v4l2_event));
	ret = ioctl(fd, VIDIOC_DQEVENT, &evt);
	if (ret < 0) {
		fprintf(stderr, "VIDIOC_DQEVENT failed\n");
		return -1;
	}

	if (evt.type == V4L2_EVENT_SOURCE_CHANGE)
		return 0;

	return -1;
}

void v4l2_decoder_loop(int vdev_fd, int n, struct v4l2_buffer *buf_cap, struct v4l2_buffer *buf_out)
{
	int i;
	struct timeval start, end;
	time_t usecs;
	double fps;
	struct timeval tv_start, tv_end;

	/*
	 * repeatedly enqueue/dequeue 1 output buffer and 1 capture buffer,
	 * the output buffer is filled once by the application and the result
	 * is expected to be filled by the device in the capture buffer,
	 * this is just to enable a stress test for the driver & the device
	 */
	for (i = 0; i < n; i++) {
		printf("Iteration #%d\n", i);
		gettimeofday(&tv_start, 0);
		/* the first buffer is queued before source change event */
		if (i > 0) {
			printf("\tQBUF IN\n");
			if (ioctl(vdev_fd, VIDIOC_QBUF, buf_out) < 0) {
				perror("VIDIOC_QBUF OUT");
				exit(1);
			}
		}

		printf("\tQBUF OUT\n");
		if (ioctl(vdev_fd, VIDIOC_QBUF, buf_cap) < 0) {
			perror("VIDIOC_QBUF IN");
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
		gettimeofday(&tv_end, 0);
		/*
		 * use capture and output buffer timestamps to measure
		 * performance with as little overhead as possible
		 */
		if (buf_out->timestamp.tv_sec == 0 || buf_cap->timestamp.tv_sec == 0) {
			start = tv_start;
			end = tv_end;
		} else {
			start = buf_out->timestamp;
			end = buf_cap->timestamp;
		}
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

int main(int argc, char *argv[])
{
	int fd;
	FILE *testjpg;
	struct encoder_args ea;
	bool is_mp = 0; /* 1 for multi-planar*/
	char *outfile = "outfile";
	struct v4l2_buffer bufferin;
	struct v4l2_buffer bufferout;
	void *bufferin_start[2] = {0};
	void *bufferout_start[1] = {0};

	parse_args(argc, argv, &ea);

	fd = open(ea.video_device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open video device %s\n",
			ea.video_device);
		exit(1);
	}

	testjpg = fopen(ea.test_file, "rb");
	if (!testjpg) {
		fprintf(stderr, "Could not open input file %s\n",
			ea.test_file);
		exit(1);
	}
	fseek(testjpg, 0, SEEK_END);
	long filesize = ftell(testjpg);
	fseek(testjpg, 0, SEEK_SET);
	printf("FILE SIZE for %s: %ld\n", ea.test_file, filesize);

	is_mp = v4l2_query_caps(fd);

	subscribe_source_change_event(fd);
	v4l2_s_fmt_out(fd, is_mp, &ea, filesize, V4L2_PIX_FMT_JPEG);

	if (ea.crop_w != 0 && ea.crop_h != 0) {
		printf("Cropping is not supported for the decoder\n");
		exit(1);
	}

	v4l2_reqbufs_out(fd, is_mp);
	v4l2_querybuf_out(fd, is_mp, &bufferout, filesize);

	printf("MMAP OUT\n");
	v4l2_mmap(fd, is_mp, &bufferout, bufferout_start);

	/*
	 * fill output buffer with the contents of the input jpeg file
	 * the output buffer is given to the device for processing,
	 * typically for display, hence the name "output", decoding in this case
	 */
	if (fread(bufferout_start[0], filesize, 1, testjpg) != 1)
		exit(1);

	fclose(testjpg);
	if (is_mp)
		bufferout.m.planes[0].bytesused = filesize;
	else
		bufferout.bytesused = filesize;

	/* Activate streaming for capture/output */
	v4l2_streamon_out(fd, is_mp);

	printf("V4L2_EVENT_SOURCE_CHANGE\n");
	printf("\tQBUF IN\n");
	if (ioctl(fd, VIDIOC_QBUF, &bufferout) < 0) {
		perror("VIDIOC_QBUF OUT");
		exit(1);
	}
	if (check_source_change(fd)) {
		perror("Failed on source change event\n");
		exit(1);
	}

	v4l2_s_fmt_cap(fd, is_mp, &ea, ea.fourcc);
	v4l2_reqbufs_cap(fd, is_mp);
	v4l2_querybuf_cap(fd, is_mp, &bufferin);
	printf("MMAP IN\n");
	v4l2_mmap(fd, is_mp, &bufferin, bufferin_start);
	v4l2_streamon_cap(fd, is_mp);

	/* enqueue & dequeue loop */
	v4l2_decoder_loop(fd, ea.num_iter, &bufferin, &bufferout);

	if (ea.hexdump)
		v4l2_print_payload(is_mp, &bufferin, bufferin_start);

	/*
	 * the decoded data is expected to be filled by the device
	 * in the capture buffer
	 */
	printf("Writing capture buffer payload to %s\n", outfile);
	v4l2_fwrite_cap_payload(fd, is_mp, outfile, &bufferin, bufferin_start, 8);

	/* Deactivate streaming for capture/output */
	v4l2_streamoff_out(fd, is_mp);
	v4l2_streamoff_cap(fd, is_mp);

	v4l2_munmap(fd, is_mp, &bufferin, bufferin_start);
	v4l2_munmap(fd, is_mp, &bufferout, bufferout_start);
	unsubcribe_event(fd);

	close(fd);
	return 0;
}
