/*
 *  Copyright 2018 NXP
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or late.
 *
 */

/*
 * @file mxc_vpu_enc.c
 * Description: V4L2 driver encoder utility
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/ioctl.h>
#include <mntent.h>
#include <signal.h>
#include <asm/types.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include <pthread.h>

#include <linux/version.h>
#include <linux/types.h>

#include <linux/videodev2.h>

#include "windsor_encoder.h"
#include "mxc_v4l2.h"

#define MODULE_NAME		"mxc_vpu_enc.out"
#define MODULE_VERSION		"0.1"

#ifndef max
#define max(a,b)		(((a) < (b)) ? (b) : (a))
#endif //!max

#define DQEVENT
#define MAX_SUPPORTED_COMPONENTS	5

struct enc_size {
	uint32_t width;
	uint32_t height;
};

struct enc_point {
	uint32_t x;
	uint32_t y;
};

struct mxc_vpu_enc_param {
	uint32_t profile;
	uint32_t level;
	uint32_t framerate;
	uint32_t stride;
	struct enc_size src;
	struct enc_point offset;
	struct enc_size crop;
	struct enc_size out;
	uint32_t iframe_interval;
	uint32_t gop;
	uint32_t low_latency_mode;
	uint32_t bitrate_mode;
	uint32_t target_bitrate;
	uint32_t max_bitrate;
	uint32_t min_bitrate;
	uint32_t qp;
	struct v4l2_rect rect;
};

struct mxc_vpu_enc_option {
	const char *name;
	uint32_t arg_num;
	uint32_t key;
	const char *desc;
	const char *detail;
};

#define ENC_OPTION(key, num, desc, detail)	{#key, num, key, desc, detail}

enum {
	IFILE = 0,
	OFILE,
	IFMT,
	OFMT,
	WIDTH,
	HEIGHT,
	OWIDTH,
	OHEIGHT,
	PROFILE,
	LEVEL,
	GOP,
	QP,
	TARBR,
	MAXBR,
	MINBR,
	FRAMERATE,
	LOWLATENCY,
	FRAMENUM,
	LOOP,
	CROP,
};

struct mxc_vpu_enc_option options[] = {
	ENC_OPTION(IFILE, 1, "input filename", "input file"),
	ENC_OPTION(OFILE, 1, "output filename", "output file"),
	ENC_OPTION(IFMT, 1, "input format", "input format"),
	ENC_OPTION(OFMT, 1, "output format", "output format"),
	ENC_OPTION(WIDTH, 1, "set input file width", "input file width"),
	ENC_OPTION(HEIGHT, 1, "set input file height", "input file height"),
	ENC_OPTION(OWIDTH, 1, "set output file width", "output file width"),
	ENC_OPTION(OHEIGHT, 1, "set output file height", "output file height"),
	ENC_OPTION(PROFILE, 1, "set h264 profile", "h264 profile, 0 : baseline, 2 : main, 4 : high"),
	ENC_OPTION(LEVEL, 1, "set h264 level", "h264 level, 0~15, 14:level_5_0(default)"),
	ENC_OPTION(GOP, 1, "set group of picture", " Group of picture"),
	ENC_OPTION(QP, 1, "set quantizer parameter", "quantizer parameter,between 0 and 51.The smaller the value,the finer the quantization,the higher the image quality,the longer the code stream"),
	ENC_OPTION(TARBR, 1, "set encoder target boudrate", "target boudrate"),
	ENC_OPTION(MAXBR, 1, "set encoder maximum boudrate", "maximum boudrate"),
	ENC_OPTION(MINBR, 1, "set encoder minimum boudrate", "minimum boudrate"),
	ENC_OPTION(FRAMERATE, 1, "frame rate(fps)", "frame rate (fps)"),
	ENC_OPTION(LOWLATENCY, 0, "enable low latency mode", "enable low latency mode, it will disable the display re-ordering"),
	ENC_OPTION(FRAMENUM, 1, "set output frame number", "output frame number"),
	ENC_OPTION(LOOP, 1, "set application in loops", "set application in loops and no output file"),
	ENC_OPTION(CROP, 4, "<left> <top> <width> <height>, set crop info", "set crop position and size"),
	{NULL, 0, 0, NULL, NULL}
};

volatile unsigned int g_unCtrlCReceived = 0;
unsigned  long time_total = 0;
unsigned int num_total = 0;
unsigned int frame_count = 0;

unsigned int setFrameNum = 0;
volatile unsigned int loopFlag = 0;
volatile unsigned int loopSync = 0;
pthread_mutex_t lockSync = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condSync = PTHREAD_COND_INITIALIZER;
unsigned int loopCount = 0;

struct  timeval start;
struct  timeval end;
unsigned int TarBitrate = 0;
unsigned int profile = 0;// BP = 0, MP = 2, HP = 4

/**the function is used for to convert yuv420p(I420) to yuv420sp(NV12)
 * yyyy yyyy
 * uu vv
 * ->
 * yyyy yyyy
 * uv uv
 **/
void convert_feed_stream(component_t *pComponent, unsigned char *buf)
{
	unsigned char *u_start;
	unsigned char *v_start;
	unsigned char *uv_temp;
	unsigned int i, j;
	unsigned int height = pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nHeight;
	unsigned int width = pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nWidth;
	unsigned int uv_size;
	uv_size = height * width / 2;
	uv_temp = (unsigned char *)malloc(sizeof(unsigned char) * uv_size);

	u_start = buf;
	v_start = u_start + uv_size / 2;
	for (i = 0, j = 0; j < uv_size; j += 2, i++) {
		uv_temp[j] = u_start[i];
		uv_temp[j + 1] = v_start[i];
	}
	memcpy(u_start, uv_temp, sizeof(unsigned char)*uv_size);

	free(uv_temp);
}

static void SigStopHanlder(int Signal)
{
	printf("%s()\n", __FUNCTION__);
	return;
}

static void SigContHanlder(int Signal)
{
	printf("%s()\n", __FUNCTION__);
	return;
}

int kbhit(void)
{
	struct timeval tv;
	fd_set	rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);

	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}

int is_encode_node(int fd)
{
	struct v4l2_capability cap;
	int ret;

	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	if (ret) {
		printf("QUERYCAP fail: %s\n", strerror(errno));
		return -1;
	}

	if (strcasecmp((const char *)cap.driver, "vpu encoder"))
		return -1;
	return 0;
}

int lookup_video_device_node(char *pszDeviceName)
{
	int hDev;
	int lErr;
	int nCnt = 0;

	if (!pszDeviceName)
		return -1;

	for (nCnt = 0; nCnt < 64; nCnt++) {
		sprintf(pszDeviceName, "/dev/video%d", (nCnt + 13) % 64);

		hDev = open(pszDeviceName, O_RDWR);
		if (hDev < 0)
			continue;
		lErr = is_encode_node(hDev);
		close(hDev);
		hDev = -1;

		if (lErr == 0)
			return 0;
	}

	*pszDeviceName = 0;
	return (-1);
}

void test_streamout(component_t *pComponent)
{
	int lErr = 0;
	FILE *fpOutput = 0;

	struct zvapp_v4l_buf_info *stAppV4lBuf =
				pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf;
	struct v4l2_buffer stV4lBuf;
	struct v4l2_decoder_cmd v4l2cmd;
	struct v4l2_plane stV4lPlanes[3];
	int nV4lBufCnt;
	unsigned int ulXferBufCnt = 0;
	int frame_nb;
	fd_set fds;
	struct timeval tv;
	int r;
	struct v4l2_event evt;
	int i;
	int OutputType;

	printf("%s() [\n", __FUNCTION__);

	frame_nb = pComponent->ports[STREAM_DIR_OUT].buf_count;

	/***********************************************
	** 1> Open output file descriptor
	***********************************************/
	if (loopFlag == 0) {
		if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT) {
			fpOutput = fopen(pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr, "w+");
			if (fpOutput == NULL) {
				printf("%s() Unable to open file %s.\n", __FUNCTION__,
				       pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr);
				lErr = 1;
				goto FUNC_END;
			}
		} else if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_NULL_OUT) {
			// output to null
		} else {
			printf("%s() Unknown media type %d.\n", __FUNCTION__,
			       pComponent->ports[STREAM_DIR_OUT].eMediaType);
			goto FUNC_END;
		}
	}

OUTPUT_StreamOn:
	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived) {
		if (pComponent->ports[STREAM_DIR_OUT].unStarted) {
			/***********************************************
			** QBUF, send all the buffers to driver
			***********************************************/
			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++) {
				if (!stAppV4lBuf[nV4lBufCnt].sent) {
					for (i = 0; i < stAppV4lBuf[nV4lBufCnt].stV4lBuf.length; i++) {
						stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused = 0;
						stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset = 0;
					}
					lErr = ioctl(pComponent->hDev, VIDIOC_QBUF, &stAppV4lBuf[nV4lBufCnt].stV4lBuf);
					if (lErr) {
						printf("%s() QBUF ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
						if (errno == EAGAIN) {
							lErr = 0;
						}
						break;
					} else {
						stAppV4lBuf[nV4lBufCnt].sent = 1;
					}
				}
			}

			/***********************************************
			** DQBUF, get buffer from driver
			***********************************************/

			FD_ZERO(&fds);
			FD_SET(pComponent->hDev, &fds);

			// Timeout
			tv.tv_sec = 2;
			tv.tv_usec = 0;

			r = select(pComponent->hDev + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) {
				fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
				continue;
			}
			if (0 == r) {
				FD_ZERO(&fds);
				FD_SET(pComponent->hDev, &fds);

				// Timeout
				tv.tv_sec = 2;
				tv.tv_usec = 0;

				r = select(pComponent->hDev + 1, NULL, NULL, &fds, &tv);
				if (-1 == r) {
					fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
					continue;
				}
				if (0 == r) {
					fprintf(stderr, "%s() select timeout\n", __FUNCTION__);
					continue;
				}

				memset(&evt, 0, sizeof(struct v4l2_event));
				lErr = ioctl(pComponent->hDev, VIDIOC_DQEVENT, &evt);
				if (lErr) {
					printf("%s() VIDIOC_DQEVENT ioctl failed %d %s\n", __FUNCTION__, errno,
					       strerror(errno));
					continue;
				} else {
					if (evt.type == V4L2_EVENT_EOS) {
						if (loopFlag == 0) {
							g_unCtrlCReceived = 1;
							printf("EOS received\n");
							gettimeofday(&end, NULL);
							goto FUNC_END;
						} else {
							printf("EOS received\n");
							gettimeofday(&end, NULL);
							goto OUTPUT_LOOP;
						}
					} else
						printf("%s() VIDIOC_DQEVENT type=%d\n", __FUNCTION__, evt.type);
					continue;
				}
			}

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.m.planes = stV4lPlanes;
			stV4lBuf.length = 2;

			lErr = ioctl(pComponent->hDev, VIDIOC_DQBUF, &stV4lBuf);
			if (!lErr) {
				char *pBuf;

				stAppV4lBuf[stV4lBuf.index].sent = 0;

				if (loopFlag == 0) {
					printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r",
					       pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused,
					       stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags,
					       stV4lBuf.timestamp.tv_sec);
				} else {
					if (setFrameNum != 0) {
						if (frame_count < setFrameNum) {
							printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r",
							       pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused,
							       stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags,
							       stV4lBuf.timestamp.tv_sec);
						}
					} else
						printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r",
						       pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused,
						       stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags,
						       stV4lBuf.timestamp.tv_sec);
				}
				frame_count++;
				if (frame_count == setFrameNum) {
					printf("setFrameNum : %d , frame_count : %d\n", setFrameNum, frame_count);
					if (loopFlag == 0) {
						g_unCtrlCReceived = 1;
						v4l2cmd.cmd = V4L2_ENC_CMD_STOP;
						lErr = ioctl(pComponent->hDev, VIDIOC_ENCODER_CMD, &v4l2cmd);
						if (lErr)
							printf("VIDIOC_ENCODER_CMD has error\n");
						goto FUNC_END;
					} else {
						v4l2cmd.cmd = V4L2_ENC_CMD_STOP;
						lErr = ioctl(pComponent->hDev, VIDIOC_ENCODER_CMD, &v4l2cmd);
						if (lErr)
							printf("VIDIOC_ENCODER_CMD has error\n");
					}
				}

				if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT) {
					if (loopFlag == 0) {
						unsigned int bytesused;
						bytesused = stV4lBuf.m.planes[0].bytesused;
						for (i = 0; i < stV4lBuf.length; i++) {

							pBuf = stAppV4lBuf[stV4lBuf.index].addr[i] + stV4lBuf.m.planes[i].data_offset;
							fwrite((void*)pBuf, 1, bytesused, fpOutput);

						}
					}
				} else if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_NULL_OUT) {
				}
				fflush(stdout);
			} else {
				if (errno == EAGAIN) {
					lErr = 0;
				} else {
					printf("\r%s()  DQBUF failed(%d) errno(%d)\n", __FUNCTION__, lErr, errno);
				}
			}

			if (pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived) {
				printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
				break;
			}

			usleep(1000);
		}
	}

OUTPUT_LOOP:
	if (loopFlag == 1) {
		usleep(10000);

		loopCount++;
		printf("output thread is %d loop times.\n", loopCount);
		if (pComponent->ports[STREAM_DIR_OUT].opened) {
			OutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			lErr = ioctl(pComponent->hDev, VIDIOC_STREAMOFF, &OutputType);
			if (lErr != 0) {
				printf("output thread streamoff error!!\n");
			}
		}
		loopSync = 1;
		pthread_mutex_lock(&lockSync);
		pthread_cond_wait(&condSync, &lockSync);
		pthread_mutex_unlock(&lockSync);
		if (pComponent->ports[STREAM_DIR_OUT].opened) {
			OutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &OutputType);
			if (lErr != 0) {
				printf("output thread streamon error!!\n");
			}
		}
		frame_count = 0;
		ulXferBufCnt = 0;
		for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++) {
			stAppV4lBuf[nV4lBufCnt].sent = 0;
		}
		goto
		OUTPUT_StreamOn;
	}

FUNC_END:
	printf("\n");
	usleep(10000);

	if (fpOutput) {
		fclose(fpOutput);
		fpOutput = NULL;
	}

	pComponent->ports[STREAM_DIR_OUT].unStarted = 0;

	printf("%s() ]\n", __FUNCTION__);
}



void test_streamin(component_t *pComponent)
{
	int lErr = 0;
	FILE *fpInput = 0;

	struct zvapp_v4l_buf_info *stAppV4lBuf =
				pComponent->ports[STREAM_DIR_IN].stAppV4lBuf;
	struct v4l2_buffer stV4lBuf;
	struct v4l2_plane stV4lPlanes[3];
	struct v4l2_buffer *pstV4lBuf = NULL;
	int nV4lBufCnt;
	struct v4l2_decoder_cmd v4l2cmd;
	unsigned int ulIOBlockSize;
	int frame_size;
	int frame_nb;
	fd_set fds;
	struct timeval tv;
	int r;
	int i;
	unsigned int total;
	int first = pComponent->ports[STREAM_DIR_IN].buf_count;
	long file_size;
	int InputType;

	printf("%s() [\n", __FUNCTION__);

	frame_nb = pComponent->ports[STREAM_DIR_IN].buf_count;
	ulIOBlockSize = frame_size = pComponent->ports[STREAM_DIR_IN].frame_size;

	/***********************************************
	** 1> Open output file descriptor
	***********************************************/
	if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) {
		fpInput = fopen(pComponent->ports[STREAM_DIR_IN].pszNameOrAddr, "r+");
		if (fpInput == NULL) {
			printf("%s() Unable to open file %s.\n", __FUNCTION__,
			       pComponent->ports[STREAM_DIR_IN].pszNameOrAddr);
			lErr = 1;
			goto FUNC_END;
		} else {
			fseek(fpInput, 0, SEEK_END);
			file_size = ftell(fpInput);
			fseek(fpInput, 0, SEEK_SET);
		}
	} else {
		goto FUNC_END;
	}

INPUT_StreamOn:
	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!pComponent->ports[STREAM_DIR_IN].unCtrlCReceived) {
		if (loopSync == 1) {
			goto INPUT_LOOP;
		}
		if ((pComponent->ports[STREAM_DIR_IN].unStarted) &&
		    ((first > 0) || pComponent->ports[STREAM_DIR_OUT].unStarted)
		   ) {
			int buf_avail = 0;

			if (first > 0) {
				first--;
			}

			/***********************************************
			** DQBUF, get buffer from driver
			***********************************************/

			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++) {
				if (!stAppV4lBuf[nV4lBufCnt].sent) {
					buf_avail = 1;
					break;
				}
			}

			if (!buf_avail) {
				FD_ZERO(&fds);
				FD_SET(pComponent->hDev, &fds);

				// Timeout
				tv.tv_sec = 2;
				tv.tv_usec = 0;
				r = select(pComponent->hDev + 1, NULL, &fds, NULL, &tv);

				if (-1 == r) {
					fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
					continue;
				}
				if (0 == r) {
					fprintf(stderr, "%s() select timeout\n", __FUNCTION__);
					continue;
				}

				stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
				stV4lBuf.memory = V4L2_MEMORY_MMAP;
				stV4lBuf.m.planes = stV4lPlanes;
				stV4lBuf.length = 2;
				lErr = ioctl(pComponent->hDev, VIDIOC_DQBUF, &stV4lBuf);
				if (!lErr) {
					stAppV4lBuf[stV4lBuf.index].sent = 0;
				}
			}

			if (lErr) {
				if (errno == EAGAIN) {
					lErr = 0;
				}
			}

			/***********************************************
			** get empty buffer and read data
			***********************************************/
			pstV4lBuf = NULL;

			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++) {
				if (!stAppV4lBuf[nV4lBufCnt].sent) {
					pstV4lBuf = &stAppV4lBuf[nV4lBufCnt].stV4lBuf;
					break;
				}
			}

			if (pstV4lBuf) {
				char            *pBuf;
				unsigned int    block_size;

				if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) {
					for (i = 0; i < pstV4lBuf->length; i++) {
						pBuf = stAppV4lBuf[pstV4lBuf->index].addr[i];
						block_size = stAppV4lBuf[pstV4lBuf->index].size[i];
						pstV4lBuf->m.planes[i].bytesused = fread((void*)pBuf, 1, block_size, fpInput);
						if (i == 1)
							convert_feed_stream(pComponent, (unsigned char *)pBuf);
						pstV4lBuf->m.planes[i].data_offset = 0;
						file_size -= pstV4lBuf->m.planes[i].bytesused;
						if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory) {
							msync((void*)pBuf, block_size, MS_SYNC);
						}
					}
				}

				total = 0;
				ulIOBlockSize = 0;
				for (i = 0; i < pstV4lBuf->length; i++) {
					total += pstV4lBuf->m.planes[i].bytesused;
					ulIOBlockSize += stAppV4lBuf[pstV4lBuf->index].size[i];
				}

				if ((pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) &&
				    (total != ulIOBlockSize)
				   ) {
					if ((pComponent->ports[STREAM_DIR_IN].portType == COMPONENT_PORT_COMP_IN) ||
					    (pComponent->ports[STREAM_DIR_IN].portType == COMPONENT_PORT_YUV_IN)
					   ) {
						if (pComponent->ports[STREAM_DIR_IN].auto_rewind) {
							fseek(fpInput, 0, SEEK_SET);
						}
					} else {
						pComponent->ports[STREAM_DIR_IN].unCtrlCReceived = 1;
					}
				}

				if (total != 0) {
					memcpy(&stV4lBuf, pstV4lBuf, sizeof(struct v4l2_buffer));

					if (pComponent->ports[STREAM_DIR_IN].unUserPTS) {
						struct timespec now;
						clock_gettime (CLOCK_MONOTONIC, &now);
						stV4lBuf.timestamp.tv_sec = now.tv_sec;
						stV4lBuf.timestamp.tv_usec = now.tv_nsec / 1000;
						stV4lBuf.flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
					} else {
						// not use PTS
						stV4lBuf.flags &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
					}

					/***********************************************
					** QBUF, put data to driver
					***********************************************/
					lErr = ioctl(pComponent->hDev, VIDIOC_QBUF, &stV4lBuf);
					if (lErr) {
						printf("%s() QBUF ioctl failed %d %s ", __FUNCTION__, errno, strerror(errno));
						if (errno == EAGAIN) {
							printf("\n");
							lErr = 0;
						} else {
							printf("v4l_buf index(%d) type(%d) memory(%d) sequence(%d) length(%d) planes(%p)\n",
							       stV4lBuf.index,
							       stV4lBuf.type,
							       stV4lBuf.memory,
							       stV4lBuf.sequence,
							       stV4lBuf.length,
							       stV4lBuf.m.planes
							      );
						}
					} else {
						stAppV4lBuf[stV4lBuf.index].sent = 1;
					}
				} else {
					if (pComponent->ports[STREAM_DIR_IN].unCtrlCReceived) {
						printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
						break;
					}

					if (file_size == 0) {
						v4l2cmd.cmd = V4L2_ENC_CMD_STOP;
						lErr = ioctl(pComponent->hDev, VIDIOC_ENCODER_CMD, &v4l2cmd);
						if (lErr)
							printf("VIDIOC_ENCODER_CMD has error\n");
						file_size = -1;
					}

					usleep(1000);
				}

				fflush(stdout);
			}

			if (pComponent->ports[STREAM_DIR_IN].unCtrlCReceived) {
				printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
				break;
			}

			if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) {
				usleep(1000);
			} else {
				usleep(1000);
			}
		} else {
			usleep(30000);
		}
	}

INPUT_LOOP:
	if (loopSync == 1) {
		printf("input thread is %d loops\n", loopCount);
		if (pComponent->ports[STREAM_DIR_IN].opened) {
			InputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			lErr = ioctl(pComponent->hDev, VIDIOC_STREAMOFF, &InputType);
			if (lErr == -1) {
				printf("input streamoff error!!\n");
			}
		}
		fseek(fpInput, 0, SEEK_END);
		file_size = ftell(fpInput);
		if (0 != fseek(fpInput, 0, SEEK_SET))
			printf("fseek error\n");

		if (pComponent->ports[STREAM_DIR_IN].opened) {
			InputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &InputType);
			if (lErr == -1) {
				printf("input streamon error!!\n");
			}
		}
		first = pComponent->ports[STREAM_DIR_IN].buf_count;
		for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++) {
			stAppV4lBuf[nV4lBufCnt].sent = 0;
		}
		loopSync = 0;
		pthread_mutex_lock(&lockSync);
		pthread_cond_signal(&condSync);
		pthread_mutex_unlock(&lockSync);
		goto INPUT_StreamOn;
	}


FUNC_END:

	printf("\n");

	usleep(10000);

	if (fpInput) {
		fclose(fpInput);
	}

	pComponent->ports[STREAM_DIR_IN].unStarted = 0;

	//return lErr;
	printf("%s() ]\n", __FUNCTION__);
}

static int set_fps(int fd, uint32_t type, uint32_t framerate)
{
	struct v4l2_streamparm parm;
	int ret;

	if (fd < 0 || !framerate)
		return -1;

	memset(&parm, 0, sizeof(parm));
	parm.type = type;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = framerate;

	ret = ioctl(fd, VIDIOC_S_PARM, &parm);
	if (ret) {
		printf("set framerate fail, %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static int set_ctrl(int fd, int id, int value)
{
	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;
	int ret;

	memset(&qctrl, 0, sizeof(qctrl));
	qctrl.id = id;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, &qctrl);
	if (ret) {
		printf("query ctrl(%d) fail, %s\n", id, strerror(errno));
		return ret;
	}

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ctrl.value = value;
	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret) {
		printf("VIDIOC_S_CTRL(%s : %d) fail, %s\n",
		       qctrl.name, value, strerror(errno));
		return ret;
	}

	return 0;
}

static int set_crop(int fd, struct mxc_vpu_enc_param *param)
{
	struct v4l2_crop crop;
	int ret;

	if (fd < 0 || !param)
		return -1;

	if (!param->rect.width || !param->rect.height)
		return 0;

	crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	crop.c.left = param->rect.left;
	crop.c.top = param->rect.top;
	crop.c.width = param->rect.width;
	crop.c.height = param->rect.height;

	ret = ioctl(fd, VIDIOC_S_CROP, &crop);
	if (ret) {
		printf("set crop fail\n");
		return -1;
	}

	return 0;
}

static void set_encoder_parameters(struct mxc_vpu_enc_param *param,
				   component_t *pComponent)
{
	int fd;

	if (!pComponent || pComponent->hDev < 0)
		return;

	fd = pComponent->hDev;
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE, param->profile);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_LEVEL, param->level);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE_MODE, param->bitrate_mode);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_BITRATE, param->target_bitrate);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, param->gop);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, param->gop);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, param->qp);
	set_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_ASO, param->low_latency_mode);
}

static struct mxc_vpu_enc_option *find_option_by_name(const char *name)
{
	int i = 0;

	if (!name)
		return NULL;

	while (1) {
		if (!options[i].name)
			break;
		if (!strcasecmp(options[i].name, name))
			return &options[i];
		i++;
	}

	return NULL;
}

static int parse_arg(struct mxc_vpu_enc_option *option, char *argv[],
		     struct mxc_vpu_enc_param *param,
		     stream_media_t *in, stream_media_t *out)
{
	out->eMediaType = MEDIA_NULL_OUT;

	switch (option->key) {
	case IFILE:
		in->eMediaType = MEDIA_FILE_IN;
		in->pszNameOrAddr = argv[0];
		break;
	case OFILE:
		out->eMediaType = MEDIA_FILE_OUT;
		out->pszNameOrAddr = argv[0];
		break;
	case IFMT:
		in->fmt = strtol(argv[0], NULL, 0);
		break;
	case OFMT:
		out->fmt = strtol(argv[0], NULL, 0);
		break;
	case WIDTH:
		param->src.width = strtol(argv[0], NULL, 0);
		break;
	case HEIGHT:
		param->src.height = strtol(argv[0], NULL, 0);
		break;
	case OWIDTH:
		param->out.width = strtol(argv[0], NULL, 0);
		break;
	case OHEIGHT:
		param->out.height = strtol(argv[0], NULL, 0);
		break;
	case PROFILE:
		param->profile = strtol(argv[0], NULL, 0);
		break;
	case LEVEL:
		param->level = strtol(argv[0], NULL, 0);
		break;
	case GOP:
		param->gop = strtol(argv[0], NULL, 0);
		break;
	case QP:
		param->qp = strtol(argv[0], NULL, 0);
		break;
	case TARBR:
		param->target_bitrate = strtol(argv[0], NULL, 0);
		break;
	case MAXBR:
		param->max_bitrate = strtol(argv[0], NULL, 0);
		break;
	case MINBR:
		param->min_bitrate = strtol(argv[0], NULL, 0);
		break;
	case FRAMERATE:
		param->framerate = strtol(argv[0], NULL, 0);
		break;
	case LOWLATENCY:
		param->low_latency_mode = 1;
		break;
	case FRAMENUM:
		setFrameNum = strtol(argv[0], NULL, 0);
		break;
	case LOOP:
		loopFlag = 1;
		break;
	case CROP:
		param->rect.left = strtol(argv[0], NULL, 0);
		param->rect.top = strtol(argv[1], NULL, 0);
		param->rect.width = strtol(argv[2], NULL, 0);
		param->rect.height = strtol(argv[3], NULL, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int parse_args(int argc, char *argv[], struct mxc_vpu_enc_param *param,
		      stream_media_t *in, stream_media_t *out)
{
	struct mxc_vpu_enc_option *option;
	int index = 1;

	if (!param || !in || !out)
		return -1;

	param->profile = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	param->level = V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
	param->framerate = 30;
	param->gop = param->iframe_interval = 30;
	param->qp = 25;
	param->bitrate_mode = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
	param->target_bitrate = 4096;
	param->low_latency_mode = 0;
	param->src.width = 1920;
	param->src.height = 1080;

	while (index < argc) {
		option = find_option_by_name(argv[index]);
		if (!option) {
			printf("unknown parameter : %s\n", argv[index]);
			return -1;
		}
		if (index + option->arg_num >= argc) {
			printf("%s need %d arguments\n",
				argv[index], option->arg_num);
			return -1;
		}

		parse_arg(option, option->arg_num ? argv + index + 1 : NULL,
			  param, in, out);
		index += (1 + option->arg_num);
	}

	if (param->rect.left + param->rect.width > param->src.width) {
		param->rect.left = 0;
		param->rect.width = param->src.width;
	}
	if (!param->out.width) {
		if (!param->rect.width)
			param->out.width = param->src.width;
		else
			param->out.width = param->rect.width;
	}
	if (param->out.width > param->src.width)
		param->out.width = param->src.width;

	if (param->rect.top + param->rect.height > param->src.height) {
		param->rect.top = 0;
		param->rect.height = param->src.height;
	}
	if (!param->out.height) {
		if (!param->rect.height)
			param->out.height = param->src.height;
		else
			param->out.height = param->rect.height;
	}
	if (param->out.height > param->src.height)
		param->out.height = param->src.height;
	param->stride = param->src.width;

	return 0;
}

static void show_all_args(void)
{
	int i = 0;

	printf("Type 'HELP' to see the list. ");
	printf("Type 'HELP NAME' to find out more about parameter 'NAME'.\n");

	while (1) {
		if (!options[i].name)
			break;

		printf("%-10s: %s\n", options[i].name, options[i].desc);
		i++;
	}
}

static void show_arg(char *name)
{
	struct mxc_vpu_enc_option *option;

	if (!name)
		return;
	option = find_option_by_name(name);
	if (!option) {
		printf("no help topics match %s,please try %s help\n",
		       name, MODULE_NAME);
		return;
	}

	printf("%s: parameter '%s' is  %s\n",
	       option->name, option->name, option->detail);
}

static int show_help(int argc, char *argv[])
{
	int index = 1;

	while (index < argc) {
		if (!strcasecmp("HELP", argv[index])) {
			if (index + 1 < argc)
				show_arg(argv[index + 1]);
			else
				show_all_args();

			return 1;
		}

		index++;
	}

	return 0;
}

static int zvconf(struct mxc_vpu_enc_param *param,
		  stream_media_t *in, stream_media_t *out)
{
	in->portType = COMPONENT_PORT_YUV_IN;
	in->openFormat.yuv.nWidth = param->src.width;
	in->openFormat.yuv.nHeight = param->src.height;
	in->openFormat.yuv.nBitCount = 12;
	in->openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
	in->openFormat.yuv.nFrameRate = param->framerate;
	in->frame_size = (param->src.width * param->src.height * 3) / 2;
	in->buf_count = 4;

	out->portType = COMPONENT_PORT_COMP_OUT;
	out->frame_size = 256 * 1024;
	out->buf_count = 4;

	return 0;
}

int main(int argc, char* argv[])
{
	int lErr = 0;
	int nCmdIdx = 0;
	component_t component[MAX_SUPPORTED_COMPONENTS];
	component_t *pComponent;
	int nType;
	int i, j, k;
	struct mxc_vpu_enc_param param;

	struct v4l2_buffer stV4lBuf;
	struct v4l2_plane stV4lPlanes[3];
	int nV4lBufCnt;
	struct v4l2_format format;
	struct v4l2_requestbuffers req_bufs;
	struct v4l2_event_subscription sub;
	float fps;

	//signal(SIGINT, SigIntHanlder);
	signal(SIGSTOP, SigStopHanlder);
	signal(SIGCONT, SigContHanlder);

	memset(&component[0], 0, sizeof(component));
	memset(&param, 0, sizeof(param));

	pComponent = &component[nCmdIdx];
	pComponent->ports[STREAM_DIR_OUT].fmt = 0xFFFFFFFF;

	if (argc == 1) {
		printf("ERROR: Lack of necessary parameters\n");
		return 0;
	}
	if (show_help(argc, argv))
		return 0;

	lErr = parse_args(argc, argv, &param,
			  &pComponent->ports[STREAM_DIR_IN],
			  &pComponent->ports[STREAM_DIR_OUT]);
	if (lErr)
		return lErr;

	// lookup and open the device
	lErr = lookup_video_device_node(component[nCmdIdx].szDevName);
	if (0 == lErr) {
		component[nCmdIdx].hDev = open(component[nCmdIdx].szDevName,
					       O_RDWR
					      );
		if (component[nCmdIdx].hDev <= 0) {
			printf("Unable to Open %s.\n", component[nCmdIdx].szDevName);
			lErr = 1;
			goto FUNC_END;
		}
	} else {
		printf("Unable to find device.\n");
		lErr = 2;
		goto FUNC_END;
	}

	// get the configuration
	lErr = zvconf(&param, &pComponent->ports[STREAM_DIR_IN],
		      &pComponent->ports[STREAM_DIR_OUT]);
	if (lErr) {
		printf("Unable to config device.\n");
		lErr = 3;
		goto FUNC_END;
	}

	// subsribe v4l2 events
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
#if 1
	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr) {
		printf("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_SOURCE_CHANGE) ioctl failed %d %s\n",
		       __FUNCTION__, errno, strerror(errno));
		lErr = 4;
		goto FUNC_END;
	}
#endif
	sub.type = V4L2_EVENT_EOS;
	lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr) {
		printf("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_EOS) ioctl failed %d %s\n",
		       __FUNCTION__, errno, strerror(errno));
		lErr = 5;
		goto FUNC_END;
	}

	//set v4l2 capture format (compressed data input)
	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;

	format.fmt.pix_mp.width = param.out.width;
	format.fmt.pix_mp.height = param.out.height;
	format.fmt.pix_mp.num_planes = 1;
	format.fmt.pix_mp.plane_fmt[0].sizeimage =
			pComponent->ports[STREAM_DIR_OUT].frame_size;

	lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	if (lErr) {
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno,
		       strerror(errno));
		lErr = 6;
		goto FUNC_END;
	}
	set_fps(pComponent->hDev,
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, param.framerate);
	// setup memory for v4l2 capture (encode stream output)
	// request number of buffer and memory type
	memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
	req_bufs.count = pComponent->ports[STREAM_DIR_OUT].buf_count;
	req_bufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_bufs.memory = V4L2_MEMORY_MMAP;

	lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr) {
		printf("%s() VIDIOC_REQBUFS V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ioctl failed %d %s\n",
		       __FUNCTION__, errno, strerror(errno));
		lErr = 7;
		goto FUNC_END;
	} else {
		printf("%s() VIDIOC_REQBUFS V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE %d\n",
		       __FUNCTION__, req_bufs.count);
	}

	// save memory type and actual buffer number
	pComponent->ports[STREAM_DIR_OUT].memory = req_bufs.memory;
	pComponent->ports[STREAM_DIR_OUT].buf_count = req_bufs.count;

	pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf =
			malloc(pComponent->ports[STREAM_DIR_OUT].buf_count * sizeof(
					       struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf) {
		printf("%s() Unable to allocate memory for V4L app structure \n", __FUNCTION__);
		lErr = 8;
		goto FUNC_END;
	}

	memset(pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf, 0,
	       pComponent->ports[STREAM_DIR_OUT].buf_count * sizeof(struct
								    zvapp_v4l_buf_info));

	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_OUT].memory) {
		// acquire buffer memory from the driver
		for (nV4lBufCnt = 0; nV4lBufCnt < pComponent->ports[STREAM_DIR_OUT].buf_count;
		     nV4lBufCnt++) {
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.index =
					nV4lBufCnt;
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.type =
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.bytesused =
					0;
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.memory =
					pComponent->ports[STREAM_DIR_OUT].memory;
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes =
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lPlanes;
			pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 1;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.index = nV4lBufCnt;
			stV4lBuf.m.planes = stV4lPlanes;
			stV4lBuf.length = 1;
			lErr = ioctl(pComponent->hDev, VIDIOC_QUERYBUF, &stV4lBuf);
			if (!lErr) {
				printf("%s() QUERYBUF(%d) buf_nb(%d)", __FUNCTION__, nV4lBufCnt,
				       stV4lBuf.length);
				for (i = 0; i < stV4lBuf.length; i++) {
					printf("(%x:%d) ", stV4lBuf.m.planes[i].m.mem_offset,
					       stV4lBuf.m.planes[i].length);

					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].size[i] =
							stV4lBuf.m.planes[i].length;
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0,
														 stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
														 pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
					if (pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] <= 0) {
						printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
						lErr = 9;
						goto FUNC_END;
					}
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].m.mem_offset
						= stV4lBuf.m.planes[i].m.mem_offset;
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused
						= 0;
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].length
						= stV4lBuf.m.planes[i].length;
					pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset
						= 0;
				}
				printf("\n");
			} else {
				printf("%s() VIDIOC_QUERYBUF failed index=%d \n", __FUNCTION__, nV4lBufCnt);
				lErr = 10;
				goto FUNC_END;
			}
		}
	}

	// this port is opened if the buffers are allocated
	pComponent->ports[STREAM_DIR_OUT].opened = 1;

	// start the v4l2 capture streaming thread
	//
	printf("%s() want to create output thread\n", __FUNCTION__);
	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_OUT].memory) {
		lErr = pthread_create(&pComponent->ports[STREAM_DIR_OUT].threadId,
				      NULL,
				      (void *)test_streamout,
				      pComponent
				     );
	}
	if (!lErr) {
		pComponent->ports[STREAM_DIR_OUT].ulThreadCreated = 1;
	}

	// wait for 100 msec
	usleep(100000);

	// stream on v4l2 capture
	nType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &nType);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_OUT].unStarted = 1;
	} else {
		printf("%s() VIDIOC_STREAMON V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE failed errno(%d) %s\n",
		       __FUNCTION__, errno, strerror(errno));
		lErr = 11;
		goto FUNC_END;
	}

	// set v4l2 output format (yuv data input)
	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	format.fmt.pix_mp.num_planes = 2;
	format.fmt.pix_mp.width = param.src.width;
	format.fmt.pix_mp.height = param.src.height;
	for (i = 0; i < format.fmt.pix_mp.num_planes; i++)
		format.fmt.pix_mp.plane_fmt[i].bytesperline = param.src.width;
	format.fmt.pix_mp.plane_fmt[0].sizeimage =
			pComponent->ports[STREAM_DIR_IN].frame_size;

	printf("VIDIOC_S_FMT format %d\n", format.fmt.pix_mp.pixelformat);

	lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	if (lErr) {
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno,
		       strerror(errno));
		lErr = 12;
		goto FUNC_END;
	}

	set_crop(pComponent->hDev, &param);

	set_encoder_parameters(&param, pComponent);
	pComponent->ports[STREAM_DIR_IN].auto_rewind = ((VPU_PIX_FMT_LOGO ==
							 format.fmt.pix_mp.pixelformat) ? ZOE_TRUE : ZOE_FALSE);
	printf("auto_rewind %d, pixelformat %d\n",
	       pComponent->ports[STREAM_DIR_IN].auto_rewind, format.fmt.pix_mp.pixelformat);

	set_fps(pComponent->hDev,
			V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, param.framerate);
	// setup memory for v4l2 output (compressed data input)
	//
	// request number of buffer and memory type
	memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
	req_bufs.count = pComponent->ports[STREAM_DIR_IN].buf_count;
	req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_bufs.memory = V4L2_MEMORY_MMAP;
	lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr) {
		printf("%s() VIDIOC_REQBUFS ioctl failed %d %s\n", __FUNCTION__, errno,
		       strerror(errno));
		lErr = 13;
		goto FUNC_END;
	}

	// save memory type and actual buffer number
	pComponent->ports[STREAM_DIR_IN].memory = req_bufs.memory;
	pComponent->ports[STREAM_DIR_IN].buf_count = req_bufs.count;

	pComponent->ports[STREAM_DIR_IN].stAppV4lBuf = malloc(
								       pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf) {
		printf("%s() Unable to allocate memory for V4L app structure \n", __FUNCTION__);
		lErr = 14;
		goto FUNC_END;
	}

	memset(pComponent->ports[STREAM_DIR_IN].stAppV4lBuf, 0,
	       pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));

	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory) {
		// acquire buffer memory from the driver
		for (nV4lBufCnt = 0; nV4lBufCnt < pComponent->ports[STREAM_DIR_IN].buf_count;
		     nV4lBufCnt++) {
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.index =
					nV4lBufCnt;
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.type =
					V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.bytesused = 0;
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.memory =
					pComponent->ports[STREAM_DIR_IN].memory;
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes =
					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lPlanes;
			pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 2;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.index = nV4lBufCnt;
			stV4lBuf.m.planes = stV4lPlanes;
			stV4lBuf.length = 2;
			lErr = ioctl(pComponent->hDev, VIDIOC_QUERYBUF, &stV4lBuf);
			if (!lErr) {
				printf("%s() QUERYBUF(%d) buf_nb(%d)", __FUNCTION__, nV4lBufCnt,
				       stV4lBuf.length);
				for (i = 0; i < stV4lBuf.length; i++) {
					printf("(%x:%d) ", stV4lBuf.m.planes[i].m.mem_offset,
					       stV4lBuf.m.planes[i].length);

					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].size[i] =
							stV4lBuf.m.planes[i].length;
					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0,
														stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
														pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
					if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i]) {
						printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
						lErr = 15;
						goto FUNC_END;
					}

					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].m.mem_offset
						= stV4lBuf.m.planes[i].m.mem_offset;
					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused
						= 0;
					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].length
						= stV4lBuf.m.planes[i].length;
					pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset
						= 0;
				}
				printf("\n");
			} else {
				printf("%s() VIDIOC_QUERYBUF failed index=%d \n", __FUNCTION__, nV4lBufCnt);
				lErr = 16;
				goto FUNC_END;
			}
		}
	}

	// this port is opened if the buffers are allocated
	pComponent->ports[STREAM_DIR_IN].opened = 1;

	gettimeofday(&start, NULL);
	// start the v4l2 output streaming thread
	printf("%s() want to create input thread\n", __FUNCTION__);
	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory) {
		printf("%s () call test_streamin\n", __FUNCTION__);
		lErr = pthread_create(&pComponent->ports[STREAM_DIR_IN].threadId,
				      NULL,
				      (void *)test_streamin,
				      pComponent
				     );
	}
	if (!lErr) {
		pComponent->ports[STREAM_DIR_IN].ulThreadCreated = 1;
	}

	// wait for 100 msec
	usleep(100000);

	// stream on v4l2 output
	printf("start hDev(%d) V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE\n", pComponent->hDev);
	nType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &nType);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_IN].unStarted = 1;
	} else {
		printf("%s() VIDIOC_STREAMON V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE failed errno(%d) %s\n",
		       __FUNCTION__, errno, strerror(errno));
		lErr = 17;
		goto FUNC_END;
	}

	while (g_unCtrlCReceived == 0) {
		usleep(50000);
	}

	time_total += 1000000 * (end.tv_sec - start.tv_sec) + end.tv_usec -
		      start.tv_usec;
	fps = frame_count * 1000000.0 / time_total;
	printf("time_total=%f,frame_count=%d,fps=%f\n", time_total / 1000000.0,
	       frame_count, fps);

	// stop streaming
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--) {
		if (component[i].hDev > 0) {
			if (component[i].ports[STREAM_DIR_OUT].opened) {
				printf("stop(%d) hDev(%d) V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE\n", i,
				       component[i].hDev);
				nType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
				ioctl(component[i].hDev, VIDIOC_STREAMOFF, &nType);
				component[i].ports[STREAM_DIR_OUT].unSentStopCmd = 1;
				component[i].ports[STREAM_DIR_OUT].unCtrlCReceived = 1;
			}

			if (component[i].ports[STREAM_DIR_IN].opened) {
				printf("stop(%d) hDev(%d) V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE\n", i,
				       component[i].hDev);
				nType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
				ioctl(component[i].hDev, VIDIOC_STREAMOFF, &nType);
				component[i].ports[STREAM_DIR_IN].unSentStopCmd = 1;
				component[i].ports[STREAM_DIR_IN].unCtrlCReceived = 1;
			}
		}
	}

	// unsubscribe v4l2 events
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--) {
		if (component[i].hDev > 0) {
			sub.type = V4L2_EVENT_ALL;
			lErr = ioctl(component[i].hDev, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
			if (lErr) {
				printf("%s() VIDIOC_UNSUBSCRIBE_EVENT ioctl failed %d %s\n", __FUNCTION__,
				       errno, strerror(errno));
			}
		}
	}

FUNC_END:
	// propagate the signal to all the threads
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--) {
		for (j = 0; j < MAX_STREAM_DIR; j++) {
			component[i].ports[j].unCtrlCReceived = 1;
		}
	}

	// wait for 100 msec
	usleep(100000);

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		for (j = 0; j < MAX_STREAM_DIR; j++) {
			if (component[i].ports[j].ulThreadCreated) {
				pthread_join(component[i].ports[j].threadId,
					     NULL
					    );
				component[i].ports[j].ulThreadCreated = 0;
			}
		}
	}

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		for (j = 0; j < MAX_STREAM_DIR; j++) {
			if (V4L2_MEMORY_MMAP == component[i].ports[j].memory) {
				if (component[i].ports[j].stAppV4lBuf) {
					for (nV4lBufCnt = 0; nV4lBufCnt < component[i].ports[j].buf_count;
					     nV4lBufCnt++) {
						for (k = 0; k < 2; k++) {
							if (component[i].ports[j].stAppV4lBuf[nV4lBufCnt].addr[i]) {
								munmap(component[i].ports[j].stAppV4lBuf[nV4lBufCnt].addr[k],
								       component[i].ports[j].stAppV4lBuf[nV4lBufCnt].size[k]);
							}
						}
					}
					free((void *)component[i].ports[j].stAppV4lBuf);
				}
			}
		}
	}

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		for (j = 0; j < MAX_STREAM_DIR; j++) {
			// close ports
			if (component[i].ports[j].opened) {
				component[i].ports[j].opened = ZOE_FALSE;
			}
		}

		// close device
		if (component[i].hDev > 0) {
			close(component[i].hDev);
			component[i].hDev = 0;
		}
	}

	pthread_cond_destroy(&condSync);

	return (lErr);
}
