/*
 *  Copyright 2018-2020 NXP
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or late.
 *
 */

/*
 * @file mxc_vpu_dec.c
 * Description: V4L2 driver decoder utility
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

#include "mxc_v4l2.h"

#define _TEST_MMAP

#define MODULE_NAME			"mxc_vpu_dec.out"
#define MODULE_VERSION		"0.1"

#ifndef max
#define max(a,b)        (((a) < (b)) ? (b) : (a))
#endif //!max

#define DQEVENT

#define LVL_BIT_WARN	(1 << 0)
#define LVL_BIT_EVENT	(1 << 1)
#define LVL_BIT_INFO	(1 << 2)
#define LVL_BIT_FPS		(1 << 3)

#define dec_err(fmt, arg...) pr_info("[Decoder Test] " fmt, ## arg)
#define dec_bit_dbg(bit, fmt, arg...) \
	do { \
		if (dec_dbg_bit & (bit)) \
			printf("[Decoder Test] " fmt, ## arg); \
	} while (0)

volatile int ret_err = 0;
volatile unsigned int g_unCtrlCReceived = 0;
unsigned long time_total = 0;
unsigned int num_total = 0;
struct timeval start;
struct timeval end;
volatile int loopTimes = 1;
volatile int preLoopTimes = 1;
volatile int initLoopTimes = 1;
int frame_done = 0;
pthread_mutex_t g_mutex;
int dec_dbg_bit = LVL_BIT_WARN;

static __u32 formats_compressed[] = {
	VPU_PIX_FMT_LOGO,	//VPU_VIDEO_UNDEFINED = 0,
	V4L2_PIX_FMT_H264,	//VPU_VIDEO_AVC = 1,     ///< https://en.wikipedia.org/wiki/H.264/MPEG-4_AVC
	V4L2_PIX_FMT_VC1_ANNEX_G,	//VPU_VIDEO_VC1_ANNEX_G = 2,     ///< https://en.wikipedia.org/wiki/VC-1
	V4L2_PIX_FMT_MPEG2,	//VPU_VIDEO_MPEG2 = 3,   ///< https://en.wikipedia.org/wiki/H.262/MPEG-2_Part_2
	VPU_PIX_FMT_AVS,	//VPU_VIDEO_AVS = 4,     ///< https://en.wikipedia.org/wiki/Audio_Video_Standard
	V4L2_PIX_FMT_MPEG4,	//VPU_VIDEO_ASP = 5,     ///< https://en.wikipedia.org/wiki/MPEG-4_Part_2
	V4L2_PIX_FMT_JPEG,	//VPU_VIDEO_JPEG = 6,    ///< https://en.wikipedia.org/wiki/JPEG
	VPU_PIX_FMT_RV8,	//VPU_VIDEO_RV8 = 7,     ///< https://en.wikipedia.org/wiki/RealVideo
	VPU_PIX_FMT_RV9,	//VPU_VIDEO_RV9 = 8,     ///< https://en.wikipedia.org/wiki/RealVideo
	VPU_PIX_FMT_VP6,	//VPU_VIDEO_VP6 = 9,     ///< https://en.wikipedia.org/wiki/VP6
	VPU_PIX_FMT_SPK,	//VPU_VIDEO_SPK = 10,    ///< https://en.wikipedia.org/wiki/Sorenson_Media#Sorenson_Spark
	V4L2_PIX_FMT_VP8,	//VPU_VIDEO_VP8 = 11,    ///< https://en.wikipedia.org/wiki/VP8
	V4L2_PIX_FMT_H264_MVC,	//VPU_VIDEO_AVC_MVC = 12,///< https://en.wikipedia.org/wiki/Multiview_Video_Coding
	VPU_PIX_FMT_HEVC,	//VPU_VIDEO_HEVC = 13,   ///< https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding
	VPU_PIX_FMT_DIVX,	//VPU_VIDEO_VC1_ANNEX_L = 14,     ///< https://en.wikipedia.org/wiki/VC-1
	V4L2_PIX_FMT_VC1_ANNEX_L,	//VPU_VIDEO_VC1_ANNEX_L = 15,     ///< https://en.wikipedia.org/wiki/VC-1
};

#define ZPU_NUM_FORMATS_COMPRESSED  SIZEOF_ARRAY(formats_compressed)

static __u32 formats_yuv[] = {
	V4L2_PIX_FMT_NV12,
	V4L2_PIX_FMT_YUV420M,
	VPU_PIX_FMT_TILED_8,
	VPU_PIX_FMT_TILED_10
};

#define ZPU_NUM_FORMATS_YUV SIZEOF_ARRAY(formats_yuv)

static void SigIntHanlder(int Signal)
{
	/*signal(SIGALRM, Signal); */
	g_unCtrlCReceived = 1;
	return;
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

void changemode(int dir)
{
	static struct termios oldt, newt;

	if (dir == 1) {
		tcgetattr(STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	} else {
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	}
}

int kbhit(void)
{
	struct timeval tv;
	fd_set rdfs;
	int ret;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);

	ret = select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	if (ret == -1) {
		printf("warnning: select stdin failed.\n");
	} else if (ret == 0) {
	} else {
		int size = 1024;
		char *buff = (char *)malloc(size);
		memset(buff, 0, size);
		if (NULL != fgets(buff, size, stdin)) {
			if (buff[strlen(buff) - 1] == '\n')
				buff[strlen(buff) - 1] = '\0';
			if (!strcasecmp(buff, "x") || !strcasecmp(buff, "stop")) {
				g_unCtrlCReceived = 1;
			}
		}
		free(buff);
	}
	return FD_ISSET(STDIN_FILENO, &rdfs);
}

int check_video_device(uint32_t devInstance,
		       uint32_t * p_busType, uint32_t * p_devType, char *devName)
{
	int fd;
	int lErr;
	struct v4l2_capability cap;

	fd = open(devName, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return -1;
	lErr = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	close(fd);

	if (lErr)
		return -1;

	if (strstr((const char *)cap.bus_info, "platform") &&
	    (strstr((const char *)cap.driver, "vpu B0")
	     || strstr((const char *)cap.driver, "vpu decoder"))) {
		*p_busType = 1;
		*p_devType = COMPONENT_TYPE_DECODER;
		return 0;
	}

	return -1;
}

int lookup_video_device_node(uint32_t devInstance,
			     uint32_t * p_busType, uint32_t * p_devType, char *pszDeviceName)
{
	int nCnt = 0;
	ZVDEV_INFO devInfo;
	int i;

	memset(&devInfo, 0xAA, sizeof(ZVDEV_INFO));

	for (i = 0; i < 64; i++) {
		sprintf(pszDeviceName, "/dev/video%d", i);
		if (!check_video_device(devInstance, p_busType, p_devType, pszDeviceName))
			break;
	}

	if (64 == nCnt) {
		// return empty device name
		*pszDeviceName = 0;
		return (-1);
	} else {
		return (0);
	}
}

int zvconf(component_t * pComponent, char *scrfilename, uint32_t type)
{
	// setup port type and open format
	switch (type) {
	case COMPONENT_TYPE_ENCODER:
		pComponent->ports[STREAM_DIR_IN].portType = COMPONENT_PORT_YUV_IN;
		pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nWidth = 1920;
		pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nHeight = 1088;
		pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nBitCount = 12;
		pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
		pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nFrameRate = 30;
		pComponent->ports[STREAM_DIR_IN].frame_size = (1920 * 1088 * 3) / 2;
		pComponent->ports[STREAM_DIR_IN].buf_count = 4;
		pComponent->ports[STREAM_DIR_OUT].portType = COMPONENT_PORT_COMP_OUT;
		pComponent->ports[STREAM_DIR_OUT].frame_size = 256 * 1024;
		pComponent->ports[STREAM_DIR_OUT].buf_count = 16;
		break;
	case COMPONENT_TYPE_CODEC:
	case COMPONENT_TYPE_DECODER:
	default:
		pComponent->ports[STREAM_DIR_IN].portType = COMPONENT_PORT_COMP_IN;
		if (pComponent->ports[STREAM_DIR_IN].frame_size <= 0)
			pComponent->ports[STREAM_DIR_IN].frame_size = 256 * 1024 * 3 + 1;
		if (pComponent->ports[STREAM_DIR_IN].buf_count <= 0)
			pComponent->ports[STREAM_DIR_IN].buf_count = 6;
		pComponent->ports[STREAM_DIR_OUT].portType = COMPONENT_PORT_YUV_OUT;
		pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nWidth = 1920;
		pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nHeight = 1088;
		pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nBitCount = 12;
		pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
		pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nFrameRate = 30;
		pComponent->ports[STREAM_DIR_OUT].frame_size = (1920 * 1088 * 3) / 2;
		if (pComponent->ports[STREAM_DIR_OUT].buf_count <= 0)
			pComponent->ports[STREAM_DIR_OUT].buf_count = 4;
		break;
	}
	pComponent->ulWidth = 1920;
	pComponent->ulHeight = 1088;
	return (0);
}

void usage(int nExit)
{
	printf("\n");
	printf("  [%s version %s]\n", MODULE_NAME, MODULE_VERSION);
	printf("  -------------------------------------------------\n");
	printf
	    ("  %s <command1> <arg1> <arg2> ... [+ <command2> <arg1> <arg2> ...] \n", MODULE_NAME);
	printf("\n");
	printf("  stream command,\n");
	printf("\n");
	printf("  arg for stream media,\n");
	printf("    ifile <file_name>       => input from file.\n");
	printf("    ifmt <fmt>              => input format 0 - 14.\n");
	printf("    ofmt <fmt>              => output format 0 - 4.\n");
	printf("    ofile <file_name>       => output to file.\n");
	printf("\n");
	printf("  examples, \n");
	printf("\n");

	if (nExit) {
		exit(nExit);
	}
}

static void convert_inter_2_prog_4_nv12(unsigned char *buffer,
					unsigned int width,
					unsigned int height,
					unsigned int luma_size, unsigned int chroma_size)
{
	unsigned char *src;
	unsigned char *dst;
	unsigned char *yTopSrc;
	unsigned char *yBotSrc;
	unsigned char *uvTopSrc;
	unsigned char *uvBotSrc;
	unsigned char *yDst;
	unsigned char *uvDst;
	int i;

	src = (unsigned char *)malloc(luma_size + chroma_size);
	memset(src, 0, luma_size + chroma_size);
	memcpy(src, buffer, luma_size + chroma_size);
	memset(buffer, 0, luma_size);

	yTopSrc = src;
	yBotSrc = src + luma_size / 2;
	uvTopSrc = src + luma_size;
	uvBotSrc = uvTopSrc + chroma_size / 2;
	dst = buffer;
	yDst = dst;
	uvDst = dst + luma_size;

	/*convert luma */
	for (i = 0; i < height; i++) {
		if (i & 0x1) {
			memcpy(yDst, yBotSrc, width);
			yBotSrc += width;
		} else {
			memcpy(yDst, yTopSrc, width);
			yTopSrc += width;
		}
		yDst += width;
	}

	/*convert chroma */
	for (i = 0; i < height / 2; i++) {
		if (i & 0x1) {
			memcpy(uvDst, uvBotSrc, width);
			uvBotSrc += width;
		} else {
			memcpy(uvDst, uvTopSrc, width);
			uvTopSrc += width;
		}
		uvDst += width;
	}

	free(src);
}

static int __ReadYUVFrame_FSL_8b(uint8_t * dst_addr, uint8_t * src_addr,
				 unsigned int width, unsigned int height,
				 unsigned int stride, int h_cnt, int v_cnt)
{
	int h_num, v_num;
	int i, j;
	unsigned int v_base_offset;
	uint8_t *cur_addr;
	uint8_t *inter_buf;
	int line_num;
	int line_base;

	inter_buf = (uint8_t *) malloc(8 * 128);
	if (!inter_buf) {
		printf("failed to alloc inter_buf\r\n");
		return -1;
	}

	for (v_num = 0; v_num < v_cnt; v_num++) {
		v_base_offset = stride * 128 * v_num;

		for (h_num = 0; h_num < h_cnt; h_num++) {
			cur_addr = (uint8_t *) (src_addr + h_num * (8 * 128) + v_base_offset);
			memcpy(inter_buf, cur_addr, 8 * 128);

			for (i = 0; i < 128; i++) {
				line_num = i + 128 * v_num;
				line_base = line_num * width;
				for (j = 0; j < 8; j++)
					dst_addr[line_base + (8 * h_num) + j] =
					    inter_buf[8 * i + j];
			}
		}
	}

	free(inter_buf);
	return 0;
}

// Read 8-bit FSL stored image
// Format is based on ratser array of tiles, where a tile is 1KB = 8x128
static unsigned int ReadYUVFrame_FSL_8b(unsigned int nPicWidth,
					unsigned int nPicHeight,
					unsigned int uVOffsetLuma,
					unsigned int uVOffsetChroma,
					unsigned int stride,
					uint8_t ** nBaseAddr,
					uint8_t * pDstBuffer, unsigned int bInterlaced)
{
	unsigned int luma_lines, chro_lines;
	uint8_t *dst_buf;
	uint8_t *base_addr;
	int h_cnt, v_cnt;

	dst_buf = pDstBuffer;
	luma_lines = nPicHeight;
	chro_lines = nPicHeight / 2;
	if (bInterlaced) {
		luma_lines = luma_lines / 2;
		chro_lines = chro_lines / 2;
	}

	/* read luma */
	base_addr = nBaseAddr[0];
	h_cnt = (nPicWidth + 7) / 8;
	v_cnt = (luma_lines + 127) / 128;

	__ReadYUVFrame_FSL_8b(dst_buf, base_addr, nPicWidth, nPicHeight, stride, h_cnt, v_cnt);
	if (bInterlaced) {
		/* Read Bot Luma */
		dst_buf += (nPicWidth * nPicHeight) / 2;
		__ReadYUVFrame_FSL_8b(dst_buf, base_addr, nPicWidth, nPicHeight,
				      stride, h_cnt, v_cnt);
		dst_buf += (nPicWidth * nPicHeight) / 2;
	} else {
		dst_buf += (nPicWidth * nPicHeight);
	}

	/* read chroma */
	base_addr = nBaseAddr[1];
	h_cnt = (nPicWidth + 7) / 8;
	v_cnt = (chro_lines + 127) / 128;
	__ReadYUVFrame_FSL_8b(dst_buf, base_addr, nPicWidth, nPicHeight, stride, h_cnt, v_cnt);
	if (bInterlaced) {
		/* Read Bot Chroma */
		dst_buf += (nPicWidth * nPicHeight) / 4;
		__ReadYUVFrame_FSL_8b(dst_buf, base_addr, nPicWidth, nPicHeight,
				      stride, h_cnt, v_cnt);
	}

	if (bInterlaced)
		convert_inter_2_prog_4_nv12(pDstBuffer, nPicWidth, nPicHeight,
					    nPicWidth * nPicHeight, nPicWidth * nPicHeight / 2);

	return 0;
}

/* PRECISION_8BIT: [0,1,2,3,4,5,6,7,8,9] -> [0,1,2,3,4,5,6,7]
 * 16-bit: [0,1,2,3,4,5,6,7,8,9] -> [0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9]
 */

#define ALGORITHM_10BIT 1

#if (ALGORITHM_10BIT == 1)

/* Algorithm: 1
 * frame buffer (10bit tiled) -> 128-line linear buffer (10bit) -> linear buffer (8bit)
 */
static int __ReadYUVFrame_FSL_10b(void *dst_addr, uint8_t * src_addr,
				  unsigned int width, unsigned int height,
				  unsigned int stride, int h_cnt, int v_cnt, unsigned int bitCnt)
{
	int h_num, v_num;
	int i, j, k, pix;
	unsigned int v_base_offset;
	uint8_t *cur_addr;
	uint8_t *inter_buf = NULL;
	int line_num;
	int line_base;
	uint8_t(*row_buf)[stride] = NULL;
	uint8_t *line_buf;
	int bit_pos, byte_loc, bit_loc;
	uint16_t byte_16;
	uint8_t *p_addr_8 = NULL;
	uint16_t *p_addr_16 = NULL;
	int ret = 0;

	if (bitCnt == 8)
		p_addr_8 = (uint8_t *) dst_addr;
	else
		p_addr_16 = (uint16_t *) dst_addr;

	inter_buf = (uint8_t *) malloc(8 * 128);
	if (!inter_buf) {
		printf("failed to alloc inter_buf\r\n");
		ret = -1;
		goto exit;
	}

	row_buf = malloc(128 * stride);
	if (!row_buf) {
		printf("failed to alloc row_buf\r\n");
		ret = -1;
		goto exit;
	}

	for (v_num = 0; v_num < v_cnt; v_num++) {
		v_base_offset = stride * 128 * v_num;

		for (h_num = 0; h_num < h_cnt; h_num++) {
			cur_addr = (uint8_t *) (src_addr + h_num * (8 * 128) + v_base_offset);
			memcpy(inter_buf, cur_addr, 8 * 128);

			for (i = 0; i < 128; i++) {
				for (j = 0; j < 8; j++)
					row_buf[i][8 * h_num + j] = inter_buf[8 * i + j];
			}
		}

		for (k = 0; k < 128; k++) {
			line_buf = row_buf[k];
			line_num = 128 * v_num + k;
			line_base = line_num * width;

			for (pix = 0; pix < width; pix++) {
				bit_pos = 10 * pix;
				byte_loc = bit_pos / 8;
				bit_loc = bit_pos % 8;
				byte_16 = line_buf[byte_loc] << 8 | line_buf[byte_loc + 1];
				if (bitCnt == 8) {
					p_addr_8[line_base + pix] = byte_16 >> (8 - bit_loc);
				} else {
					byte_16 = (byte_16 >> (6 - bit_loc));
					p_addr_16[line_base + pix] = byte_16 & 0x3ff;
				}
			}
		}
	}

	ret = 0;

exit:
	if (inter_buf)
		free(inter_buf);
	if (row_buf)
		free(row_buf);
	return ret;
}

#elif (ALGORITHM_10BIT == 2)
/* Algorithm 2:
 * frame buffer (10bit tiled) -> linear buffer (8bit), dst coordinate -> src
 */
static int __ReadYUVFrame_FSL_10b(void *dst_addr, uint8_t * src_addr,
				  unsigned int width, unsigned int height,
				  unsigned int stride, int h_cnt, int v_cnt, unsigned int bitCnt)
{
	int h_num, v_num;
	int sx, sy, dx, dy;
	int i, j;
	int i_in_src, j_in_src;
	int first_byte_pos, second_byte_pos;
	int dst_pos;
	int bit_pos, bit_loc;
	uint8_t first_byte, second_byte;
	uint16_t byte_16;
	uint8_t *dst_addr_8 = NULL;
	uint16_t *dst_addr_16 = NULL;

	if (bitCnt == 8)
		dst_addr_8 = (uint8_t *) dst_addr;
	else
		dst_addr_16 = (uint16_t *) dst_addr;

	/* transferd h_cnt calculated by ((nPicWidth * 10 / 8) + 7) / 8;
	 * not suitable for this algorithm
	 */
	h_cnt = (width + 7) / 8;

	for (v_num = 0; v_num < v_cnt; v_num++) {
		for (h_num = 0; h_num < h_cnt; h_num++) {
			for (i = 0; i < 128; i++) {
				for (j = 0; j < 8; j++) {
					dx = h_num * 8 + j;
					dy = v_num * 128 + i;
					sx = dx * 10 / 8;
					sy = dy;
					i_in_src = sy % 128;
					j_in_src = sx % 8;
					first_byte_pos =
					    (sy / 128) * (stride * 128) + (sx / 8) * (8 * 128) +
					    (i_in_src * 8) + j_in_src;
					second_byte_pos = (j_in_src != 7) ?
					    (first_byte_pos + 1) : (first_byte_pos + 8 * 127 + 1);
					dst_pos = dy * width + dx;
					first_byte = src_addr[first_byte_pos];
					second_byte = src_addr[second_byte_pos];
					bit_pos = dx * 10;
					bit_loc = bit_pos % 8;
					byte_16 = (first_byte << 8) | second_byte;
					if (bitCnt == 8)
						dst_addr_8[dst_pos] = byte_16 >> (8 - bit_loc);
					else
						dst_addr_16[dst_pos] =
						    (byte_16 >> (6 - bit_loc)) & 0x3ff;
				}
			}
		}
	}

	return 0;
}

#else

/* Algorithm 3:
 * frame buffer (10bit tiled) -> linear buffer (8bit), src coordinate -> dst
 */
static int __ReadYUVFrame_FSL_10b(void *dst_addr, uint8_t * src_addr,
				  unsigned int width, unsigned int height,
				  unsigned int stride, int h_cnt, int v_cnt, unsigned int bitCnt)
{
	int h_num, v_num;
	int sx, sy, dx, dy;
	int i, j;
	uint8_t *psrc;
	uint8_t *dst_addr_8 = NULL;
	uint8_t *pdst_8 = NULL;
	uint16_t *dst_addr_16 = NULL;
	uint16_t *pdst_16 = NULL;
	int bit_pos, bit_loc;
	uint8_t first_byte, second_byte;
	uint16_t byte_16;
	int start_pos = 0;

	if (bitCnt == 8)
		dst_addr_8 = (uint8_t *) dst_addr;
	else
		dst_addr_16 = (uint16_t *) dst_addr;

	for (v_num = 0; v_num < v_cnt; v_num++) {
		for (h_num = 0; h_num < h_cnt; h_num++) {
			sx = h_num * 8;
			sy = v_num * 128;
			dx = (sx * 8 + 9) / 10;
			dy = sy;

			for (i = 0; i < 128; i++) {
				psrc = &src_addr[sy * (stride) + h_num * (8 * 128) + i * 8];
				if (bitCnt == 8)
					pdst_8 = &dst_addr_8[dy * width + i * width + dx];
				else
					pdst_16 = &dst_addr_16[dy * width + i * width + dx];
				bit_pos = 10 * dx;

				/* front remain 2 bit, that means the first byte(8bit) all belone front pixel, should skip it */
				if ((sx * 8 % 10) == 2)
					start_pos = 1;
				else
					start_pos = 0;

				for (j = start_pos; j < 8; j++) {
					first_byte = psrc[j];
					second_byte = (j != 7) ? psrc[j + 1] : psrc[8 * 128];
					byte_16 = (first_byte << 8) | second_byte;
					bit_loc = bit_pos % 8;

					if (bitCnt == 8) {
						*pdst_8 = byte_16 >> (8 - bit_loc);
						pdst_8++;
					} else {
						*pdst_16 = (byte_16 >> (6 - bit_loc)) & 0x3ff;
						pdst_16++;
					}
					bit_pos += 10;
					/* first_byte use 2 bit, second_byte use 8 bit
					 * seconed_byte exhaused, should skip in next cycle
					 */
					if (bit_loc == 6)
						j++;
				}
			}
		}
	}

	return 0;
}

#endif

static unsigned int ReadYUVFrame_FSL_10b(unsigned int nPicWidth,
					 unsigned int nPicHeight,
					 unsigned int uVOffsetLuma,
					 unsigned int uVOffsetChroma,
					 unsigned int stride,
					 uint8_t ** nBaseAddr,
					 uint8_t * pDstBuffer,
					 unsigned int bInterlaced, unsigned int bitCnt)
{
	unsigned int luma_lines, chro_lines;
	uint8_t *base_addr;
	int h_cnt, v_cnt;
	uint8_t *dst_buf = pDstBuffer;
	int bit_ratio = bitCnt / 8;

	luma_lines = nPicHeight;
	chro_lines = nPicHeight / 2;
	if (bInterlaced) {
		luma_lines = luma_lines / 2;
		chro_lines = chro_lines / 2;
	}

	/* read luma */
	base_addr = nBaseAddr[0];
	h_cnt = ((nPicWidth * 10 / 8) + 7) / 8;
	v_cnt = (luma_lines + 127) / 128;

	__ReadYUVFrame_FSL_10b(dst_buf, base_addr, nPicWidth, nPicHeight,
			       stride, h_cnt, v_cnt, bitCnt);
	if (bInterlaced) {
		/* Read Bot Luma */
		dst_buf += (nPicWidth * nPicHeight) / 2 * bit_ratio;
		__ReadYUVFrame_FSL_10b(dst_buf, base_addr, nPicWidth,
				       nPicHeight, stride, h_cnt, v_cnt, bitCnt);
		dst_buf += (nPicWidth * nPicHeight) / 2 * bit_ratio;
	} else {
		dst_buf += (nPicWidth * nPicHeight) * bit_ratio;
	}

	/* read chroma */
	base_addr = nBaseAddr[1];
	h_cnt = (nPicWidth * 10 / 8 + 7) / 8;
	v_cnt = (chro_lines + 127) / 128;

	__ReadYUVFrame_FSL_10b(dst_buf, base_addr, nPicWidth, nPicHeight,
			       stride, h_cnt, v_cnt, bitCnt);

	if (bInterlaced) {
		/* Read Bot Chroma */
		dst_buf += (nPicWidth * nPicHeight) / 4 * (bit_ratio);
		__ReadYUVFrame_FSL_10b(dst_buf, base_addr, nPicWidth,
				       nPicHeight, stride, h_cnt, v_cnt, bitCnt);
	}

	if (bInterlaced)
		convert_inter_2_prog_4_nv12(pDstBuffer, nPicWidth * bit_ratio,
					    nPicHeight,
					    nPicWidth * nPicHeight * bit_ratio,
					    nPicWidth * nPicHeight / 2 * bit_ratio);

	return 0;
}

static void LoadFrameNV12(unsigned char *pFrameBuffer,
			  unsigned char *pYuvBuffer, unsigned int nFrameWidth,
			  unsigned int nFrameHeight, unsigned int nSizeLuma,
			  unsigned int bMonochrome, unsigned int bInterlaced)
{
	unsigned int nSizeUorV = nSizeLuma >> 2;
	unsigned char *pYSrc = pFrameBuffer;
	unsigned char *pUVSrc = pYSrc + nSizeLuma;

	unsigned char *pYDst = pYuvBuffer;
	unsigned char *pUDst = pYuvBuffer + nSizeLuma;
	unsigned char *pVDst = pYuvBuffer + nSizeLuma + nSizeUorV;

	memcpy(pYDst, pYSrc, nSizeLuma);
	if (bMonochrome) {
		memset(pUDst, 128, nSizeUorV);
		memset(pVDst, 128, nSizeUorV);
	} else {
		unsigned char *pLast = pVDst;

		while (pUDst < pLast) {
			*pUDst++ = *pUVSrc++;
			*pVDst++ = *pUVSrc++;
		}
	}
}

static void LoadFrameNV12_10b(unsigned char *pFrameBuffer,
			      unsigned char *pYuvBuffer,
			      unsigned int nFrameWidth,
			      unsigned int nFrameHeight, unsigned int nSizeLuma,
			      unsigned int bMonochrome,
			      unsigned int bInterlaced, unsigned int bitCnt)
{
	if (bitCnt == 16) {
		unsigned int nSizeUorV = nSizeLuma / 4;
		uint16_t *pYSrc = (uint16_t *) pFrameBuffer;
		uint16_t *pUVSrc = pYSrc + nSizeLuma;

		uint16_t *pYDst = (uint16_t *) pYuvBuffer;
		uint16_t *pUDst = (uint16_t *) pYuvBuffer + nSizeLuma;
		uint16_t *pVDst = (uint16_t *) pYuvBuffer + nSizeLuma + nSizeUorV;
		uint16_t *pLast = pVDst;

		memcpy(pYDst, pYSrc, nSizeLuma * 2);
		while (pUDst < pLast) {
			*pUDst++ = *pUVSrc++;
			*pVDst++ = *pUVSrc++;
		}
	} else {
		unsigned int nSizeUorV = nSizeLuma / 4;
		uint8_t *pYSrc = (uint8_t *) pFrameBuffer;
		uint8_t *pUVSrc = pYSrc + nSizeLuma;

		uint8_t *pYDst = (uint8_t *) pYuvBuffer;
		uint8_t *pUDst = (uint8_t *) pYuvBuffer + nSizeLuma;
		uint8_t *pVDst = (uint8_t *) pYuvBuffer + nSizeLuma + nSizeUorV;
		uint8_t *pLast = pVDst;

		memcpy(pYDst, pYSrc, nSizeLuma);
		while (pUDst < pLast) {
			*pUDst++ = *pUVSrc++;
			*pVDst++ = *pUVSrc++;
		}
	}
}

int isNumber(char *str)
{
	int ret = 1;
	int len = strlen(str);
	int i;
	for (i = 0; i < len; i++) {
		if (str[i] < '0' || str[i] > '9') {
			ret = 0;
			break;
		}
	}
	return ret;
}

void release_buffer(stream_media_t * port)
{
	int i, j;

	if (V4L2_MEMORY_MMAP == port->memory) {
		if (port->stAppV4lBuf) {
			for (i = 0; i < port->buf_count; i++) {
				for (j = 0; j < 2; j++) {
					if (port->stAppV4lBuf[i].addr[j])
						munmap(port->stAppV4lBuf[i].addr[j],
						       port->stAppV4lBuf[i].size[j]);
				}
			}
			free((void *)port->stAppV4lBuf);
			port->stAppV4lBuf = NULL;
		}
	}
}

int get_fmt(component_t * pComponent, stream_media_t * port, struct v4l2_format *format)
{
	int lErr;

	lErr = ioctl(pComponent->hDev, VIDIOC_G_FMT, format);
	if (lErr) {
		printf("%s() VIDIOC_G_FMT ioctl failed %d %s\n", __FUNCTION__,
		       errno, strerror(errno));
		return -1;
	} else {
		printf("%s() VIDIOC_G_FMT w(%d) h(%d)\n", __FUNCTION__,
		       format->fmt.pix_mp.width, format->fmt.pix_mp.height);
		printf
		    ("%s() VIDIOC_G_FMT: sizeimage_0(%d) bytesperline_0(%d) sizeimage_1(%d) bytesperline_1(%d) \n",
		     __FUNCTION__, format->fmt.pix_mp.plane_fmt[0].sizeimage,
		     format->fmt.pix_mp.plane_fmt[0].bytesperline,
		     format->fmt.pix_mp.plane_fmt[1].sizeimage,
		     format->fmt.pix_mp.plane_fmt[1].bytesperline);
	}
	port->openFormat.yuv.nWidth = format->fmt.pix_mp.width;
	port->openFormat.yuv.nHeight = format->fmt.pix_mp.height;
	port->openFormat.yuv.nBitCount = 12;
	port->openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
	port->openFormat.yuv.nFrameRate = 30;
	port->openFormat.yuv.stride = format->fmt.pix_mp.plane_fmt[0].bytesperline;
	port->frame_size = (format->fmt.pix_mp.height * format->fmt.pix_mp.height * 3) / 2;
	pComponent->ulWidth = format->fmt.pix_mp.width;
	pComponent->ulHeight = format->fmt.pix_mp.height;

	return 0;
}

int set_fmt(component_t * pComponent, struct v4l2_format *format)
{
	int lErr;

	lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, format);
	if (lErr) {
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__,
		       errno, strerror(errno));
		return -1;
	}

	return 0;
}

int check_fmt(component_t * pComponent, struct v4l2_format *format)
{
	struct v4l2_fmtdesc fmt_desc;
	int ret = -1;

	fmt_desc.index = 0;
	fmt_desc.type = format->type;

	if (format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
	    || format->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		dec_bit_dbg(LVL_BIT_INFO, "output support format:\n");
	else if (format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
		 || format->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		dec_bit_dbg(LVL_BIT_INFO, "capture support format:\n");
	else
		dec_bit_dbg(LVL_BIT_INFO, "format_type(%d) support format:\n", format->type);

	while (!ioctl(pComponent->hDev, VIDIOC_ENUM_FMT, &fmt_desc)) {
		dec_bit_dbg(LVL_BIT_INFO, "%c%c%c%c\n",
			    (fmt_desc.pixelformat) & 0xff,
			    (fmt_desc.pixelformat >> 8) & 0xff,
			    (fmt_desc.pixelformat >> 16) & 0xff,
			    (fmt_desc.pixelformat >> 24) & 0xff);

		if (fmt_desc.pixelformat == format->fmt.pix_mp.pixelformat)
			ret = 0;

		fmt_desc.index++;
	}

	if (ret == -1)
		dec_bit_dbg(LVL_BIT_WARN,
			    "warning: not support format: %c%c%c%c\n",
			    (format->fmt.pix_mp.pixelformat) & 0xff,
			    (format->fmt.pix_mp.pixelformat >> 8) & 0xff,
			    (format->fmt.pix_mp.pixelformat >> 16) & 0xff,
			    (format->fmt.pix_mp.pixelformat >> 24) & 0xff);

	return ret;
}

int get_mini_cap_buffer(component_t * pComponent)
{
	struct v4l2_control ctl;
	int lErr;

	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
	lErr = ioctl(pComponent->hDev, VIDIOC_G_CTRL, &ctl);
	if (lErr) {
		printf("%s() VIDIOC_G_CTRL ioctl failed %d %s\n", __FUNCTION__,
		       errno, strerror(errno));
		return -1;
	}
	printf("%s() VIDIOC_G_CTRL ioctl val=%d\n", __FUNCTION__, ctl.value);
	if (pComponent->ports[STREAM_DIR_OUT].buf_count < ctl.value)
		pComponent->ports[STREAM_DIR_OUT].buf_count = ctl.value;

	return 0;
}

int req_buffer(component_t * pComponent, stream_media_t * port,
	       struct v4l2_requestbuffers *req_bufs)
{
	int lErr;

	lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, req_bufs);
	if (lErr) {
		printf("%s() VIDIOC_REQBUFS ioctl failed %d %s\n", __FUNCTION__,
		       errno, strerror(errno));
		return -1;
	}

	port->memory = req_bufs->memory;
	port->buf_count = req_bufs->count;

	return 0;
}

int query_buffer(component_t * pComponent, stream_media_t * port,
		 enum v4l2_buf_type type, struct v4l2_plane *stV4lPlanes)
{
	struct zvapp_v4l_buf_info *stAppV4lBuf;
	struct v4l2_buffer stV4lBuf;
	int i, j;
	int lErr;

	stAppV4lBuf = port->stAppV4lBuf;
	memset(stAppV4lBuf, 0, port->buf_count * sizeof(struct zvapp_v4l_buf_info));

	if (V4L2_MEMORY_MMAP == port->memory) {
		for (i = 0; i < port->buf_count; i++) {
			stAppV4lBuf[i].stV4lBuf.index = i;
			stAppV4lBuf[i].stV4lBuf.type = type;
			stAppV4lBuf[i].stV4lBuf.bytesused = 0;
			stAppV4lBuf[i].stV4lBuf.memory = port->memory;
			stAppV4lBuf[i].stV4lBuf.m.planes = stAppV4lBuf[i].stV4lPlanes;
			if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
			    || type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
				stAppV4lBuf[i].stV4lBuf.length = 2;
			else
				stAppV4lBuf[i].stV4lBuf.length = 1;
			stV4lBuf.type = type;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.index = i;
			stV4lBuf.m.planes = stV4lPlanes;
			if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
			    || type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
				stV4lBuf.length = 2;
			else
				stV4lBuf.length = 1;

			lErr = ioctl(pComponent->hDev, VIDIOC_QUERYBUF, &stV4lBuf);
			if (!lErr) {
				for (j = 0; j < stV4lBuf.length; j++) {
					stAppV4lBuf[i].size[j] = stV4lBuf.m.planes[j].length;
					stAppV4lBuf[i].addr[j] =
					    mmap(0, stV4lBuf.m.planes[j].length,
						 PROT_READ | PROT_WRITE,
						 MAP_SHARED, pComponent->hDev,
						 stV4lBuf.m.planes[j].m.mem_offset);
					if (stAppV4lBuf[i].addr[j] <= 0) {
						printf
						    ("%s() V4L mmap failed index=%d \n",
						     __FUNCTION__, i);
						return -1;
					}
					stAppV4lBuf[i].stV4lBuf.m.planes[j].m.mem_offset =
					    stV4lBuf.m.planes[j].m.mem_offset;
					stAppV4lBuf[i].stV4lBuf.m.planes[j].bytesused = 0;
					stAppV4lBuf[i].stV4lBuf.m.planes[j].length =
					    stV4lBuf.m.planes[j].length;
					stAppV4lBuf[i].stV4lBuf.m.planes[j].data_offset = 0;
				}
			} else {
				printf("%s() VIDIOC_QUERYBUF failed index=%d \n", __FUNCTION__, i);
				return -1;
			}
		}
	}

	return 0;
}

int dec_streamon(component_t * pComponent, enum v4l2_buf_type type)
{
	int ret = 0;

	ret = ioctl(pComponent->hDev, VIDIOC_STREAMON, &type);
	if (ret)
		printf("warning: streamon failed, type: %d, ret: %d\n", type, ret);

	return ret;
}

int dec_streamoff(component_t * pComponent, enum v4l2_buf_type type)
{
	int ret = 0;

	ret = ioctl(pComponent->hDev, VIDIOC_STREAMOFF, &type);
	if (ret)
		printf("warning: streamoff failed, type: %d, ret: %d\n", type, ret);

	return ret;
}

void showUsage(void)
{
	printf("\n\
Usage: ./mxc_v4l2_vpu_dec.out ifile [PATH] ifmt [IFMT] ofmt [OFMT] [OPTIONS]\n\n\
OPTIONS:\n\
    --help          Show usage manual.\n\n\
    ifile path      Specify the input file path.\n\n\
    path            Specify the input file path.\n\n\
    ifmt            Specify input file encode format number. Format comparsion table:\n\
                        VPU_VIDEO_UNDEFINED           0\n\
                        VPU_VIDEO_AVC                 1\n\
                        VPU_VIDEO_VC1                 2\n\
                        VPU_VIDEO_MPEG2               3\n\
                        VPU_VIDEO_AVS                 4\n\
                        VPU_VIDEO_ASP(MPEG4/H263)     5\n\
                        VPU_VIDEO_JPEG(MJPEG)         6\n\
                        VPU_VIDEO_RV8                 7\n\
                        VPU_VIDEO_RV9                 8\n\
                        VPU_VIDEO_VP6                 9\n\
                        VPU_VIDEO_SPK                 10\n\
                        VPU_VIDEO_VP8                 11\n\
                        VPU_VIDEO_AVC_MVC             12\n\
                        VPU_VIDEO_HEVC                13\n\
                        VPU_PIX_FMT_DIVX              14\n\n\
    ofmt            Specify decode format number. Format comparsion table:\n\
                        V4L2_PIX_FMT_NV12             0\n\
                        V4L2_PIX_FMT_YUV420           1\n\
                        VPU_PIX_FMT_TILED_8           2\n\
                        VPU_PIX_FMT_TILED_10          3\n\n\
    obit            Specify output bit format for 10-bit encoded data:\n\
                        VPU_OUT_BIT_PRECISE_8         0 (default)\n\
                        VPU_OUT_BIT_16                1\n\n\
    ofile path      Specify the output file path.\n\n\
    loop times      Specify loop decode times to the same file. If the times not set, the loop continues.\n\n\
    frames count    Specify the count of decode frames. Default total decode.\n\n\
    bs count        Specify the count of input buffer block size, the unit is Kb.\n\n\
    iqc count       Specify the count of input reqbuf.\n\n\
    oqc count       Specify the count of output reqbuf.\n\n\
    dev device      Specify the VPU decoder device node(generally /dev/video12).\n\n\
    dbg log_level   Specify bit mask of debug log.\n\
                        LVL_BIT_WARN             1 << 0\n\
                        LVL_BIT_EVENT            1 << 1\n\
                        LVL_BIT_INFO             1 << 2\n\
                        LVL_BIT_FPS              1 << 3\n\n\n\
EXAMPLES:\n\
    ./mxc_v4l2_vpu_dec.out ifile decode.264 ifmt 1 ofmt 1 ofile test.yuv\n\n\
    ./mxc_v4l2_vpu_dec.out ifile decode.264 ifmt 1 bs 500 ofmt 1 ofile test.yuv\n\n\
    ./mxc_v4l2_vpu_dec.out ifile decode.bit ifmt 13 ofmt 1 ofile test.yuv frames 100 loop 10 dev /dev/video12\n\n\
    ./mxc_v4l2_vpu_dec.out ifile decode.bit ifmt 13 ofmt 1 loop\n\n");

}

unsigned int dec_get_outBitCnt(VPU_OUT_BIT_FMT bit)
{
	unsigned int bitCnt = 0;

	switch (bit) {
	case VPU_OUT_BIT_PRECISE_8:
		bitCnt = 8;
		break;
	case VPU_OUT_BIT_16:
		bitCnt = 16;
		break;
	default:
		bitCnt = 8;
		break;
	}

	return bitCnt;
}

int dec_write_buf(stream_media_t * port, struct v4l2_buffer *v4l2Buf,
		  struct zvapp_v4l_buf_info *appV4lBuf, FILE * fOutput)
{
	unsigned char *nBaseAddr[4];
	unsigned int stride;
	unsigned int b10format;
	unsigned int bInterLace;
	unsigned int totalSizeImage;
	unsigned char *dstbuf = NULL;
	unsigned char *yuvbuf = NULL;
	unsigned int writeBytes;
	unsigned int outBitCnt;
	int width;
	int height;
	int ret = 0;

	stride = port->openFormat.yuv.stride;
	b10format = (v4l2Buf->reserved == 1) ? 1 : 0;
	bInterLace = (v4l2Buf->field == 4) ? 1 : 0;
	width = port->openFormat.yuv.nWidth;
	height = port->openFormat.yuv.nHeight;

	outBitCnt = dec_get_outBitCnt(port->outBit);
	if (b10format) {
		if (outBitCnt == 16)
			totalSizeImage =
			    (v4l2Buf->m.planes[0].length + v4l2Buf->m.planes[1].length) * 2;
		else
			totalSizeImage = v4l2Buf->m.planes[0].length + v4l2Buf->m.planes[1].length;

		writeBytes = (width * height * 3 / 2) * (outBitCnt / 8);
	} else {
		totalSizeImage = v4l2Buf->m.planes[0].length + v4l2Buf->m.planes[1].length;
		writeBytes = width * height * 3 / 2;
	}

	dstbuf = (unsigned char *)malloc(totalSizeImage);
	memset(dstbuf, 0x0, totalSizeImage);
	if (!dstbuf) {
		printf("error: dstbuf alloc failed\n");
		ret = -1;
		goto exit;
	}
	yuvbuf = (unsigned char *)malloc(totalSizeImage);
	memset(yuvbuf, 0x0, totalSizeImage);
	if (!yuvbuf) {
		printf("error: yuvbuf alloc failed\n");
		ret = -1;
		goto exit;
	}

	nBaseAddr[0] =
	    (unsigned char *)(appV4lBuf[v4l2Buf->index].addr[0] + v4l2Buf->m.planes[0].data_offset);
	nBaseAddr[1] =
	    (unsigned char *)(appV4lBuf[v4l2Buf->index].addr[1] + v4l2Buf->m.planes[1].data_offset);
	nBaseAddr[2] = nBaseAddr[0] + v4l2Buf->m.planes[0].length / 2;
	nBaseAddr[3] = nBaseAddr[1] + v4l2Buf->m.planes[1].length / 2;

	switch (port->fmt) {
	case V4L2_PIX_FMT_NV12:
		if (b10format)
			ReadYUVFrame_FSL_10b(width, height, 0, 0, stride,
					     nBaseAddr, yuvbuf, bInterLace, outBitCnt);
		else
			ReadYUVFrame_FSL_8b(width, height, 0, 0, stride,
					    nBaseAddr, yuvbuf, bInterLace);
		fwrite((void *)yuvbuf, 1, writeBytes, fOutput);
		break;
	case V4L2_PIX_FMT_YUV420M:
		if (b10format) {
			ReadYUVFrame_FSL_10b(width, height, 0, 0, stride,
					     nBaseAddr, yuvbuf, bInterLace, outBitCnt);
			LoadFrameNV12_10b(yuvbuf, dstbuf, width, height,
					  width * height, 0, bInterLace, outBitCnt);
		} else {
			ReadYUVFrame_FSL_8b(width, height, 0, 0, stride,
					    nBaseAddr, yuvbuf, bInterLace);
			LoadFrameNV12(yuvbuf, dstbuf, width, height, width * height, 0, bInterLace);
		}
		fwrite((void *)dstbuf, 1, writeBytes, fOutput);
		break;
	case VPU_PIX_FMT_TILED_8:
	case VPU_PIX_FMT_TILED_10:
		fwrite((void *)nBaseAddr[0], 1, v4l2Buf->m.planes[0].bytesused, fOutput);
		fwrite((void *)nBaseAddr[1], 1, v4l2Buf->m.planes[1].bytesused, fOutput);
		break;
	default:
		printf
		    ("warning: please specify output format, or the format you specified is not standard. \n");
		break;
	}

	fflush(fOutput);
	ret = 0;

exit:
	if (dstbuf)
		free(dstbuf);
	if (yuvbuf)
		free(yuvbuf);

	return ret;
}

void test_streamout(component_t * pComponent)
{
	int lErr = 0;
	FILE *fpOutput = 0;
	struct zvapp_v4l_buf_info *stAppV4lBuf;
	struct v4l2_buffer stV4lBuf;
	struct v4l2_plane stV4lPlanes[3];
	struct pollfd pfd;
	int r;
	struct v4l2_event evt;
	unsigned int i;
	unsigned int j;
	zoe_bool_t seek_flag;
	unsigned int outFrameNum = 0;
	float used_time = 0.01;
	struct v4l2_format format;
	struct v4l2_requestbuffers req_bufs;

STREAMOUT_INFO:

	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	lErr = get_fmt(pComponent, &pComponent->ports[STREAM_DIR_OUT], &format);
	if (lErr < 0) {
		ret_err = 40;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	format.fmt.pix_mp.pixelformat = pComponent->ports[STREAM_DIR_OUT].fmt;
	lErr = set_fmt(pComponent, &format);
	if (lErr < 0) {
		ret_err = 41;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	lErr = get_mini_cap_buffer(pComponent);
	if (lErr < 0) {
		ret_err = 43;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
	req_bufs.count = pComponent->ports[STREAM_DIR_OUT].buf_count;
	req_bufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	req_bufs.memory = V4L2_MEMORY_MMAP;
	lErr = req_buffer(pComponent, &pComponent->ports[STREAM_DIR_OUT], &req_bufs);
	if (lErr < 0) {
		ret_err = 44;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf =
	    malloc(pComponent->ports[STREAM_DIR_OUT].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf) {
		printf("warning: %s() alloc stream buffer failed.\n", __FUNCTION__);
		ret_err = 44;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}
	stAppV4lBuf = pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf;

	lErr = query_buffer(pComponent, &pComponent->ports[STREAM_DIR_OUT],
			    V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, stV4lPlanes);
	if (lErr < 0) {
		ret_err = 45;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	pComponent->ports[STREAM_DIR_OUT].opened = 1;

STREAMOUT_START:
	printf("%s() [\n", __FUNCTION__);
	seek_flag = 1;
	pComponent->ports[STREAM_DIR_OUT].done_flag = 0;
	pComponent->ports[STREAM_DIR_OUT].streamoff = 0;
	frame_done = 0;
	outFrameNum = 0;

	/***********************************************
    ** 1> Open output file descriptor
    ***********************************************/

	if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT) {
		fpOutput = fopen(pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr, "w+");
		if (fpOutput == NULL) {
			printf("%s() error: Unable to open file %s.\n",
			       __FUNCTION__, pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr);
			g_unCtrlCReceived = 1;
			ret_err = 46;
			goto FUNC_EXIT;
		}
	}

	lErr = dec_streamon(pComponent, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived = 0;
	} else {
		g_unCtrlCReceived = 1;
		ret_err = 48;
		goto FUNC_END;
	}

	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!g_unCtrlCReceived && !pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived) {
		/***********************************************
		** QBUF, send all the buffers to driver
		***********************************************/
		if (seek_flag == 1) {
			for (i = 0; i < pComponent->ports[STREAM_DIR_OUT].buf_count; i++) {
				stAppV4lBuf[i].sent = 0;
			}
			seek_flag = 0;
		}
		for (i = 0; i < pComponent->ports[STREAM_DIR_OUT].buf_count; i++) {
			if (!stAppV4lBuf[i].sent) {
				for (j = 0; j < stAppV4lBuf[i].stV4lBuf.length; j++) {
					stAppV4lBuf[i].stV4lBuf.m.planes[j].bytesused = 0;
					stAppV4lBuf[i].stV4lBuf.m.planes[j].data_offset = 0;
				}
				lErr = ioctl(pComponent->hDev,
					     VIDIOC_QBUF, &stAppV4lBuf[i].stV4lBuf);
				if (lErr) {
					if (lErr != EAGAIN)
						printf
						    ("%s() QBUF ioctl failed %d %s\n",
						     __FUNCTION__, errno, strerror(errno));
					break;
				} else {
					stAppV4lBuf[i].sent = 1;
				}
			}
		}

		/***********************************************
		** DQBUF, get buffer from driver
		***********************************************/
		pfd.fd = pComponent->hDev;
		pfd.events = POLLIN | POLLRDNORM | POLLPRI;
		r = poll(&pfd, 1, 1000);

		if (-1 == r) {
			fprintf(stderr, "%s() poll errno(%d)\n", __FUNCTION__, errno);
			continue;
		} else if (0 == r) {
			printf("\nstream out: poll readable dev timeout.\n");
			continue;
		} else {
			if (pfd.revents & POLLPRI) {
				memset(&evt, 0, sizeof(struct v4l2_event));
				lErr = ioctl(pComponent->hDev, VIDIOC_DQEVENT, &evt);
				if (lErr) {
					printf
					    ("%s() VIDIOC_DQEVENT ioctl failed %d %s\n",
					     __FUNCTION__, errno, strerror(errno));
					break;
				}

				if (evt.type == V4L2_EVENT_EOS) {
					printf("V4L2_EVENT_EOS is called\n");
					break;
				} else if (evt.type == V4L2_EVENT_SOURCE_CHANGE) {
					pComponent->res_change_flag = 1;
					break;
				} else if (evt.type == V4L2_EVENT_CODEC_ERROR) {
					g_unCtrlCReceived = 1;
					goto FUNC_END;
				} else {
					printf("%s() VIDIOC_DQEVENT type=%d\n",
					       __FUNCTION__, evt.type);
				}
			}

			if (pfd.revents & POLLERR) {
				printf("POLLERR is triggerred\n");
				g_unCtrlCReceived = 1;
				goto FUNC_END;
			}
			if (!(pfd.revents & (POLLIN | POLLRDNORM)))
				continue;
		}

		stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		stV4lBuf.memory = V4L2_MEMORY_MMAP;
		stV4lBuf.m.planes = stV4lPlanes;
		stV4lBuf.length = 2;

		lErr = ioctl(pComponent->hDev, VIDIOC_DQBUF, &stV4lBuf);
		if (!lErr) {
			// clear sent flag
			stAppV4lBuf[stV4lBuf.index].sent = 0;

			if (pComponent->ports[STREAM_DIR_OUT].outFrameCount > 0
			    && outFrameNum >= pComponent->ports[STREAM_DIR_OUT].outFrameCount) {
				if (!frame_done)
					frame_done = 1;
				break;
			} else {
				outFrameNum++;
				gettimeofday(&end, NULL);
				used_time =
				    (float)(end.tv_sec - start.tv_sec +
					    (end.tv_usec - start.tv_usec) / 1000000.0);
				dec_bit_dbg(LVL_BIT_FPS,
					    "\rframes = %d, fps = %.2f, used_time = %.2f\t\t",
					    outFrameNum, outFrameNum / used_time, used_time);
				if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT) {
					dec_write_buf(&pComponent->ports[STREAM_DIR_OUT],
						      &stV4lBuf, stAppV4lBuf, fpOutput);
				}
			}
		} else {
			if (errno == EPIPE) {
				printf("EPIPE is called\n");
				break;
			}
			if (errno != EAGAIN) {
				printf("\r%s()  DQBUF failed(%d) errno(%d)\n",
				       __FUNCTION__, lErr, errno);
			}
		}
		usleep(10);
	}

FUNC_END:
	printf("%s() ]\n", __FUNCTION__);
	fflush(fpOutput);
	if (fpOutput) {
		fclose(fpOutput);
	}

	if (pComponent->res_change_flag) {
		pthread_mutex_lock(&g_mutex);
		lErr = dec_streamoff(pComponent, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		if (lErr)
			g_unCtrlCReceived = 1;
		pComponent->ports[STREAM_DIR_OUT].streamoff = 1;
		pthread_mutex_unlock(&g_mutex);

		release_buffer(&pComponent->ports[STREAM_DIR_OUT]);
		pComponent->res_change_flag = 0;
		goto STREAMOUT_INFO;
	}

	pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived = 1;
	pComponent->ports[STREAM_DIR_OUT].done_flag = 1;

	/* output port shall not streamoff until input prot streamoff,
	 * except resolution change case
	 */
	while (!pComponent->ports[STREAM_DIR_IN].streamoff && !g_unCtrlCReceived)
		usleep(10);

	pthread_mutex_lock(&g_mutex);
	lErr = dec_streamoff(pComponent, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (lErr)
		g_unCtrlCReceived = 1;

	pComponent->ports[STREAM_DIR_OUT].streamoff = 1;
	pthread_mutex_unlock(&g_mutex);

	printf("Total: frames = %d, fps = %.2f, used_time = %.2f \n",
	       outFrameNum, outFrameNum / used_time, used_time);

	if (!g_unCtrlCReceived) {
		while (preLoopTimes == loopTimes && !g_unCtrlCReceived) ;	//sync stream in loopTimes

		if (abs(preLoopTimes - loopTimes) > 1) {
			printf("warn: test_streamin thread too fast, test_streamout cannot sync\n");
			loopTimes = preLoopTimes - 1;	//sync actual loopTimes
			g_unCtrlCReceived = 1;
		} else {
			if (loopTimes <= 0) {
				g_unCtrlCReceived = 1;
			} else {
				while (pComponent->ports[STREAM_DIR_IN].done_flag
				       && !g_unCtrlCReceived) {
					usleep(10);
				}

				preLoopTimes = loopTimes;
				goto STREAMOUT_START;
			}
		}
	}

FUNC_EXIT:

	release_buffer(&pComponent->ports[STREAM_DIR_OUT]);
	stAppV4lBuf = NULL;
	pComponent->ports[STREAM_DIR_OUT].opened = ZOE_FALSE;
}

void test_streamin(component_t * pComponent)
{
	int lErr = 0;
	FILE *fpInput = 0;

	struct zvapp_v4l_buf_info *stAppV4lBuf;
	struct v4l2_buffer stV4lBuf;
	struct v4l2_plane stV4lPlanes[3];
	struct v4l2_buffer *pstV4lBuf = NULL;
	struct v4l2_decoder_cmd v4l2cmd;

	struct pollfd pfd;
	int r;

	unsigned int i;
	unsigned int total;
	long file_size;
	int seek_flag;
	int qbuf_times;
	struct v4l2_requestbuffers req_bufs;
	struct v4l2_format format;

	unsigned int ulIOBlockSize;

	// set v4l2 output format (compressed data input)
	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (pComponent->ports[STREAM_DIR_IN].fmt < ZPU_NUM_FORMATS_COMPRESSED) {
		format.fmt.pix_mp.pixelformat =
		    formats_compressed[pComponent->ports[STREAM_DIR_IN].fmt];
	} else {
		format.fmt.pix_mp.pixelformat = VPU_PIX_FMT_LOGO;
	}
	format.fmt.pix_mp.num_planes = 1;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = pComponent->ports[STREAM_DIR_IN].frame_size;
	lErr = check_fmt(pComponent, &format);
	if (lErr < 0) {
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}
	lErr = set_fmt(pComponent, &format);
	if (lErr < 0) {
		ret_err = 30;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	ulIOBlockSize = pComponent->ports[STREAM_DIR_IN].frame_size;

	memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
	req_bufs.count = pComponent->ports[STREAM_DIR_IN].buf_count;
	req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	req_bufs.memory = V4L2_MEMORY_MMAP;
	lErr = req_buffer(pComponent, &pComponent->ports[STREAM_DIR_IN], &req_bufs);
	if (lErr < 0) {
		ret_err = 31;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	pComponent->ports[STREAM_DIR_IN].stAppV4lBuf =
	    malloc(pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf) {
		printf("warning: %s() alloc stream buffer failed.\n", __FUNCTION__);
		ret_err = 31;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}
	stAppV4lBuf = pComponent->ports[STREAM_DIR_IN].stAppV4lBuf;

	lErr = query_buffer(pComponent, &pComponent->ports[STREAM_DIR_IN],
			    V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, stV4lPlanes);
	if (lErr < 0) {
		ret_err = 32;
		g_unCtrlCReceived = 1;
		goto FUNC_EXIT;
	}

	pComponent->ports[STREAM_DIR_IN].opened = 1;

STREAMIN_START:
	printf("%s() [\n", __FUNCTION__);
	pComponent->ports[STREAM_DIR_IN].done_flag = 0;
	pComponent->ports[STREAM_DIR_IN].streamoff = 0;
	seek_flag = 1;
	qbuf_times = 0;
	gettimeofday(&start, NULL);

	/***********************************************
	** 1> Open output file descriptor
	***********************************************/
	if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) {
		fpInput = fopen(pComponent->ports[STREAM_DIR_IN].pszNameOrAddr, "r");
		if (fpInput == NULL) {
			printf("%s() error: Unable to open file %s.\n",
			       __FUNCTION__, pComponent->ports[STREAM_DIR_IN].pszNameOrAddr);
			g_unCtrlCReceived = 1;
			ret_err = 33;
			goto FUNC_EXIT;
		} else {
			printf("Testing stream: %s\n",
			       pComponent->ports[STREAM_DIR_IN].pszNameOrAddr);
			fseek(fpInput, 0, SEEK_END);
			file_size = ftell(fpInput);
			fseek(fpInput, 0, SEEK_SET);
		}
	} else {
		printf("%s() Unknown media type %d.\n", __FUNCTION__,
		       pComponent->ports[STREAM_DIR_IN].eMediaType);
		g_unCtrlCReceived = 1;
		ret_err = 34;
		goto FUNC_EXIT;
	}

	lErr = dec_streamon(pComponent, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_IN].unCtrlCReceived = 0;
	} else {
		g_unCtrlCReceived = 1;
		ret_err = 35;
		goto FUNC_END;
	}

	while (pComponent->ports[STREAM_DIR_OUT].done_flag && !g_unCtrlCReceived) {
		usleep(10);
	}

	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!g_unCtrlCReceived && !pComponent->ports[STREAM_DIR_IN].unCtrlCReceived) {
		if (frame_done || pComponent->ports[STREAM_DIR_OUT].done_flag) {
			break;
		}

		if (seek_flag) {
			for (i = 0; i < pComponent->ports[STREAM_DIR_IN].buf_count; i++) {
				stAppV4lBuf[i].sent = 0;
			}
			seek_flag = 0;
		}

		int buf_avail = 0;

		/***********************************************
		** DQBUF, get buffer from driver
		***********************************************/
		for (i = 0; i < pComponent->ports[STREAM_DIR_IN].buf_count; i++) {
			if (!stAppV4lBuf[i].sent) {
				buf_avail = 1;
				break;
			}
		}

		if (!buf_avail) {
			pfd.fd = pComponent->hDev;
			pfd.events = POLLOUT | POLLWRNORM;

			r = poll(&pfd, 1, 2000);

			if (-1 == r) {
				fprintf(stderr, "%s() poll errno(%d)\n", __FUNCTION__, errno);
				continue;
			}
			if (0 == r) {
				printf("\nstream in: poll writable dev timeout.\n");
				continue;
			}

			if (pfd.revents & POLLERR) {
				usleep(10);
				continue;
			}

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.m.planes = stV4lPlanes;
			stV4lBuf.length = 1;
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

		for (i = 0; i < pComponent->ports[STREAM_DIR_IN].buf_count; i++) {
			if (!stAppV4lBuf[i].sent) {
				pstV4lBuf = &stAppV4lBuf[i].stV4lBuf;
				break;
			}
		}

		if (pstV4lBuf) {
			char *pBuf;
			unsigned int block_size;

RETRY:
			if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) {
				for (i = 0; i < pstV4lBuf->length; i++) {
					pBuf = stAppV4lBuf[pstV4lBuf->index].addr[i];
					block_size = stAppV4lBuf[pstV4lBuf->index].size[i];
					pstV4lBuf->m.planes[i].bytesused =
					    fread((void *)pBuf, 1, block_size, fpInput);
					pstV4lBuf->m.planes[i].data_offset = 0;
					file_size -= pstV4lBuf->m.planes[i].bytesused;
					if (V4L2_MEMORY_MMAP ==
					    pComponent->ports[STREAM_DIR_IN].memory) {
						msync((void *)pBuf, block_size, MS_SYNC);
					}
				}
			}

			total = 0;
			ulIOBlockSize = 0;
			for (i = 0; i < pstV4lBuf->length; i++) {
				total += pstV4lBuf->m.planes[i].bytesused;
				ulIOBlockSize += stAppV4lBuf[pstV4lBuf->index].size[i];
			}

			if ((pComponent->ports[STREAM_DIR_IN].eMediaType ==
			     MEDIA_FILE_IN) && (total != ulIOBlockSize)) {
				if ((pComponent->ports[STREAM_DIR_IN].portType ==
				     COMPONENT_PORT_COMP_IN)
				    || (pComponent->ports[STREAM_DIR_IN].portType ==
					COMPONENT_PORT_YUV_IN)) {
					pComponent->ports[STREAM_DIR_IN].unCtrlCReceived = 1;
				}
			}

			if (total != 0) {
				/***********************************************
				** QBUF, put data to driver
				***********************************************/
				lErr = ioctl(pComponent->hDev, VIDIOC_QBUF, pstV4lBuf);
				if (lErr) {
					if (errno == EAGAIN) {
						printf("\n");
						lErr = 0;
					} else {
						printf
						    ("v4l2_buf index(%d) type(%d) memory(%d) sequence(%d) length(%d) planes(%p)\n",
						     pstV4lBuf->index,
						     pstV4lBuf->type,
						     pstV4lBuf->memory,
						     pstV4lBuf->sequence,
						     pstV4lBuf->length, pstV4lBuf->m.planes);
					}
				} else {
					stAppV4lBuf[pstV4lBuf->index].sent = 1;
					qbuf_times++;
				}
			} else {
				if (pComponent->ports[STREAM_DIR_IN].unCtrlCReceived) {
					printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
					break;
				}
				if (file_size == 0) {
					file_size = -1;
					break;
				}
				usleep(10);
				goto RETRY;
			}
		}
		usleep(10);
	}

FUNC_END:
	printf("\n%s() ]\n", __FUNCTION__);
	if (fpInput) {
		fclose(fpInput);
	}

	pComponent->ports[STREAM_DIR_IN].unCtrlCReceived = 1;

	printf("stream in: qbuf_times= %d\n", qbuf_times);
	if (!g_unCtrlCReceived && !frame_done) {
		v4l2cmd.cmd = V4L2_DEC_CMD_STOP;
		lErr = ioctl(pComponent->hDev, VIDIOC_DECODER_CMD, &v4l2cmd);
		if (lErr) {
			printf
			    ("warning: %s() VIDIOC_DECODER_CMD has error, errno(%d), %s \n",
			     __FUNCTION__, errno, strerror(errno));
			g_unCtrlCReceived = 1;
			ret_err = 36;
		} else {
			printf("%s() sent cmd: V4L2_DEC_CMD_STOP\n", __FUNCTION__);
		}
	}
	pComponent->ports[STREAM_DIR_IN].done_flag = 1;

	/* input port shall not streamoff until output port complete decode */
	while (!pComponent->ports[STREAM_DIR_OUT].done_flag && !g_unCtrlCReceived)
		usleep(10);

	pthread_mutex_lock(&g_mutex);
	lErr = dec_streamoff(pComponent, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (lErr)
		g_unCtrlCReceived = 1;
	pComponent->ports[STREAM_DIR_IN].streamoff = 1;
	pthread_mutex_unlock(&g_mutex);

	loopTimes--;
	if (!g_unCtrlCReceived) {
		if ((loopTimes) > 0) {
			goto STREAMIN_START;
		}
	}

FUNC_EXIT:

	release_buffer(&pComponent->ports[STREAM_DIR_IN]);
	stAppV4lBuf = NULL;
	pComponent->ports[STREAM_DIR_IN].opened = ZOE_FALSE;
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
		printf("VIDIOC_S_CTRL(%s : %d) fail, %s\n", qctrl.name, value, strerror(errno));
		return ret;
	}

	return 0;
}

#define MAX_SUPPORTED_COMPONENTS	5

#define HAS_ARG_NUM(argc, argnow, need) ((nArgNow + need) < argc)

int main(int argc, char *argv[])
{
	int lErr = 0;
	int nArgNow = 1;
	int nCmdIdx = 0;
	int nHas2ndCmd;
	component_t component[MAX_SUPPORTED_COMPONENTS];
	component_t *pComponent;
	unsigned int i, j;
	uint32_t type = COMPONENT_TYPE_DECODER;	//COMPOENT_TYPE

	struct v4l2_event_subscription sub;
	struct v4l2_event evt;

	int r;
	struct pollfd p_fds;
	int wait_pollpri_times;
	int int_tmp;
	signal(SIGINT, SigIntHanlder);
	signal(SIGSTOP, SigStopHanlder);
	signal(SIGCONT, SigContHanlder);

	memset(&component[0], 0, MAX_SUPPORTED_COMPONENTS * sizeof(component_t));

HAS_2ND_CMD:

	nHas2ndCmd = 0;

	component[nCmdIdx].busType = -1;
	component[nCmdIdx].ports[STREAM_DIR_OUT].eMediaType = MEDIA_NULL_OUT;
	component[nCmdIdx].ports[STREAM_DIR_IN].fmt = 0xFFFFFFFF;
	component[nCmdIdx].ports[STREAM_DIR_OUT].fmt = 0xFFFFFFFF;
	component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr = NULL;
	pComponent = &component[nCmdIdx];

	if (argc >= 2 && strstr(argv[1], "help")) {
		showUsage();
		return 0;
	}

	while (nArgNow < argc) {
		if (!strcasecmp(argv[nArgNow], "IFILE")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_IN].eMediaType = MEDIA_FILE_IN;
			component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr =
			    malloc(sizeof(char) * (strlen(argv[nArgNow]) + 1));
			memset(component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr,
			       0x00, sizeof(char) * (strlen(argv[nArgNow]) + 1));
			memcpy(component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr,
			       argv[nArgNow], strlen(argv[nArgNow]));
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
		} else if (!strcasecmp(argv[nArgNow], "OFILE")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (strcasecmp(argv[nArgNow], "NONE")) {
				component[nCmdIdx].ports[STREAM_DIR_OUT].eMediaType =
				    MEDIA_FILE_OUT;
				component[nCmdIdx].ports[STREAM_DIR_OUT].pszNameOrAddr =
				    malloc(sizeof(char) * (strlen(argv[nArgNow]) + 1));
				memset(component[nCmdIdx].ports[STREAM_DIR_OUT].pszNameOrAddr,
				       0x00, sizeof(char) * (strlen(argv[nArgNow]) + 1));
				memcpy(component[nCmdIdx].ports[STREAM_DIR_OUT].pszNameOrAddr,
				       argv[nArgNow], strlen(argv[nArgNow]));
			}
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
		} else if (!strcasecmp(argv[nArgNow], "NULL")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_OUT].eMediaType = MEDIA_NULL_OUT;
		} else if (!strcasecmp(argv[nArgNow], "IFMT")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow]))
				component[nCmdIdx].ports[STREAM_DIR_IN].fmt = atoi(argv[nArgNow++]);	//uint32_t
		} else if (!strcasecmp(argv[nArgNow], "OFMT")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow])) {
				int_tmp = atoi(argv[nArgNow]);
				if (int_tmp >= 0 && int_tmp <= (ZPU_NUM_FORMATS_YUV - 1)) {
					component[nCmdIdx].ports[STREAM_DIR_OUT].fmt =
					    formats_yuv[int_tmp];
				} else {
					printf
					    ("error: %s() the specified output format is not support yet. \n",
					     __FUNCTION__);
					ret_err = 1;
					goto FUNC_END;
				}
				if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
					break;
				}
				nArgNow++;
			}
		} else if (!strcasecmp(argv[nArgNow], "OBIT")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow])) {
				component[nCmdIdx].ports[STREAM_DIR_OUT].outBit =
				    (VPU_OUT_BIT_FMT) abs(atoi(argv[nArgNow++]));
			}
		} else if (!strcasecmp(argv[nArgNow], "BS")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow])) {
				unsigned int fs = abs(atoi(argv[nArgNow++]));
				if (fs > 7900) {
					fs = 7900;
					printf("The maximum input buffer block size is 7900.\n");
				}
				component[nCmdIdx].ports[STREAM_DIR_IN].frame_size = fs * 1024 + 1;
			}
		} else if (!strcasecmp(argv[nArgNow], "IQC")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow])) {
				unsigned int iqc = abs(atoi(argv[nArgNow++]));
				if (iqc < 2) {
					iqc = 2;
					printf("The minimum qbuf count is 2.\n");
				} else if (iqc > 32) {
					iqc = 32;
					printf("The maximum qbuf count is 32.\n");
				}
				component[nCmdIdx].ports[STREAM_DIR_IN].buf_count = iqc;
			}
		} else if (!strcasecmp(argv[nArgNow], "OQC")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow])) {
				unsigned int oqc = abs(atoi(argv[nArgNow++]));
				if (oqc < 2) {
					oqc = 2;
					printf("The minimum qbuf count is 2.\n");
				} else if (oqc > 32) {
					oqc = 32;
					printf("The maximum qbuf count is 32.\n");
				}
				component[nCmdIdx].ports[STREAM_DIR_OUT].buf_count = oqc;
			}
		} else if (!strcasecmp(argv[nArgNow], "FRAMES")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow]))
				component[nCmdIdx].ports[STREAM_DIR_OUT].outFrameCount =
				    atoi(argv[nArgNow++]);
		} else if (!strcasecmp(argv[nArgNow], "LOOP")) {
			initLoopTimes = preLoopTimes = loopTimes = 10000;
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow]))
				loopTimes = atoi(argv[nArgNow++]);
			initLoopTimes = preLoopTimes = loopTimes;
		} else if (!strcasecmp(argv[nArgNow], "DEV")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			memset(component[nCmdIdx].szDevName, 0x00,
			       sizeof(component[nCmdIdx].szDevName));
			strcpy(component[nCmdIdx].szDevName, argv[nArgNow++]);
		} else if (!strcasecmp(argv[nArgNow], "DBG")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			if (isNumber(argv[nArgNow]))
				dec_dbg_bit = atoi(argv[nArgNow++]);
		} else if (!strcasecmp(argv[nArgNow], "+")) {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
			nHas2ndCmd = 1;
			break;
		} else {
			if (!HAS_ARG_NUM(argc, nArgNow, 1)) {
				break;
			}
			nArgNow++;
		}
	}
	if (component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr == NULL
	    || component[nCmdIdx].ports[STREAM_DIR_IN].fmt == 0xFFFFFFFF
	    || component[nCmdIdx].ports[STREAM_DIR_OUT].fmt == 0xFFFFFFFF) {
		printf("INPUT ERROR.\n\
Usage:\n\
	./mxc_v4l2_vpu_dec.out ifile decode.264 ifmt 1 ofmt 1 ofile test.yuv\n\n\
	./mxc_v4l2_vpu_dec.out ifile decode.264 ifmt 1 ofmt 1 ofile test.yuv\n\n\
	./mxc_v4l2_vpu_dec.out ifile decode.bit ifmt 13 ofmt 1 loop\n\n\
Or reference the usage manual.\n\
	./mxc_v4l2_vpu_dec.out --help \n\
	");
		ret_err = 1;
		goto FUNC_END;
	}

	pthread_mutex_init(&g_mutex, NULL);

	if (strstr(component[nCmdIdx].szDevName, "/dev/video")) {
		lErr =
		    check_video_device(component[nCmdIdx].devInstance,
				       &component[nCmdIdx].busType, &type,
				       component[nCmdIdx].szDevName);
		if (lErr) {
			printf
			    ("%s(): error: The selected device(%s) does not match VPU decoder!\nplease select: /dev/video12\n",
			     __FUNCTION__, component[nCmdIdx].szDevName);
			ret_err = 2;
			goto FUNC_END;
		}
	} else {
		// lookup and open the device
		lErr = lookup_video_device_node(component[nCmdIdx].devInstance,
						&component[nCmdIdx].busType, &type,
						component[nCmdIdx].szDevName);
		if (lErr) {
			printf("%s() error: Unable to find device.\n", __FUNCTION__);
			ret_err = 3;
			goto FUNC_END;
		}
	}
	component[nCmdIdx].hDev = open(component[nCmdIdx].szDevName, O_RDWR | O_NONBLOCK);
	if (component[nCmdIdx].hDev <= 0) {
		printf("%s() error: Unable to Open %s.\n", __FUNCTION__,
		       component[nCmdIdx].szDevName);
		ret_err = 4;
		goto FUNC_END;
	}
	// get the configuration
	lErr = zvconf(&component[nCmdIdx], component[nCmdIdx].pszScriptName, type);
	if (lErr) {
		printf("%s() error: Unable to config device.\n", __FUNCTION__);
		ret_err = 5;
		goto FUNC_END;
	}
	// subsribe v4l2 events
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	sub.type = V4L2_EVENT_SOURCE_CHANGE;
	lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr) {
		printf
		    ("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_SOURCE_CHANGE) ioctl failed %d %s\n",
		     __FUNCTION__, errno, strerror(errno));
		ret_err = 6;
		goto FUNC_END;
	}
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	sub.type = V4L2_EVENT_EOS;
	lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr) {
		printf
		    ("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_EOS) ioctl failed %d %s\n",
		     __FUNCTION__, errno, strerror(errno));
		ret_err = 7;
		goto FUNC_END;
	}
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	sub.type = V4L2_EVENT_CODEC_ERROR;
	lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr) {
		printf
		    ("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_CODEC_ERROR) ioctl failed %d %s\n",
		     __FUNCTION__, errno, strerror(errno));
		ret_err = 7;
		goto FUNC_END;
	}

	if (set_ctrl(pComponent->hDev, V4L2_CID_NON_FRAME, 1)) {
		/*To be compatible with previous versions */
		set_ctrl(pComponent->hDev, V4L2_CID_USER_BASE + 0x1100, 1);
		set_ctrl(pComponent->hDev, V4L2_CID_USER_BASE + 0x1109, 2);
	}

	lErr = pthread_create(&pComponent->ports[STREAM_DIR_IN].threadId,
			      NULL, (void *)test_streamin, pComponent);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_IN].ulThreadCreated = 1;
	} else {
		printf("%s() pthread create failed, threadId: %lu \n",
		       __FUNCTION__, pComponent->ports[STREAM_DIR_IN].threadId);
		ret_err = 8;
		goto FUNC_END;
	}

	// wait for 10 msec
	usleep(10000);

	// wait for resoltion change
	p_fds.fd = pComponent->hDev;
	p_fds.events = POLLPRI;

	wait_pollpri_times = 0;
	if (!g_unCtrlCReceived) {
		while (!g_unCtrlCReceived) {
			r = poll(&p_fds, 1, 2000);
			if (-1 == r) {
				fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
				g_unCtrlCReceived = 1;
				ret_err = 9;
				goto FUNC_END;
			} else if (0 == r) {
				wait_pollpri_times++;
			} else {
				if (p_fds.revents & POLLPRI) {
					break;
				} else {
					sleep(1);
					wait_pollpri_times++;
				}
			}
			if (wait_pollpri_times >= 30) {
				printf
				    ("error: %s(), waiting for the POLLPRI event response timeout.\n",
				     __FUNCTION__);
				g_unCtrlCReceived = 1;
				ret_err = 10;
				goto FUNC_END;
			}
		}
	}

	if (g_unCtrlCReceived) {
		goto FUNC_END;
	}
#ifdef DQEVENT
	memset(&evt, 0, sizeof(struct v4l2_event));
	lErr = ioctl(pComponent->hDev, VIDIOC_DQEVENT, &evt);
	if (lErr) {
		printf("%s() VIDIOC_DQEVENT ioctl failed %d %s\n", __FUNCTION__,
		       errno, strerror(errno));
		ret_err = 11;
		g_unCtrlCReceived = 1;
		goto FUNC_END;
	} else {
		switch (evt.type) {
		case V4L2_EVENT_SOURCE_CHANGE:
			printf
			    ("%s() VIDIOC_DQEVENT V4L2_EVENT_SOURCE_CHANGE changes(0x%x) pending(%d) seq(%d)\n",
			     __FUNCTION__, evt.u.src_change.changes, evt.pending, evt.sequence);
			break;
		case V4L2_EVENT_EOS:
			printf
			    ("%s() VIDIOC_DQEVENT V4L2_EVENT_EOS pending(%d) seq(%d)\n",
			     __FUNCTION__, evt.pending, evt.sequence);
			break;
		case V4L2_EVENT_CODEC_ERROR:
			printf
			    ("%s() VIDIOC_DQEVENT V4L2_EVENT_CODEC_ERROR pending(%d) seq(%d)\n",
			     __FUNCTION__, evt.pending, evt.sequence);
			g_unCtrlCReceived = 1;
			goto FUNC_END;
			break;
		default:
			printf("%s() VIDIOC_DQEVENT unknown event(%d)\n", __FUNCTION__, evt.type);
			break;
		}
	}
#endif

	lErr = pthread_create(&pComponent->ports[STREAM_DIR_OUT].threadId,
			      NULL, (void *)test_streamout, pComponent);
	if (!lErr) {
		pComponent->ports[STREAM_DIR_OUT].ulThreadCreated = 1;
	} else {
		printf("%s() pthread create failed, threadId: %lu \n",
		       __FUNCTION__, pComponent->ports[STREAM_DIR_OUT].threadId);
		ret_err = 12;
		g_unCtrlCReceived = 1;
		goto FUNC_END;
	}
	// wait for 10 msec
	usleep(10000);

	if (nHas2ndCmd) {
		nCmdIdx++;
		goto HAS_2ND_CMD;
	}
	// wait for user input
CHECK_USER_INPUT:
	while ((g_unCtrlCReceived == 0) && !kbhit()) {
		usleep(1000);
	}

	if (!g_unCtrlCReceived) {
		usleep(1000);
		goto CHECK_USER_INPUT;
	}

FUNC_END:
	// wait for 100 msec
	usleep(100000);
	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		for (j = 0; j < MAX_STREAM_DIR; j++) {
			if (component[i].ports[j].ulThreadCreated) {
				lErr = pthread_join(component[i].ports[j].threadId, NULL);
				if (lErr) {
					printf
					    ("warning: %s() pthread_join failed: errno(%d)  \n",
					     __FUNCTION__, lErr);
					ret_err = 13;
				}

				component[i].ports[j].ulThreadCreated = 0;
			}
		}
	}

	// unsubscribe v4l2 events
	memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		if (component[i].hDev > 0) {
			sub.type = V4L2_EVENT_ALL;
			lErr = ioctl(component[i].hDev, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
			if (lErr) {
				printf
				    ("%s() VIDIOC_UNSUBSCRIBE_EVENT ioctl failed %d %s\n",
				     __FUNCTION__, errno, strerror(errno));
			}
		}
	}

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++) {
		if (component[i].ports[STREAM_DIR_IN].pszNameOrAddr) {
			free(component[i].ports[STREAM_DIR_IN].pszNameOrAddr);
		}
		if (component[i].ports[STREAM_DIR_OUT].pszNameOrAddr) {
			free(component[i].ports[STREAM_DIR_OUT].pszNameOrAddr);
		}
		// close device
		if (component[i].hDev > 0) {
			close(component[i].hDev);
			component[i].hDev = 0;
		}

	}

	pthread_mutex_destroy(&g_mutex);
	printf("\nEND.\t loop_times=%d\n", (initLoopTimes - loopTimes));

	return (ret_err);
}
