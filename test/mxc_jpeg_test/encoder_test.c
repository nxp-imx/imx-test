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
	int fd, i, j;
	FILE *testrgb;
	void *buf;
	struct encoder_args ea;

	parse_args(argc, argv, &ea);

	fd = open(ea.video_device, O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	testrgb = fopen(ea.test_file, "rb");
	fseek(testrgb, 0, SEEK_END);
	long filesize = ftell(testrgb);
	fseek(testrgb, 0, SEEK_SET);
	buf = malloc(filesize + 1);

	printf("\n%s FILE SIZE %ld\n", ea.test_file, filesize);

	struct v4l2_capability capabilities;
	if (ioctl(fd, VIDIOC_QUERYCAP, &capabilities) < 0) {
		perror("VIDIOC_QUERYCAP");
		exit(1);
	}

	if (!(capabilities.capabilities & V4L2_CAP_VIDEO_M2M)) {
		fprintf(stderr,
		  "The device does not handle single-planar video capture.\n");
		exit(1);
	}

	struct v4l2_format out_fmt;
	out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	out_fmt.fmt.pix.pixelformat = ea.fourcc;
	out_fmt.fmt.pix.sizeimage = filesize;
	out_fmt.fmt.pix.width = ea.width;
	out_fmt.fmt.pix.height = ea.height;

	struct v4l2_format cap_fmt;
	cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cap_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	cap_fmt.fmt.pix.width = ea.width;
	cap_fmt.fmt.pix.height = ea.height;

	printf("\nIOCTL VIDIOC_S_FMT\n");

	if (ioctl(fd, VIDIOC_S_FMT, &out_fmt) < 0) {
		perror("VIDIOC_S_FMT");
		exit(1);
	}
	printf("\n2\n");
	if (ioctl(fd, VIDIOC_S_FMT, &cap_fmt) < 0) {
		perror("VIDIOC_S_FMT");
		exit(1);
	}

	struct v4l2_requestbuffers bufrequestin;
	struct v4l2_requestbuffers bufrequestout;
	/* The reserved array must be zeroed */
	memset(&bufrequestin, 0, sizeof(bufrequestin));
	memset(&bufrequestout, 0, sizeof(bufrequestout));

	bufrequestin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufrequestin.memory = V4L2_MEMORY_MMAP;
	bufrequestin.count = 1;

	bufrequestout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufrequestout.memory = V4L2_MEMORY_MMAP;
	bufrequestout.count = 1;

	printf("\nIOCTL VIDIOC_REQBUFS\n");

	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestin) < 0) {
		perror("VIDIOC_REQBUFS IN");
		exit(1);
	}

	if (ioctl(fd, VIDIOC_REQBUFS, &bufrequestout) < 0) {
		perror("VIDIOC_REQBUFS OUT");
		exit(1);
	}

	struct v4l2_buffer bufferin;
	struct v4l2_buffer bufferout;
	memset(&bufferin, 0, sizeof(bufferin));
	memset(&bufferout, 0, sizeof(bufferout));

	bufferin.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	/* bytesused set by the driver for capture stream */
	bufferin.memory = V4L2_MEMORY_MMAP;
	bufferin.index = 0;

	bufferout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufferout.memory = V4L2_MEMORY_MMAP;
	bufferout.bytesused = filesize;
	bufferout.index = 0;

	printf("\nIOCTL VIDIOC_QUERYBUF\n");

	if (ioctl(fd, VIDIOC_QUERYBUF, &bufferin) < 0) {
		perror("VIDIOC_QUERYBUF IN");
		exit(1);
	}
	if (ioctl(fd, VIDIOC_QUERYBUF, &bufferout) < 0) {
		perror("VIDIOC_QUERYBUF OUT");
		exit(1);
	}

	bufferout.bytesused = filesize;

	void *bufferin_start = mmap(
				    NULL,
				    bufferin.length, /* set by the driver */
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED,
				    fd,
				    bufferin.m.offset
				   );

	/* empty capture buffer */
	memset(bufferin_start, 0, bufferin.length);

	if (bufferin_start == MAP_FAILED) {
		perror("mmap in");
		exit(1);
	}


	void *bufferout_start = mmap(
				     NULL,
				     bufferout.length,
				     PROT_READ | PROT_WRITE,
				     MAP_SHARED,
				     fd,
				     bufferout.m.offset
				    );

	if (bufferout_start == MAP_FAILED) {
		perror("mmap out");
		exit(1);
	}

	memset(bufferout_start, 0, bufferout.length);
	/* fill output buffer */
	fread(bufferout_start, filesize, 1, testrgb);
	fclose(testrgb);

	printf("BUFFER STARTS: %lx, %ld, other size %ld\n",
	       *(long *)bufferin_start,
	       (unsigned long) bufferin.length,
	       (unsigned long) bufferout.length);

	/* Here is where you typically start two loops:
	 * - One which runs for as long as you want to
	 *   capture frames (shoot the video).
	 * - One which iterates over your buffers everytime. */

	// Put the buffer in the incoming queue.



		printf("\n\nQBUF IN\n\n");
		if (ioctl(fd, VIDIOC_QBUF, &bufferin) < 0) {
			perror("VIDIOC_QBUF IN");
			exit(1);
		}

		if (ioctl(fd, VIDIOC_QBUF, &bufferout) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}

		// Activate streaming
		printf("\n\nSTREAMON IN\n\n");
		int typein = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(fd, VIDIOC_STREAMON, &typein) < 0) {
			perror("VIDIOC_STREAMON IN");
			exit(1);
		}
		printf("\n\nSTREAMON OUT\n\n");

		int typeout = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if (ioctl(fd, VIDIOC_STREAMON, &typeout) < 0) {
			perror("VIDIOC_STREAMON OUT");
			exit(1);
		}

		printf("\n\nDQBUF OUT\n\n");
		if (ioctl(fd, VIDIOC_DQBUF, &bufferout) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}
		printf("\n\nDQBUF IN\n\n");
		if (ioctl(fd, VIDIOC_DQBUF, &bufferin) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}

		for (j = 0; j < bufferin.bytesused; j++) {
			printf("%02x ", ((char *)bufferin_start)[j]);
			if ((j+1) % 32 == 0)
				printf("\n");
		}

	printf("\nDONE BUF %d %lx %lx, bytesused=0x%x\n", i,
	       *((long *)bufferout_start),
	       *((long *)bufferin_start), bufferin.bytesused);
	FILE *fout = fopen("outfile.jpeg", "wb");

	fwrite(bufferin_start, bufferin.bytesused, 1, fout);
	fclose(fout);


	/* Your loops end here. */

	// Deactivate streaming
	printf("\n\nTEST DONE\n\n");

	printf("\n\nDeactivate streaming\n\n");
	   if(ioctl(fd, VIDIOC_STREAMOFF, &typein) < 0){
	   perror("VIDIOC_STREAMOFF IN");
	   exit(1);
	   }

	   printf("\n\nOFF OUT\n\n");
	   if(ioctl(fd, VIDIOC_STREAMOFF, &typeout) < 0){
	   perror("VIDIOC_STREAMOFF OUT");
	   exit(1);
	   }

	// dealocate buffers
	munmap(bufferin_start, bufferin.length);
	munmap(bufferout_start, bufferout.length);

	close(fd);
	return 0;
}
