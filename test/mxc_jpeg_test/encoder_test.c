/*
 * Copyright (C) 2017 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-controls.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
void print_usage(void)
{
	printf("Usage: encoder_test -d </dev/videoX> -f <FILENAME.rgb>\n");
}

int main(int argc, char *argv[])
{
	int fd, i, j;
	char *video_device = 0;
	char *test_file = 0;

	if (argc != 5) {
		print_usage();
		return;
	}

	for (i = 1; i < 5; i += 2) {
		if (strcmp(argv[i], "-d") == 0)
			video_device = argv[i+1];
		else if (strcmp(argv[i], "-f") == 0)
			test_file = argv[i+1];
	}
	if (video_device == 0 || test_file == 0) {
		print_usage();
		return;
	}

	fd = open(video_device, O_RDWR);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	FILE *testrgb = fopen(test_file, "rb");
	fseek(testrgb, 0, SEEK_END);
	long filesize = ftell(testrgb);
	fseek(testrgb, 0, SEEK_SET);
	void *buf = malloc(filesize + 1);

	printf("\n FILE SIZE %ld \n", filesize);

	struct v4l2_capability capabilities;
	if (ioctl(fd, VIDIOC_QUERYCAP, &capabilities) < 0) {
		perror("VIDIOC_QUERYCAP");
		exit(1);
	}

	if (!(capabilities.capabilities & V4L2_CAP_VIDEO_M2M)) {
		fprintf(stderr, "The device does not handle single-planar video capture.\n");
		exit(1);
	}

	struct v4l2_format format;
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	format.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
	format.fmt.pix.width = 256;
	format.fmt.pix.height = 256;

	struct v4l2_format format2;
	format2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	format2.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
	format2.fmt.pix.width = 256;
	format2.fmt.pix.height = 256;

	printf("\nIOCTL VIDIOC_S_FMT\n");

	if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
		perror("VIDIOC_S_FMT");
		exit(1);
	}
	printf("\n2\n");
	if (ioctl(fd, VIDIOC_S_FMT, &format2) < 0) {
		perror("VIDIOC_S_FMT");
		exit(1);
	}

	struct v4l2_requestbuffers bufrequestin;
	struct v4l2_requestbuffers bufrequestout;

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
	bufferin.memory = V4L2_MEMORY_MMAP;
	bufferin.index = 0;

	bufferout.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufferout.memory = V4L2_MEMORY_MMAP;
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

	void *bufferin_start = mmap(
				    NULL,
				    bufferin.length,
				    PROT_READ | PROT_WRITE,
				    MAP_SHARED,
				    fd,
				    bufferin.m.offset
				   );

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
	fread(bufferout_start, filesize, 1, testrgb);
	fclose(testrgb);
	printf("BUFFER STARTS: %lx, %ld, other size %ld\n", *(long *)bufferin_start,
	       (unsigned long) bufferin.length, (unsigned long) bufferout.length);


	struct v4l2_buffer bufferin2;
	memset(&bufferin2, 0, sizeof(bufferin2));

	struct v4l2_buffer bufferout2;
	memset(&bufferout2, 0, sizeof(bufferout2));

	bufferin2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	bufferin2.memory = V4L2_MEMORY_MMAP;
	bufferin2.index = 0; /* Queueing buffer index 0. */

	bufferout2.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	bufferout2.memory = V4L2_MEMORY_MMAP;
	bufferout2.index = 0; /* Queueing buffer index 0. */

	printf("\n\nSTREAMON IN\n\n");

	// Activate streaming
	int typein = bufferin2.type;
	if (ioctl(fd, VIDIOC_STREAMON, &typein) < 0) {
		perror("VIDIOC_STREAMON IN");
		exit(1);
	}

	printf("\n\nSTREAMON OUT\n\n");

	int typeout = bufferout2.type;
	if (ioctl(fd, VIDIOC_STREAMON, &typeout) < 0) {
		perror("VIDIOC_STREAMON OUT");
		exit(1);
	}

	for (i = 0; i < 5; i++) {
		printf("\n\nQBUF IN\n\n");
		if (ioctl(fd, VIDIOC_QBUF, &bufferin2) < 0) {
			perror("VIDIOC_QBUF IN");
			exit(1);
		}

		printf("\n\nQBUF OUT\n\n");
		if (ioctl(fd, VIDIOC_QBUF, &bufferout2) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}

		printf("DONE BUF %d %lx %lx\n", i, *((long *)bufferout_start),
				*((long *)bufferin_start));

		/*	FILE* fout = fopen("outfile", "wb");
			fwrite(bufferin_start, bufferin.length, 1, fout);
			fclose(fout);*/
		for (j = 0; j < bufferin.length; j += 8) {
			printf("%02x %02x %02x %02x %02x %02x %02x %02x",
			       ((char *)bufferin_start)[j],
			       ((char *)bufferin_start)[j+1],
			       ((char *)bufferin_start)[j+2],
			       ((char *)bufferin_start)[j+3],
			       ((char *)bufferin_start)[j+4],
			       ((char *)bufferin_start)[j+5],
			       ((char *)bufferin_start)[j+6],
			       ((char *)bufferin_start)[j+7]);
		}

		printf("\n\nDQBUF OUT\n\n");

		if (ioctl(fd, VIDIOC_DQBUF, &bufferout2) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}
		if (ioctl(fd, VIDIOC_DQBUF, &bufferin2) < 0) {
			perror("VIDIOC_QBUF OUT");
			exit(1);
		}
	}


	/* Your loops end here. */

	// Deactivate streaming
	printf("\n\nOFF IN\n\n");
	/*
	   if(ioctl(fd, VIDIOC_STREAMOFF, &typein) < 0){
	   perror("VIDIOC_STREAMOFF IN");
	   exit(1);
	   }

	   printf("\n\nOFF OUT\n\n");
	   if(ioctl(fd, VIDIOC_STREAMOFF, &typeout) < 0){
	   perror("VIDIOC_STREAMOFF OUT");
	   exit(1);
	   }
	 */

	close(fd);
	return 0;
}
