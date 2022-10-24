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

	v4l2_s_fmt_out(fd, is_mp, &ea, filesize, V4L2_PIX_FMT_JPEG);
	v4l2_s_fmt_cap(fd, is_mp, &ea, ea.fourcc);

	if (ea.crop_w != 0 && ea.crop_h != 0) {
		printf("Cropping is not supported for the decoder\n");
		exit(1);
	}

	v4l2_reqbufs(fd, is_mp);

	v4l2_querybuf_cap(fd, is_mp, &bufferin);
	v4l2_querybuf_out(fd, is_mp, &bufferout, filesize);

	printf("MMAP IN\n");
	v4l2_mmap(fd, is_mp, &bufferin, bufferin_start);
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
	v4l2_streamon(fd, is_mp);

	/* enqueue & dequeue loop */
	v4l2_qbuf_dqbuf_loop(fd, ea.num_iter, &bufferin, &bufferout);

	if (ea.hexdump)
		v4l2_print_payload(is_mp, &bufferin, bufferin_start);

	/*
	 * the decoded data is expected to be filled by the device
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
