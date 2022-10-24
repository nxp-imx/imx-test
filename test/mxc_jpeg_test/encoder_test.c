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
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "mxc_jpeg_test.h"

int main(int argc, char *argv[])
{
	int fd;
	FILE *testraw;
	long filesize;
	struct encoder_args ea;
	bool is_mp = 0; /* 1 for multi-planar*/
	char *outfile = "outfile.jpeg";
	struct v4l2_buffer bufferin;
	struct v4l2_buffer bufferout;
	void *bufferin_start[1] = {0};
	void *bufferout_start[2] = {0};

	parse_args(argc, argv, &ea);

	fd = open(ea.video_device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Could not open video device %s\n",
			ea.video_device);
		exit(1);
	}

	if (ea.quality != -1) {
		struct v4l2_control argp;

		argp.id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
		argp.value = ea.quality;
		if (ioctl(fd, VIDIOC_S_CTRL, &argp)) {
			perror("VIDIOC_S_CTRL quality");
			exit(1);
		}
	}

	testraw = fopen(ea.test_file, "rb");
	if (!testraw) {
		fprintf(stderr, "Could not open input raw file %s\n",
			ea.test_file);
		exit(1);
	}
	fseek(testraw, 0, SEEK_END);
	filesize = ftell(testraw);
	fseek(testraw, 0, SEEK_SET);
	printf("FILE SIZE for %s: %ld\n", ea.test_file, filesize);

	is_mp = v4l2_query_caps(fd);

	v4l2_s_fmt_out(fd, is_mp, &ea, filesize, ea.fourcc);
	v4l2_s_fmt_cap(fd, is_mp, &ea, V4L2_PIX_FMT_JPEG);

	if (ea.crop_w != 0 && ea.crop_h != 0) {
		struct v4l2_selection sel = {
			.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
			.target = V4L2_SEL_TGT_CROP,
		};
		sel.r.width = ea.crop_w;
		sel.r.height = ea.crop_h;
		if (ioctl(fd, VIDIOC_S_SELECTION, &sel)) {
			perror("VIDIOC_S_SELECTION OUT");
			exit(-1);
		}
		printf("VIDIOC_S_SELECTION OUT (%d x %d)\n", sel.r.width, sel.r.height);
	}

	v4l2_reqbufs(fd, is_mp);

	v4l2_querybuf_cap(fd, is_mp, &bufferin);
	v4l2_querybuf_out(fd, is_mp, &bufferout, filesize);

	printf("MMAP IN\n");
	v4l2_mmap(fd, is_mp, &bufferin, bufferin_start);
	printf("MMAP OUT\n");
	v4l2_mmap(fd, is_mp, &bufferout, bufferout_start);

	/*
	 * fill output buffer with the contents of the input raw file
	 * the output buffer is given to the device for processing,
	 * typically for display, hence the name "output", encoding in this case
	 */
	if (!is_mp) {
		if (fread(bufferout_start[0], filesize, 1, testraw) != 1)
			exit(1);
		bufferout.bytesused = filesize;
	} else { /* multi-planar */
		if (ea.fourcc != V4L2_PIX_FMT_NV12M) {
			if (fread(bufferout_start[0], filesize, 1, testraw) != 1)
				exit(1);
			bufferout.m.planes[0].bytesused = filesize;
		} else {
			int expected_size_padded = ea.w_padded * ea.h_padded * 3 / 2;
			int expected_size = ea.width * ea.height * 3 / 2;
			int luma_size = ea.w_padded * ea.h_padded;
			int chroma_size = ea.w_padded * ea.h_padded / 2;

			printf("%d x %d NV12 expected size %d/%d, actual filesize is %ld\n",
			       ea.width, ea.height, expected_size, expected_size_padded, filesize);

			if (filesize != expected_size_padded) {
				if (ea.width != ea.w_padded)
					exit(1);
				else if (filesize != expected_size)
					exit(1);
				/*
				 * if only the height is unaligned, allow NV12 buffer
				 * without padding, ex: 1920 x 1080
				 */
				luma_size = ea.width * ea.height;
				chroma_size = ea.width * ea.height / 2;
			}
			/* read luma */
			if (fread(bufferout_start[0], luma_size, 1, testraw) != 1)
				exit(1);
			bufferout.m.planes[0].bytesused = luma_size;
			/* read chroma */
			fseek(testraw, 0, luma_size + 1);
			if (fread(bufferout_start[1], chroma_size, 1, testraw) != 1)
				exit(1);
			bufferout.m.planes[1].bytesused = chroma_size;
		}
	}
	fclose(testraw);

	/* Activate streaming for capture/output */
	v4l2_streamon(fd, is_mp);

	/* enqueue & dequeue loop */
	v4l2_qbuf_dqbuf_loop(fd, ea.num_iter, &bufferin, &bufferout);

	if (ea.hexdump)
		v4l2_print_payload(is_mp, &bufferin, bufferin_start);

	/*
	 * the encoded data is expected to be filled by the device
	 * in the capture buffer
	 */
	printf("Writing capture buffer payload to %s\n", outfile);
	v4l2_fwrite_cap_payload(fd, is_mp, outfile, &bufferin, bufferin_start, 8);

	/* Deactivate streaming for capture/output */
	v4l2_streamoff(fd, is_mp);

	v4l2_munmap(fd, is_mp, &bufferin, bufferin_start);
	v4l2_munmap(fd, is_mp, &bufferout, bufferout_start);

	close(fd);
	return 0;
}
