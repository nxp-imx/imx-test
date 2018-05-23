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

volatile unsigned int g_unCtrlCReceived = 0;
unsigned  long time_total =0;
unsigned int num_total =0;
unsigned int frame_count = 0;
zoe_bool_t  seek_flag=0;
struct  timeval start;
struct  timeval end;

static __u32  formats_compressed[] = 
{
    VPU_PIX_FMT_LOGO,   //VPU_VIDEO_UNDEFINED = 0,
    V4L2_PIX_FMT_H264,  //VPU_VIDEO_AVC = 1,     ///< https://en.wikipedia.org/wiki/H.264/MPEG-4_AVC
    V4L2_PIX_FMT_VC1_ANNEX_G,   //VPU_VIDEO_VC1 = 2,     ///< https://en.wikipedia.org/wiki/VC-1
    V4L2_PIX_FMT_MPEG2, //VPU_VIDEO_MPEG2 = 3,   ///< https://en.wikipedia.org/wiki/H.262/MPEG-2_Part_2
    VPU_PIX_FMT_AVS,    //VPU_VIDEO_AVS = 4,     ///< https://en.wikipedia.org/wiki/Audio_Video_Standard
    VPU_PIX_FMT_ASP,    //VPU_VIDEO_ASP = 5,     ///< https://en.wikipedia.org/wiki/MPEG-4_Part_2
    V4L2_PIX_FMT_JPEG,  //VPU_VIDEO_JPEG = 6,    ///< https://en.wikipedia.org/wiki/JPEG
    VPU_PIX_FMT_RV8,    //VPU_VIDEO_RV8 = 7,     ///< https://en.wikipedia.org/wiki/RealVideo
    VPU_PIX_FMT_RV9,    //VPU_VIDEO_RV9 = 8,     ///< https://en.wikipedia.org/wiki/RealVideo
    VPU_PIX_FMT_VP6,    //VPU_VIDEO_VP6 = 9,     ///< https://en.wikipedia.org/wiki/VP6
    VPU_PIX_FMT_SPK,    //VPU_VIDEO_SPK = 10,    ///< https://en.wikipedia.org/wiki/Sorenson_Media#Sorenson_Spark
    V4L2_PIX_FMT_VP8,   //VPU_VIDEO_VP8 = 11,    ///< https://en.wikipedia.org/wiki/VP8
    V4L2_PIX_FMT_H264_MVC,  //VPU_VIDEO_AVC_MVC = 12,///< https://en.wikipedia.org/wiki/Multiview_Video_Coding
    VPU_PIX_FMT_HEVC,   //VPU_VIDEO_HEVC = 13,   ///< https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding
    VPU_PIX_FMT_VP9     //VPU_VIDEO_VP9 = 14,    ///< https://en.wikipedia.org/wiki/VP9
};
#define ZPU_NUM_FORMATS_COMPRESSED  SIZEOF_ARRAY(formats_compressed)


static __u32  formats_yuv[] = 
{
    V4L2_PIX_FMT_NV12,
    V4L2_PIX_FMT_YUV420M,
    V4L2_PIX_FMT_UYVY,
    VPU_PIX_FMT_TILED_8,
    VPU_PIX_FMT_TILED_10
};
#define ZPU_NUM_FORMATS_YUV SIZEOF_ARRAY(formats_yuv)

static void SigIntHanlder(int Signal)
{
    /*signal(SIGALRM, Signal);*/
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
	static struct termios	oldt, newt;

	if (dir == 1)
	{
		tcgetattr(STDIN_FILENO, &oldt);
		newt = oldt;
		newt.c_lflag &= ~(ICANON | ECHO);
		tcsetattr(STDIN_FILENO, TCSANOW, &newt);
	}
	else
	{
		tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	}
}

int kbhit(void)
{
	struct timeval	tv;
	fd_set			rdfs;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&rdfs);
	FD_SET(STDIN_FILENO, &rdfs);

	select(STDIN_FILENO + 1, &rdfs, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &rdfs);
}

int lookup_video_device_node(uint32_t devInstance,
							 uint32_t *p_busType,
                             uint32_t *p_devType,
							 char *pszDeviceName
							 )
{
	int			            hDev;
    int			            lErr;
	int			            nCnt = 0;
    ZVDEV_INFO	            devInfo;
    struct v4l2_capability  cap;
    char                    card[64];
	uint32_t                busType = -1;
    uint32_t                devType = -1;

    printf("%s()-> Requesting %d:%d\n", __FUNCTION__, devInstance, *p_busType);

	memset(&devInfo, 
           0xAA, 
           sizeof(ZVDEV_INFO)
           );

    while (nCnt < 64) 
	{
		sprintf(pszDeviceName, 
				"/dev/video%d", 
				nCnt
				);

		hDev = open(pszDeviceName, 
					O_RDWR
					);
        if (hDev >= 0) 
		{
            // query capability
			lErr = ioctl(hDev, 
						 VIDIOC_QUERYCAP, 
						 &cap
						 );
            // close the driver now
			close(hDev);
            if (0 == lErr)
            {
				printf("%s()-> drv(%s) card(%s) bus(%s) ver(0x%x) cap(0x%x) dev_cap(0x%x)\n",
					   __FUNCTION__,
                       cap.driver,
                       cap.card,
                       cap.bus_info,
                       cap.version,
                       cap.capabilities,
                       cap.device_caps
					   );

                if (0 == strcmp(cap.bus_info, "PCIe:"))
                {
                    busType = 0;
                }
                if (0 == strcmp(cap.bus_info, "platform:"))
                {
                    busType = 1;
                }

                if (0 == strcmp(cap.driver, "MX8 codec"))
                {
                    devType = COMPONENT_TYPE_CODEC;
                }
                if (0 == strcmp(cap.driver, "vpu B0"))
                {
                    devType = COMPONENT_TYPE_DECODER;
                }
                if (0 == strcmp(cap.driver, "vpu encoder"))
                {
                    devType = COMPONENT_TYPE_ENCODER;
                }

                // find the matching device
				if (-1 != *p_busType)
				{
					if (*p_busType != busType)
					{
						nCnt++;
						continue;
					}
				}

                if (-1 != *p_devType)
                {
                    if (*p_devType != devType)
                    {
						nCnt++;
						continue;
                    }
                }

                // instance
                snprintf(card, sizeof(cap.card) - 1, "%s", cap.driver);
                if (0 == strcmp(cap.card, card))
                {
                    *p_busType = busType;
                    *p_devType = devType;
					// got it
					break;
                }
            }
            else
            {
				printf("%s()-> %s ioctl VIDIOC_QUERYCAP failed(%d)\n", __FUNCTION__, pszDeviceName, lErr);
            }
		}
        else
        {
			printf("%s()-> open(%s) failed hDev(%d) errno(%d)\n", __FUNCTION__, pszDeviceName, hDev, errno);
        }
		nCnt++;
	}

	if (64 == nCnt) 
	{
		// return empty device name
		*pszDeviceName = 0;
		return (-1);
	}
	else
	{
		return (0);
	}
}

int zvconf(component_t *pComponent,
		   char *scrfilename,
           uint32_t type
		   )
{
    // setup port type and open format
    switch (type)
    {
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
	        pComponent->ports[STREAM_DIR_IN].frame_size = 256 * 1024 * 3 +1;
	        pComponent->ports[STREAM_DIR_IN].buf_count = 6;
	        pComponent->ports[STREAM_DIR_OUT].portType = COMPONENT_PORT_YUV_OUT;
	        pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nWidth = 1920;
	        pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nHeight = 1088;
	        pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nBitCount = 12;
	        pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
	        pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nFrameRate = 30;
	        pComponent->ports[STREAM_DIR_OUT].frame_size = (1920 * 1088 * 3) / 2;
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
    printf("  %s <command1> <arg1> <arg2> ... [+ <command2> <arg1> <arg2> ...] \n", MODULE_NAME);
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

//#define PRECISION_8BIT // 10 bits if not defined
// Read 8-bit FSL stored image
// Format is based on ratser array of tiles, where a tile is 1KB = 8x128
static unsigned int ReadYUVFrame_FSL_8b ( unsigned int nPicWidth, 
                                        unsigned int nPicHeight, 
                                        unsigned int uVOffsetLuma,
                                        unsigned int uVOffsetChroma,
                                        unsigned int nFsWidth,  
                                        uint8_t **nBaseAddr,
										uint8_t *pDstBuffer,
                                        unsigned int     bInterlaced
                                      )
{
	unsigned int i;
	unsigned int h_tiles, v_tiles, v_offset, nLines, vtile, htile;
	unsigned int nLinesLuma  = nPicHeight;
	unsigned int nLinesChroma = nPicHeight >> 1;
	uint8_t      *cur_addr;
	unsigned int *pBuffer = (unsigned int *)pDstBuffer;
	unsigned int *pTmpBuffer;

	pTmpBuffer = (unsigned int*)malloc(256 * 4);
	if (!pTmpBuffer) {
		printf("failed to alloc pTmpBuffer\r\n");
		return -1;
	}

	if (bInterlaced)
	{
		// Per field no effect??
		nLinesLuma   >>= 1;
		nLinesChroma >>= 1;
	}

	// Read Top Luma
	nLines    = nLinesLuma;
	v_offset  = uVOffsetLuma;
	h_tiles   = (nPicWidth + 7) >> 3;
	v_tiles   = (nLines + v_offset + 127) >> 7;

	for (vtile = 0; vtile < v_tiles; vtile++)
	{
		unsigned int v_base_offset = nFsWidth * 128 * vtile;

		for (htile = 0; htile < h_tiles; htile++)
		{
			cur_addr = (uint8_t *)(nBaseAddr[0] + htile * 1024 + v_base_offset);
			memcpy(pTmpBuffer, cur_addr, 256 * 4);

			for (i = 0; i < 128; i++)
			{
				int line_num  = (i + 128 * vtile) - v_offset;
				unsigned int line_base = (line_num * nPicWidth) >> 2;
				// Skip data that is off the bottom of the pic
				if (line_num == nLines)
					break;
				// Skip data that is off the top of the pic
				if (line_num < 0)
					continue;
				pBuffer[line_base + (2 * htile) + 0] = pTmpBuffer[2 * i + 0];
				pBuffer[line_base + (2 * htile) + 1] = pTmpBuffer[2 * i + 1];
			}        
		}
	}

	if (bInterlaced)
	{
		pBuffer += (nPicWidth * nPicHeight) >> 3;

		// Read Bot Luma
		nLines = nLinesLuma;
		v_offset = uVOffsetLuma;
		h_tiles = (nPicWidth + 7) >> 3;
		v_tiles = (nLines + v_offset + 127) >> 7;

		for (vtile = 0; vtile < v_tiles; vtile++)
		{
			unsigned int v_base_offset = nFsWidth * 128 * vtile;

			for (htile = 0; htile < h_tiles; htile++)
			{
				cur_addr = (uint8_t *)(nBaseAddr[2] + htile * 1024 + v_base_offset);
				memcpy(pTmpBuffer, cur_addr, 256 * 4);

				for (i = 0; i < 128; i++)
				{
					int line_num = (i + 128 * vtile) - v_offset;
					unsigned int line_base = (line_num * nPicWidth) >> 2;
					// Skip data that is off the bottom of the pic
					if (line_num == nLines)
						break;
					// Skip data that is off the top of the pic
					if (line_num < 0)
						continue;
					pBuffer[line_base + (2 * htile) + 0] = pTmpBuffer[2 * i + 0];
					pBuffer[line_base + (2 * htile) + 1] = pTmpBuffer[2 * i + 1];
				}
			}
		}

		pBuffer += (nPicWidth * nPicHeight) >> 3;
	}
	else
	{ 
		pBuffer += (nPicWidth * nPicHeight) >> 2;
	}

	// Read Top Chroma
	nLines    = nLinesChroma;
	v_offset  = uVOffsetChroma;
	h_tiles = (nPicWidth + 7) >> 3;
	v_tiles = (nLines + v_offset + 127) >> 7;
	for (vtile = 0; vtile < v_tiles; vtile++)
	{
		unsigned int v_base_offset = nFsWidth * 128 * vtile;

		for (htile = 0; htile < h_tiles; htile++)
		{
			cur_addr = (uint8_t *)(nBaseAddr[1] + htile * 1024 + v_base_offset);
			memcpy(pTmpBuffer, cur_addr, 256 * 4);

			for (i = 0; i < 128; i++)
			{
				int line_num = (i + 128 * vtile) - v_offset;
				unsigned int line_base = (line_num * nPicWidth) >> 2;
				// Skip data that is off the bottom of the pic
				if (line_num == nLines)
					break;
				// Skip data that is off the top of the pic
				if (line_num < 0)
					continue;
				pBuffer[line_base + (2 * htile) + 0] = pTmpBuffer[2 * i + 0];
				pBuffer[line_base + (2 * htile) + 1] = pTmpBuffer[2 * i + 1];
			}
		}
	}

	if (bInterlaced)
	{
		pBuffer += (nPicWidth * nPicHeight) >> 4;

		// Read Bot Chroma
		nLines = nLinesChroma;
		v_offset = uVOffsetChroma;
		h_tiles = (nPicWidth + 7) >> 3;
		v_tiles = (nLines + v_offset + 127) >> 7;
		for (vtile = 0; vtile < v_tiles; vtile++)
		{
			unsigned int v_base_offset = nFsWidth * 128 * vtile;

			for (htile = 0; htile < h_tiles; htile++)
			{
				cur_addr = (uint8_t *)(nBaseAddr[3] + htile * 1024 + v_base_offset);
				memcpy(pTmpBuffer, cur_addr, 256 * 4);

				for (i = 0; i < 128; i++)
				{
					int line_num = (i + 128 * vtile) - v_offset;
					unsigned int line_base = (line_num * nPicWidth) >> 2;
					// Skip data that is off the bottom of the pic
					if (line_num == nLines)
						break;
					// Skip data that is off the top of the pic
					if (line_num < 0)
						continue;
					pBuffer[line_base + (2 * htile) + 0] = pTmpBuffer[2 * i + 0];
					pBuffer[line_base + (2 * htile) + 1] = pTmpBuffer[2 * i + 1];
				}
			}
		}
	}

	free(pTmpBuffer);
	return(0);
}

// Read 10bit packed FSL stored image
// Format is based on ratser array of tiles, where a tile is 1KB = 8x128
static unsigned int ReadYUVFrame_FSL_10b ( unsigned int nPicWidth, 
                                       unsigned int nPicHeight, 
                                       unsigned int uVOffsetLuma,
                                       unsigned int uVOffsetChroma,
                                       unsigned int nFsWidth,  
                                       unsigned char **nBaseAddr,
                                       unsigned char *pDstBuffer,
                                       unsigned int     bInterlaced
                                     )
{
#define RB_WIDTH (4096*5/4/4)
	unsigned int (*pRowBuffer)[RB_WIDTH];
	unsigned int i;
	unsigned int h_tiles,v_tiles,v_offset,nLines,vtile,htile;
	unsigned int nLinesLuma  = nPicHeight;
	unsigned int nLinesChroma = nPicHeight>>1;
	unsigned char *cur_addr;
#ifdef PRECISION_8BIT
	unsigned char *pBuffer = (unsigned char *)pDstBuffer;
#else
	uint16_t *pBuffer = (uint16_t *)pDstBuffer;
#endif
	unsigned int *pTmpBuffer;
	unsigned int pix;

	pRowBuffer = malloc(128 * RB_WIDTH * 4);
	if (!pRowBuffer) {
		printf("failed to alloc pRowBuffer\n");
		return -1;
	}

	pTmpBuffer = malloc(256 * 4);
	if (!pTmpBuffer) {
		printf("failed to alloc pTmpBuffer\n");
		return -1;
	}

	// Read Luma
	nLines    = nLinesLuma;
	v_offset  = uVOffsetLuma;
	h_tiles   = ((nPicWidth*5/4) + 7) >>3;  // basically number of 8-byte words across the pic
	v_tiles   = (nLines+v_offset+127) >>7;  // basically number of 128 line groups

	for (vtile=0; vtile<v_tiles; vtile++)
	{
		unsigned int v_base_offset = nFsWidth*128*vtile;

		// Read Row of tiles as 10-bit
		for (htile=0; htile<h_tiles;htile++)
		{
			cur_addr = (unsigned char *)(nBaseAddr[0] + htile*1024 + v_base_offset);
			memcpy(pTmpBuffer, cur_addr, 256 * 4);

			// Expand data into Rows
			for (i = 0; i < 128; i++)
			{
				pRowBuffer[i][(2 * htile) + 0] = pTmpBuffer[2 * i + 0];
				pRowBuffer[i][(2 * htile) + 1] = pTmpBuffer[2 * i + 1];
			}
		}

		// Convert the 10-bit data to 8-bit, by dropping the LSBs
		for (i = 0; i < 128; i++)
		{
			unsigned char * pRow = (unsigned char *)&pRowBuffer[i];    // Pointer to start of row
			int line_num = (i + 128 * vtile) - v_offset;  // line number in the potentially vertically offset image
			unsigned int line_base = (line_num * nPicWidth);       // location of first pixel in the line in byte units

			// Skip data that is off the bottom of the pic
			if (line_num == nLines)
				break;
			// Skip data that is off the top of the pic
			if (line_num < 0)
				continue;

			// Convert and store 1 pixel at a time across the row
			for (pix = 0; pix<nPicWidth; pix++)
			{
				unsigned int bit_pos         = 10 * pix;                                   // First bit of pixel across the packed line
				unsigned int byte_loc        = bit_pos / 8;                                // Byte containing the first bit        
				unsigned int bit_loc         = bit_pos % 8;                                // First bit location in the firstbyte counting down from MSB!
				uint16_t two_bytes  = (pRow[byte_loc] << 8) | pRow[byte_loc + 1]; // The 2 bytes conaining the pixel
#ifdef PRECISION_8BIT
				unsigned char  the_pix    = two_bytes >> (8 - bit_loc);                 // Align the 8 MSBs to the LSB ans store in a char
				// Store the converted pixel
				pBuffer[line_base + pix] = the_pix;
#else
				pBuffer[line_base + pix] = (two_bytes >> (6 - bit_loc)) & 0x3FF;
#endif
				if ((line_num == 0) && (pix < 4))
					printf("10b 0x%x\n", (two_bytes >> (6 - bit_loc)) & 0x3FF);
			}
		}
	}

	pBuffer += (nPicWidth * nPicHeight);

	// Read Chroma
	nLines    = nLinesChroma;
	v_offset  = uVOffsetChroma;
	h_tiles   = ((nPicWidth*5/4) + 7) >>3;  // basically number of 8-byte words across the pic
	v_tiles   = (nLines+v_offset+127) >>7;  // basically number of 128 line groups
	for (vtile = 0; vtile<v_tiles; vtile++)
	{
		unsigned int v_base_offset = nFsWidth*128*vtile;

		for (htile=0; htile<h_tiles;htile++)
		{
			cur_addr = (unsigned char *)(nBaseAddr[1] + htile * 1024 + v_base_offset);
			memcpy(pTmpBuffer, cur_addr, 256 * 4);

			// Expand data into Rows
			for (i = 0; i < 128; i++)
			{
				pRowBuffer[i][(2 * htile) + 0] = pTmpBuffer[2 * i + 0];
				pRowBuffer[i][(2 * htile) + 1] = pTmpBuffer[2 * i + 1];
			}
		}

		// Convert the 10-bit data to 8-bit, by dropping the LSBs
		for (i = 0; i < 128; i++)
		{
			unsigned char * pRow = (unsigned char *)&pRowBuffer[i];    // Pointer to start of row
			int line_num = (i + 128 * vtile) - v_offset;  // line number in the potentially vertically offset image
			unsigned int line_base = (line_num * nPicWidth);       // location of first pixel in the line in byte units

			// Skip data that is off the bottom of the pic
			if (line_num == nLines)
				break;
			// Skip data that is off the top of the pic
			if (line_num < 0)
				continue;

			// Convert and store 1 pixel at a time across the row
			for (pix = 0; pix<nPicWidth; pix++)
			{
				unsigned int bit_pos         = 10 * pix;                                   // First bit of pixel across the packed line
				unsigned int byte_loc        = bit_pos / 8;                                // Byte containing the first bit        
				unsigned int bit_loc         = bit_pos % 8;                                // First bit location in the firstbyte counting down from MSB!
				uint16_t two_bytes  = (pRow[byte_loc] << 8) | pRow[byte_loc + 1]; // The 2 bytes conaining the pixel
#ifdef PRECISION_8BIT
				unsigned char  the_pix    = two_bytes >> (8 - bit_loc);                 // Align the 8 MSBs to the LSB ans store in a char
				// Store the converted pixel
				pBuffer[line_base + pix] = the_pix;
#else
				pBuffer[line_base + pix] = (two_bytes >> (6 - bit_loc)) & 0x3FF;
#endif
			}
		}
	}
	free(pRowBuffer);
	free(pTmpBuffer);
	return(0);
}  

static void LoadFrameNV12 (unsigned char *pFrameBuffer, unsigned char *pYuvBuffer, unsigned int nFrameWidth, unsigned int nFrameHeight, unsigned int nSizeLuma, unsigned int bMonochrome, unsigned int bInterlaced)
{
	unsigned int nSizeUorV    = nSizeLuma >> 2;
	unsigned char *pYSrc  = pFrameBuffer;
	unsigned char *pUVSrc = pYSrc + nSizeLuma;

	unsigned char *pYDst  = pYuvBuffer;
	unsigned char *pUDst  = pYuvBuffer + nSizeLuma;
	unsigned char *pVDst  = pYuvBuffer + nSizeLuma + nSizeUorV;

	if ((!bInterlaced) || bMonochrome)
	{
		memcpy (pYDst, pYSrc, nSizeLuma);
		if (bMonochrome)
		{
			memset (pUDst, 128, nSizeUorV);
			memset (pVDst, 128, nSizeUorV);
		}
		else
		{
			unsigned char *pLast = pVDst;
			while (pUDst < pLast)
			{
				*pUDst++ = *pUVSrc++;
				*pVDst++ = *pUVSrc++;
			}
		}
	}
	else
	{
		// FrameBuffer is organized as below for 4:2:0 interlaced 
		// TopFild_Y, BotFld_Y, TopFild_U, BotFld_U, TopFild_V, BotFld_V
		// unsigned char *pTopFld, *pBotFld, *pTopFldV, *pBotFldV, *pLast;
		unsigned char *pTopFld, *pBotFld, *pLast;
		unsigned int n;
		// Luma
		pTopFld = pYSrc;
		pBotFld = pYSrc + (nSizeLuma >> 1);
		for (n = 0; n < nFrameHeight; n++)
		{
			if (n & 0x1)
			{
				memcpy (pYDst, pBotFld, nFrameWidth);
				pBotFld += nFrameWidth;
			}
			else
			{
				memcpy (pYDst, pTopFld, nFrameWidth);
				pTopFld += nFrameWidth;
			}
			pYDst += nFrameWidth;
		}
		// Chroma
		//4:2:0 interleaved
		pTopFld = pUVSrc;
		pBotFld = pUVSrc + nSizeUorV;

		for (n = 0; n < (nFrameHeight>>1); n++)
		{
			pLast = pUDst + (nFrameWidth>>1);

			if (n & 0x1)
			{
				while (pUDst < pLast)
				{
					*pUDst++ = *pBotFld++;
					*pVDst++ = *pBotFld++;
				}
			}
			else
			{
				while (pUDst < pLast)
				{
					*pUDst++ = *pTopFld++;
					*pVDst++ = *pTopFld++;
				}		    
			}
		} //end for loop
	}
}

static void LoadFrameNV12_10b (unsigned char *pFrameBuffer, unsigned char *pYuvBuffer, unsigned int nFrameWidth, unsigned int nFrameHeight, unsigned int nSizeLuma, unsigned int bMonochrome, unsigned int bInterlaced)
{
	unsigned int nSizeUorV    = nSizeLuma >> 2;
#ifdef PRECISION_8BIT
	unsigned char *pYSrc  = pFrameBuffer;
	unsigned char *pUVSrc = pYSrc + nSizeLuma;

	unsigned char *pYDst  = pYuvBuffer;
	unsigned char *pUDst  = pYuvBuffer + nSizeLuma;
	unsigned char *pVDst  = pYuvBuffer + nSizeLuma + nSizeUorV;

	unsigned char *pLast = pVDst;
#else
	uint16_t *pYSrc  = (uint16_t *)pFrameBuffer;
	uint16_t *pUVSrc = pYSrc + nSizeLuma;

	uint16_t *pYDst  = (uint16_t *)pYuvBuffer;
	uint16_t *pUDst  = (uint16_t *)pYuvBuffer + nSizeLuma;
	uint16_t *pVDst  = (uint16_t *)pYuvBuffer + nSizeLuma + nSizeUorV;

	uint16_t *pLast = pVDst;
#endif

#ifdef PRECISION_8BIT
	memcpy (pYDst, pYSrc, nSizeLuma);
#else
	memcpy (pYDst, pYSrc, nSizeLuma * 2);
#endif

	while (pUDst < pLast)
	{
		*pUDst++ = *pUVSrc++;
		*pVDst++ = *pUVSrc++;
	}
}

void test_streamout(component_t *pComponent)
{
	int							lErr = 0;
	FILE						*fpOutput = 0;

	struct zvapp_v4l_buf_info	*stAppV4lBuf = pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf;
	struct v4l2_buffer			stV4lBuf;
    struct v4l2_plane           stV4lPlanes[3];
	int							nV4lBufCnt;

	unsigned int				ulXferBufCnt = 0;

	unsigned int				ulWidth;
	unsigned int				ulHeight;

	int							frame_nb;

    fd_set                      fds;
    struct timeval              tv;
    int                         r;
    struct v4l2_event           evt;

    int                         i;

	printf("%s() [\n", __FUNCTION__);

	ulWidth = pComponent->ulWidth;
	ulHeight = pComponent->ulHeight;
	frame_nb = pComponent->ports[STREAM_DIR_OUT].buf_count;

    /***********************************************
    ** 1> Open output file descriptor
    ***********************************************/
	
	
	if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT)
	{
		fpOutput = fopen(pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr, "w+");
		if (fpOutput == NULL)
		{
			printf("%s() Unable to open file %s.\n", __FUNCTION__, pComponent->ports[STREAM_DIR_OUT].pszNameOrAddr);
			lErr = 1;
			goto FUNC_END;
		}
	}
	else if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_NULL_OUT)
	{
		// output to null
	}
	else
	{
		printf("%s() Unknown media type %d.\n", __FUNCTION__, pComponent->ports[STREAM_DIR_OUT].eMediaType);
		goto FUNC_END;
	}


	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived)
	{
		if (pComponent->ports[STREAM_DIR_OUT].unStarted)
		{
			/***********************************************
			** QBUF, send all the buffers to driver
			***********************************************/
	
			if(seek_flag==1){
			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
			{
				stAppV4lBuf[nV4lBufCnt].sent = 0;
			}
			seek_flag = 0;
			}
			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
			{
				if (!stAppV4lBuf[nV4lBufCnt].sent)
				{
					for (i = 0; i < stAppV4lBuf[nV4lBufCnt].stV4lBuf.length; i++)
					{
						stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused = 0;
						stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset = 0;
					}
					lErr = ioctl(pComponent->hDev, VIDIOC_QBUF, &stAppV4lBuf[nV4lBufCnt].stV4lBuf);
					if (lErr)
					{
						printf("%s() QBUF ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
						if (errno == EAGAIN)
						{
							lErr = 0;
						}
						break;
					}
					else
					{
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
           	tv.tv_sec = 1;
			tv.tv_usec = 0;

			r = select(pComponent->hDev + 1, &fds, NULL, NULL, &tv);

			if (-1 == r) 
			{
				fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
				continue;
			}
			if (0 == r) 
			{
			    //continue;
				FD_ZERO(&fds);
				FD_SET(pComponent->hDev, &fds);

				// Timeout
				tv.tv_sec = 3;
				tv.tv_usec = 0;

				r = select(pComponent->hDev + 1, NULL, NULL, &fds, &tv);
				if (-1 == r)
				{
					fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
					continue;
				}
				if (0 == r)
				{
					continue;
				}

				memset(&evt, 0, sizeof(struct v4l2_event));
				lErr = ioctl(pComponent->hDev, VIDIOC_DQEVENT, &evt);
				if (lErr)
				{
					printf("%s() VIDIOC_DQEVENT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
					continue;
				}
				else
				{
					if(evt.type == V4L2_EVENT_EOS) {
						printf("V4L2_EVENT_EOS is called\n");
						g_unCtrlCReceived = 1;
						gettimeofday(&end,NULL);
						break;
					}
					else
						printf("%s() VIDIOC_DQEVENT type=%d\n", __FUNCTION__, V4L2_EVENT_EOS);
					continue;
				}
			}

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.m.planes = stV4lPlanes;
			stV4lBuf.length = 2;
			
			lErr = ioctl(pComponent->hDev, VIDIOC_DQBUF, &stV4lBuf);

			if (!lErr)
			{
				// clear sent flag
				stAppV4lBuf[stV4lBuf.index].sent = 0;

				frame_count++;
				printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r\n", pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused, stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags, stV4lBuf.timestamp.tv_sec);
				if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT)
				{
					{
						unsigned int bytesused;
						unsigned char *nBaseAddr[4];
						unsigned char *dstbuf, *yuvbuf;
						unsigned int uStride;
						unsigned int uVertAlign = 512 - 1;
						unsigned char *ybuf, *uvbuf;
						zoe_bool_t b10format = 0;
						unsigned int bField = 0;
						uStride = (( ulWidth  + uVertAlign ) & ~uVertAlign );
						dstbuf = (unsigned char *)malloc(ulWidth * ulHeight*2*3/2);
						if(!dstbuf)
						{
							printf("dstbuf alloc failed\n");
							goto FUNC_END;
						}
						yuvbuf = (unsigned char *)malloc(ulWidth * ulHeight*2*3/2);
						if(!yuvbuf)
						{
							printf("yuvbuf alloc failed\n");
							goto FUNC_END;
						}

						nBaseAddr[0] = (unsigned char *)(stAppV4lBuf[stV4lBuf.index].addr[0] + stV4lBuf.m.planes[0].data_offset);
						nBaseAddr[1] = (unsigned char *)(stAppV4lBuf[stV4lBuf.index].addr[1] + stV4lBuf.m.planes[1].data_offset);
						nBaseAddr[2] = nBaseAddr[0] + ulWidth * ulHeight/2;
						nBaseAddr[3] = nBaseAddr[1] + ulWidth * ulHeight/4;
						if (b10format) {
							ReadYUVFrame_FSL_10b(ulWidth, ulHeight, 0, 0, uStride, nBaseAddr, yuvbuf, 0);
							LoadFrameNV12_10b (yuvbuf, dstbuf, ulWidth, ulHeight, ulWidth * ulHeight, 0, 0);
						} else {
							ReadYUVFrame_FSL_8b(ulWidth, ulHeight, 0, 0, uStride, nBaseAddr, yuvbuf, bField);
							LoadFrameNV12(yuvbuf, dstbuf, ulWidth, ulHeight, ulWidth * ulHeight, 0, bField);
						}
						ybuf = dstbuf;
						uvbuf = dstbuf + ulWidth * ulHeight;

						for (i = 0; i < stV4lBuf.length; i++) {

							if (0 == i)
							{
								bytesused = ulWidth * ulHeight;
#ifndef PRECISION_8BIT
								if (b10format)
									bytesused = bytesused * 2;
#endif
								fwrite((void*)ybuf, 1, bytesused, fpOutput);
							}
							else
							{
								bytesused = (ulWidth * ulHeight) / 2;
								fwrite((void*)uvbuf, 1, bytesused, fpOutput);
							}
						}
					free(dstbuf);
					free(yuvbuf);
					}
				}
				else if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_NULL_OUT)
				{
				}
				fflush(stdout);
			}
			else
			{
				if (errno == EAGAIN)
				{
					lErr = 0;
				}
				else
				{
					printf("\r%s()  DQBUF failed(%d) errno(%d)\n", __FUNCTION__, lErr, errno);
				}
			}

			if (pComponent->ports[STREAM_DIR_OUT].unCtrlCReceived)
			{
				printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
				break;
			}

   		}
	}


FUNC_END:
	printf("\n");
	usleep(1000);

	if (fpOutput)
	{
		fclose(fpOutput);
	}

	pComponent->ports[STREAM_DIR_OUT].unStarted = 0;

	printf("%s() ]\n", __FUNCTION__);
}

void test_streamin(component_t *pComponent)
{
	int							lErr = 0;
	FILE						*fpInput = 0;

	struct zvapp_v4l_buf_info	*stAppV4lBuf = pComponent->ports[STREAM_DIR_IN].stAppV4lBuf;
	struct v4l2_buffer			stV4lBuf;
    struct v4l2_plane           stV4lPlanes[3];
	struct v4l2_buffer			*pstV4lBuf = NULL;
	int							nV4lBufCnt;
	struct v4l2_decoder_cmd     v4l2cmd;

	unsigned int				ulIOBlockSize;

	int							frame_size;
	int							frame_nb;

    fd_set                      fds;
    struct timeval              tv;
    int                         r;

    int                         i;
    unsigned int                total;
    int                         first = pComponent->ports[STREAM_DIR_IN].buf_count;
	long                        file_size;

	printf("%s() [\n", __FUNCTION__);

	frame_nb = pComponent->ports[STREAM_DIR_IN].buf_count;
    ulIOBlockSize = frame_size = pComponent->ports[STREAM_DIR_IN].frame_size;

	/***********************************************
	** 1> Open output file descriptor
	***********************************************/
	if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN)
	{
		fpInput = fopen(pComponent->ports[STREAM_DIR_IN].pszNameOrAddr, "r+");
		if (fpInput == NULL)
		{
            printf("%s() Unable to open file %s.\n", __FUNCTION__, pComponent->ports[STREAM_DIR_IN].pszNameOrAddr);
			lErr = 1;
			goto FUNC_END;
		}
        else
        {
			fseek(fpInput, 0, SEEK_END);
			file_size = ftell(fpInput);
		    fseek(fpInput, 0, SEEK_SET);
        }
	}
	else
	{
		goto FUNC_END;
	}


	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!pComponent->ports[STREAM_DIR_IN].unCtrlCReceived)
	{
		if ((pComponent->ports[STREAM_DIR_IN].unStarted) &&
            ((first > 0) || pComponent->ports[STREAM_DIR_OUT].unStarted)
            )
		{
            int buf_avail = 0;

            if (first > 0)
            {
                first--;
            }

			/***********************************************
			** DQBUF, get buffer from driver
			***********************************************/

			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
			{
				if (!stAppV4lBuf[nV4lBufCnt].sent)
				{
                    buf_avail = 1;
                    break;
                }
			}

            if (!buf_avail)
            {
                FD_ZERO(&fds);
                FD_SET(pComponent->hDev, &fds);

                // Timeout
                tv.tv_sec = 2;
                tv.tv_usec = 0;

                r = select(pComponent->hDev + 1, NULL, &fds, NULL, &tv);

                if (-1 == r) 
                {
                    fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
                    continue;
                }
                if (0 == r) 
                {
                    continue;
                }

				stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
				stV4lBuf.memory = V4L2_MEMORY_MMAP;
                stV4lBuf.m.planes = stV4lPlanes;
	            stV4lBuf.length = 1;
				lErr = ioctl(pComponent->hDev, VIDIOC_DQBUF, &stV4lBuf);
				if (!lErr)
				{
					stAppV4lBuf[stV4lBuf.index].sent = 0;
				}
            }

			if (lErr)
			{
				if (errno == EAGAIN)
				{
					lErr = 0;
				}
			}

			/***********************************************
			** get empty buffer and read data
			***********************************************/
			pstV4lBuf = NULL;

			for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
			{
				if (!stAppV4lBuf[nV4lBufCnt].sent)
				{
					pstV4lBuf = &stAppV4lBuf[nV4lBufCnt].stV4lBuf;
					break;
				}
			}

			if (pstV4lBuf)
			{
				char            *pBuf;
                unsigned int    block_size;

RETRY:
				if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN)
				{
                    for (i = 0; i < pstV4lBuf->length; i++)
                    {
				        pBuf = stAppV4lBuf[pstV4lBuf->index].addr[i];
                        block_size = stAppV4lBuf[pstV4lBuf->index].size[i];
					    pstV4lBuf->m.planes[i].bytesused = fread((void*)pBuf, 1, block_size, fpInput);
					    pstV4lBuf->m.planes[i].data_offset = 0;
						file_size -= pstV4lBuf->m.planes[i].bytesused;
						if (file_size == 0)
						{
							v4l2cmd.cmd = V4L2_DEC_CMD_STOP;
							lErr = ioctl(pComponent->hDev, VIDIOC_DECODER_CMD, &v4l2cmd);
							if (lErr)
								printf("VIDIOC_DECODER_CMD has error\n");
						}

						if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory)
						{
							msync((void*)pBuf, block_size, MS_SYNC);
						}
                    }
				}

                total = 0;
                ulIOBlockSize = 0;
                for (i = 0; i < pstV4lBuf->length; i++)
                {
                    total += pstV4lBuf->m.planes[i].bytesused;
                    ulIOBlockSize += stAppV4lBuf[pstV4lBuf->index].size[i];
                }

				if ((pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN) &&
					(total != ulIOBlockSize)
					)
				{
					if ((pComponent->ports[STREAM_DIR_IN].portType == COMPONENT_PORT_COMP_IN) ||
						(pComponent->ports[STREAM_DIR_IN].portType == COMPONENT_PORT_YUV_IN)
						)
					{
                        if (pComponent->ports[STREAM_DIR_IN].auto_rewind)
                        {
						    fseek(fpInput, 0, SEEK_SET);
                        }
					}
					else
					{
						pComponent->ports[STREAM_DIR_IN].unCtrlCReceived = 1;
					}
				}

				if (total != 0)
				{
					memcpy(&stV4lBuf, pstV4lBuf, sizeof(struct v4l2_buffer));

					if (pComponent->ports[STREAM_DIR_IN].unUserPTS)
					{
                        struct timespec now;
                        clock_gettime (CLOCK_MONOTONIC, &now);
                        stV4lBuf.timestamp.tv_sec = now.tv_sec;
                        stV4lBuf.timestamp.tv_usec = now.tv_nsec / 1000;	
	                    stV4lBuf.flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
					}
					else
					{
						// not use PTS
	                    stV4lBuf.flags &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
					}

					/***********************************************
					** QBUF, put data to driver
					***********************************************/
					lErr = ioctl(pComponent->hDev, VIDIOC_QBUF, &stV4lBuf);
					if (lErr)
					{
						if (errno == EAGAIN)
						{
						    printf("\n");
							lErr = 0;
						}
                        else
                        {
						    printf("v4l2_buf index(%d) type(%d) memory(%d) sequence(%d) length(%d) planes(%p)\n",
                                stV4lBuf.index,
                                stV4lBuf.type,
                                stV4lBuf.memory,
                                stV4lBuf.sequence,
                                stV4lBuf.length,
                                stV4lBuf.m.planes
                                );
                        }
					}
					else
					{
						stAppV4lBuf[stV4lBuf.index].sent = 1;
					}
				}
				else
				{
					if (pComponent->ports[STREAM_DIR_IN].unCtrlCReceived)
					{
						printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
						break;
					}

					usleep(1000);
					goto RETRY;
				}

				fflush(stdout);
			}

			if (pComponent->ports[STREAM_DIR_IN].unCtrlCReceived)
			{
				printf("\n\n%s() CTRL+C received.\n", __FUNCTION__);
				break;
			}

			if (pComponent->ports[STREAM_DIR_IN].eMediaType == MEDIA_FILE_IN)
			{
				usleep(1000);
			}
			else
			{
				usleep(1000);
			}
		}
        else
        {
			usleep(30000);
        }
    }


FUNC_END:

	printf("\n");

	usleep(10000);

	if (fpInput)
	{
		fclose(fpInput);
	}

    pComponent->ports[STREAM_DIR_IN].unStarted = 0;

    //return lErr;
	printf("%s() ]\n", __FUNCTION__);
}

#define MAX_SUPPORTED_COMPONENTS	5

#define HAS_ARG_NUM(argc, argnow, need) ((nArgNow + need) < argc)

int main(int argc,
		 char* argv[]
		 )
{
	int			                lErr = 0;
	int			                nArgNow = 1;
	int			                nCmdIdx = 0;
	int			                nHas2ndCmd;
	component_t	                component[MAX_SUPPORTED_COMPONENTS];
    component_t                 *pComponent;
	int			                nType;
	int			                i, j, k;
    uint32_t                    type = COMPONENT_TYPE_DECODER;

	struct v4l2_buffer			stV4lBuf;
    struct v4l2_plane           stV4lPlanes[3];
	int							nV4lBufCnt;
    struct v4l2_format          format;
    struct v4l2_requestbuffers  req_bufs;
    struct v4l2_control         ctl;
    struct v4l2_event_subscription  sub;
    struct v4l2_event           evt;

    int                         r;
    struct pollfd               p_fds;

	signal(SIGINT, SigIntHanlder);
	signal(SIGSTOP, SigStopHanlder);
	signal(SIGCONT, SigContHanlder);

	memset(&component[0],
		   0,
		   MAX_SUPPORTED_COMPONENTS * sizeof(component_t)
		   );

HAS_2ND_CMD:

	nHas2ndCmd = 0;

	component[nCmdIdx].busType = -1;
    pComponent = &component[nCmdIdx];
    pComponent->ports[STREAM_DIR_OUT].fmt = 0xFFFFFFFF;

    while (nArgNow < argc)
	{
		if (!strcasecmp(argv[nArgNow],"IFILE"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_IN].eMediaType = MEDIA_FILE_IN;
			component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr = argv[nArgNow++];
		}
		else if (!strcasecmp(argv[nArgNow],"OFILE"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_OUT].eMediaType = MEDIA_FILE_OUT;
			component[nCmdIdx].ports[STREAM_DIR_OUT].pszNameOrAddr = argv[nArgNow++];
		}
		else if (!strcasecmp(argv[nArgNow],"NULL"))
		{
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_OUT].eMediaType = MEDIA_NULL_OUT;
		}
		else if (!strcasecmp(argv[nArgNow],"IFMT"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_IN].fmt = atoi(argv[nArgNow++]);
        }
		else if (!strcasecmp(argv[nArgNow],"OFMT"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_OUT].fmt = atoi(argv[nArgNow++]);
        }
		else if (!strcasecmp(argv[nArgNow],"+"))
		{
            nArgNow++;
            nHas2ndCmd = 1;
            break;
        }
		else
		{
            nArgNow++;
        }
    }

	// lookup and open the device
	lErr = lookup_video_device_node(component[nCmdIdx].devInstance,
									&component[nCmdIdx].busType,
                                    &type,
									component[nCmdIdx].szDevName
									);
	if (0 == lErr)
	{
		component[nCmdIdx].hDev = open(component[nCmdIdx].szDevName,
									   O_RDWR
									   );
		if (component[nCmdIdx].hDev <= 0)
		{
			printf("Unable to Open %s.\n", component[nCmdIdx].szDevName);
			lErr = 1;
			goto FUNC_END;
		}
	}
	else
	{
		printf("Unable to find device.\n");
		lErr = 2;
		goto FUNC_END;
	}

	// get the configuration
	lErr = zvconf(&component[nCmdIdx],
				  component[nCmdIdx].pszScriptName,
                  type
				  );
	if (lErr)
	{
		printf("Unable to config device.\n");
		lErr = 3;
		goto FUNC_END;
	}

    // subsribe v4l2 events
    memset(&sub, 0, sizeof(struct v4l2_event_subscription));
#if 1
    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr)
	{
		printf("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_SOURCE_CHANGE) ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 4;
		goto FUNC_END;
	}
#endif
    sub.type = V4L2_EVENT_EOS;
    lErr = ioctl(pComponent->hDev, VIDIOC_SUBSCRIBE_EVENT, &sub);
	if (lErr)
	{
		printf("%s() VIDIOC_SUBSCRIBE_EVENT(V4L2_EVENT_EOS) ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 5;
		goto FUNC_END;
	}

    // set v4l2 output format (compressed data input)
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (pComponent->ports[STREAM_DIR_IN].fmt < ZPU_NUM_FORMATS_COMPRESSED)
    {
        format.fmt.pix_mp.pixelformat = formats_compressed[pComponent->ports[STREAM_DIR_IN].fmt];
    }
    else
    {
        format.fmt.pix_mp.pixelformat = VPU_PIX_FMT_LOGO;
    }
    format.fmt.pix_mp.num_planes = 1;
    format.fmt.pix_mp.plane_fmt[0].sizeimage = pComponent->ports[STREAM_DIR_IN].frame_size;

    lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	if (lErr)
	{
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 6;
		goto FUNC_END;
	}

    pComponent->ports[STREAM_DIR_IN].auto_rewind = ((VPU_PIX_FMT_LOGO == format.fmt.pix_mp.pixelformat) ? ZOE_TRUE : ZOE_FALSE);
    printf("auto_rewind %d, pixelformat %d\n", pComponent->ports[STREAM_DIR_IN].auto_rewind, format.fmt.pix_mp.pixelformat);

    // setup memory for v4l2 output (compressed data input)
    // request number of buffer and memory type
    memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
    req_bufs.count = pComponent->ports[STREAM_DIR_IN].buf_count;
    req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req_bufs.memory = V4L2_MEMORY_MMAP;

    lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr)
	{
		printf("%s() VIDIOC_REQBUFS ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 7;
		goto FUNC_END;
	}

    // save memory type and actual buffer number
    pComponent->ports[STREAM_DIR_IN].memory = req_bufs.memory;
    pComponent->ports[STREAM_DIR_IN].buf_count = req_bufs.count;

    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf = malloc(pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf)
	{
		printf("%s() Unable to allocate memory for V4L app structure \n", __FUNCTION__);
		lErr = 8;
		goto FUNC_END;
	}

	memset(pComponent->ports[STREAM_DIR_IN].stAppV4lBuf, 0, pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));

    if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory)
    {
        // acquire buffer memory from the driver
		for (nV4lBufCnt = 0; nV4lBufCnt < pComponent->ports[STREAM_DIR_IN].buf_count; nV4lBufCnt++)
		{
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.index = nV4lBufCnt;
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.bytesused = 0;
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.memory = pComponent->ports[STREAM_DIR_IN].memory;
            pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes = pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lPlanes;
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 1;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.index = nV4lBufCnt;
            stV4lBuf.m.planes = stV4lPlanes;
	        stV4lBuf.length = 1;
			lErr = ioctl(pComponent->hDev, VIDIOC_QUERYBUF, &stV4lBuf);
			if (!lErr)
			{
				printf("%s() QUERYBUF(%d) buf_nb(%d)", __FUNCTION__, nV4lBufCnt, stV4lBuf.length);
                for (i = 0; i < stV4lBuf.length; i++)
                {
				    printf("(%x:%d) ", stV4lBuf.m.planes[i].m.mem_offset, stV4lBuf.m.planes[i].length);

				    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].size[i] = stV4lBuf.m.planes[i].length;
				    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0, stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
				    if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i])
				    {
					    printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
					    lErr = 9;
					    goto FUNC_END;
				    }

		            pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].m.mem_offset = stV4lBuf.m.planes[i].m.mem_offset;
                    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused = 0;
                    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].length = stV4lBuf.m.planes[i].length;
                    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset = 0;
                }
				printf("\n");
			}
			else
			{
				printf("%s() VIDIOC_QUERYBUF failed index=%d \n", __FUNCTION__, nV4lBufCnt);
				lErr = 10;
				goto FUNC_END;
			}
		}
    }

    // this port is opened if the buffers are allocated
    pComponent->ports[STREAM_DIR_IN].opened = 1;

	gettimeofday(&start,NULL);
    // start the v4l2 output streaming thread
    printf("%s() want to create input thread\n", __FUNCTION__);
	
	lErr = pthread_create(&pComponent->ports[STREAM_DIR_IN].threadId,
			NULL,
			(void *)test_streamin,
			pComponent
			);
	if (!lErr)
	{
		pComponent->ports[STREAM_DIR_IN].ulThreadCreated = 1;
	}

	// wait for 100 msec
	usleep(100000);

    // stream on v4l2 output
    nType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &nType);
	if (!lErr)
	{
        pComponent->ports[STREAM_DIR_IN].unStarted = 1;
	}
    else
    {
		printf("%s() VIDIOC_STREAMON V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE failed errno(%d) %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 11;
		goto FUNC_END;
    }

    // wait for resoltion change
    p_fds.fd = pComponent->hDev;
    p_fds.events = POLLPRI;

    while (!g_unCtrlCReceived)
    {
        r = poll(&p_fds, 1, 2000);
		printf("%s() r %d\n", __FUNCTION__, r);
        if (-1 == r) 
        {
            fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
            goto FUNCTION_STOP;
        }
        if (0 == r) 
        {
            fprintf(stderr, "%s() select timeout\n", __FUNCTION__);
            continue;
        }
        if (r > 0)
        {
            break;
        }
    }

    if (g_unCtrlCReceived)
    {
        goto FUNCTION_STOP;
    }

#ifdef DQEVENT
    memset(&evt, 0, sizeof(struct v4l2_event));
    lErr = ioctl(pComponent->hDev, VIDIOC_DQEVENT, &evt);
	if (lErr)
	{
		printf("%s() VIDIOC_DQEVENT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
	}
    else
    {
        switch (evt.type)
        {
            case V4L2_EVENT_SOURCE_CHANGE:
		        printf("%s() VIDIOC_DQEVENT V4L2_EVENT_SOURCE_CHANGE changes(0x%x) pending(%d) seq(%d)\n", __FUNCTION__, evt.u.src_change.changes, evt.pending, evt.sequence);
                break;
            case V4L2_EVENT_EOS:
		        printf("%s() VIDIOC_DQEVENT V4L2_EVENT_EOS pending(%d) seq(%d)\n", __FUNCTION__, evt.pending, evt.sequence);
                break;
            default:
		        printf("%s() VIDIOC_DQEVENT unknown event(%d)\n", __FUNCTION__, evt.type);
                break;
        }
    }
#endif

    // get format on v4l2 capture (YUV output)
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    lErr = ioctl(pComponent->hDev, VIDIOC_G_FMT, &format);
	if (lErr)
	{
		printf("%s() VIDIOC_G_FMT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 12;
		goto FUNC_END;
	}
    else
    {
		printf("%s() VIDIOC_G_FMT w(%d) h(%d)\n", __FUNCTION__, format.fmt.pix_mp.width, format.fmt.pix_mp.height);
    }
    pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nWidth = format.fmt.pix_mp.width;
    pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nHeight = format.fmt.pix_mp.height;
    pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nBitCount = 12;
    pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nDataType = ZV_YUV_DATA_TYPE_NV12;
    pComponent->ports[STREAM_DIR_OUT].openFormat.yuv.nFrameRate = 30;
    pComponent->ports[STREAM_DIR_OUT].frame_size = (format.fmt.pix_mp.height * format.fmt.pix_mp.height * 3) / 2;
    pComponent->ulWidth = format.fmt.pix_mp.width;
    pComponent->ulHeight = format.fmt.pix_mp.height;

    // set v4l2 capture format (YUV output)
    if ((pComponent->ports[STREAM_DIR_OUT].fmt < ZPU_NUM_FORMATS_YUV) &&
        (format.fmt.pix_mp.pixelformat != formats_yuv[pComponent->ports[STREAM_DIR_OUT].fmt])
        )
    {
        format.fmt.pix_mp.pixelformat = formats_yuv[pComponent->ports[STREAM_DIR_OUT].fmt];

        lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	    if (lErr)
	    {
	        printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
	        lErr = 13;
	        goto FUNC_END;
	    }
    }
    else
    {
        for (i = 0; i < ZPU_NUM_FORMATS_YUV; i++)
        {
            if (format.fmt.pix_mp.pixelformat == formats_yuv[i])
            {
                pComponent->ports[STREAM_DIR_OUT].fmt = i;
                break;
            }
        }
    }

    // get cropping information
    pComponent->crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_G_CROP, &pComponent->crop);
	if (lErr)
	{
		printf("%s() VIDIOC_G_CROP ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 14;
		goto FUNC_END;
	}
    else
    {
		printf("%s() VIDIOC_G_CROP left(%d) top(%d) w(%d) h(%d)\n", __FUNCTION__, pComponent->crop.c.left, pComponent->crop.c.top, pComponent->crop.c.width, pComponent->crop.c.height);
    }

    // get minimum number of buffers required for v4l2 capture (YUV output)
    memset(&ctl, 0, sizeof(struct v4l2_control));
    ctl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
    lErr = ioctl(pComponent->hDev, VIDIOC_G_CTRL, &ctl);
	if (lErr)
	{
		printf("%s() VIDIOC_G_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 15;
		goto FUNC_END;
	}

	printf("%s() VIDIOC_G_CTRL ioctl val=%d\n", __FUNCTION__, ctl.value);
	pComponent->ports[STREAM_DIR_OUT].buf_count = ctl.value + 3;

    // setup memory for v4l2 capture (YUV output)
    // request number of buffer and memory type
    memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
    req_bufs.count = pComponent->ports[STREAM_DIR_OUT].buf_count;
    req_bufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_bufs.memory = V4L2_MEMORY_MMAP;

    lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr)
	{
		printf("%s() VIDIOC_REQBUFS V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 16;
		goto FUNC_END;
	}
    else
    {
		printf("%s() VIDIOC_REQBUFS V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE %d\n", __FUNCTION__, req_bufs.count);
    }

    // save memory type and actual buffer number
    pComponent->ports[STREAM_DIR_OUT].memory = req_bufs.memory;
    pComponent->ports[STREAM_DIR_OUT].buf_count = req_bufs.count;

    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf = malloc(pComponent->ports[STREAM_DIR_OUT].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf)
	{
		printf("%s() Unable to allocate memory for V4L app structure \n", __FUNCTION__);
		lErr = 17;
		goto FUNC_END;
	}

	memset(pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf, 0, pComponent->ports[STREAM_DIR_OUT].buf_count * sizeof(struct zvapp_v4l_buf_info));

    if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_OUT].memory)
    {
        // acquire buffer memory from the driver
		for (nV4lBufCnt = 0; nV4lBufCnt < pComponent->ports[STREAM_DIR_OUT].buf_count; nV4lBufCnt++)
		{
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.index = nV4lBufCnt;
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.bytesused = 0;
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.memory = pComponent->ports[STREAM_DIR_OUT].memory;
            pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes = pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lPlanes;
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 2;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			stV4lBuf.memory = V4L2_MEMORY_MMAP;
			stV4lBuf.index = nV4lBufCnt;
            stV4lBuf.m.planes = stV4lPlanes;
		    stV4lBuf.length = 2;
			lErr = ioctl(pComponent->hDev, VIDIOC_QUERYBUF, &stV4lBuf);
			if (!lErr)
			{
				printf("%s() QUERYBUF(%d) buf_nb(%d)", __FUNCTION__, nV4lBufCnt, stV4lBuf.length);
                for (i = 0; i < stV4lBuf.length; i++)
                {
				    printf("(%x:%d) ", stV4lBuf.m.planes[i].m.mem_offset, stV4lBuf.m.planes[i].length);

				    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].size[i] = stV4lBuf.m.planes[i].length;
				    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0, stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
				    if (pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] <= 0)
				    {
					    printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
					    lErr = 18;
					    goto FUNC_END;
				    }
		            pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].m.mem_offset = stV4lBuf.m.planes[i].m.mem_offset;
                    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].bytesused = 0;
                    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].length = stV4lBuf.m.planes[i].length;
                    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.m.planes[i].data_offset = 0;
                }
				printf("\n");
			}
			else
			{
				printf("%s() VIDIOC_QUERYBUF failed index=%d \n", __FUNCTION__, nV4lBufCnt);
				lErr = 19;
				goto FUNC_END;
			}
		}
    }

    // this port is opened if the buffers are allocated
    pComponent->ports[STREAM_DIR_OUT].opened = 1;

    // start the v4l2 capture streaming thread
	lErr = pthread_create(&pComponent->ports[STREAM_DIR_OUT].threadId,
			NULL,
			(void *)test_streamout,
			pComponent
			);
	if (!lErr)
	{
		pComponent->ports[STREAM_DIR_OUT].ulThreadCreated = 1;
	}

	// wait for 100 msec
	usleep(100000);

    // stream on v4l2 capture
    nType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &nType);
	if (!lErr)
	{
        pComponent->ports[STREAM_DIR_OUT].unStarted = 1;
	}
    else
    {
		printf("%s() VIDIOC_STREAMON V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE failed errno(%d) %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 20;
		goto FUNC_END;
    }

	if (nHas2ndCmd)
	{
		nCmdIdx++;
		goto HAS_2ND_CMD;
	}

    

	// wait for user input
	changemode(1);

CHECK_USER_INPUT:
	while ((g_unCtrlCReceived == 0) && !kbhit())
	{
		usleep(30000);
	}

	if (!g_unCtrlCReceived) {
		usleep(30000);
		goto CHECK_USER_INPUT;
	}
	time_total += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
	printf("time_total=%f,frame_count=%d,fps=%f\n",time_total / 1000000.0,frame_count,frame_count*1000000.0/time_total);

	changemode(0);

FUNCTION_STOP:

	// stop streaming
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--)
	{
		if (component[i].hDev > 0)
		{
			if (component[i].ports[STREAM_DIR_OUT].opened)
			{
			    printf("stop(%d) hDev(%d) V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE\n", i, component[i].hDev);
                nType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
			    ioctl(component[i].hDev, VIDIOC_STREAMOFF, &nType);
			    component[i].ports[STREAM_DIR_OUT].unSentStopCmd = 1;
				component[i].ports[STREAM_DIR_OUT].unCtrlCReceived = 1;
			}

			if (component[i].ports[STREAM_DIR_IN].opened)
			{
			    printf("stop(%d) hDev(%d) V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE\n", i, component[i].hDev);
                nType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
			    ioctl(component[i].hDev, VIDIOC_STREAMOFF, &nType);
			    component[i].ports[STREAM_DIR_IN].unSentStopCmd = 1;
				component[i].ports[STREAM_DIR_IN].unCtrlCReceived = 1;
			}
		}
	}

    // unsubscribe v4l2 events
    memset(&sub, 0, sizeof(struct v4l2_event_subscription));
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--)
	{
		if (component[i].hDev > 0)
		{
            sub.type = V4L2_EVENT_ALL;
            lErr = ioctl(component[i].hDev, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
	        if (lErr)
	        {
		        printf("%s() VIDIOC_UNSUBSCRIBE_EVENT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
	        }
        }
    }


FUNC_END:

	frame_count = 0;
	// propagate the signal to all the threads
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--)
	{
		for (j = 0; j < MAX_STREAM_DIR; j++)
		{
			if (component[i].ports[j].ulThreadCreated)
			{
				printf("%s() set component(%d) port(%d) unCtrlCReceived\n", __FUNCTION__, i, j);
                component[i].ports[j].unStarted = 0;
				component[i].ports[j].unCtrlCReceived = 1;
			}
		}
	}

	// wait for 100 msec
	usleep(100000);

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++)
	{
		for (j = 0; j < MAX_STREAM_DIR; j++)
		{
			if (component[i].ports[j].ulThreadCreated)
			{
				pthread_join(component[i].ports[j].threadId,
							 NULL
							 );
				component[i].ports[j].ulThreadCreated = 0;
			}
		}
	}

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++)
	{
		for (j = 0; j < MAX_STREAM_DIR; j++)
		{
			if (V4L2_MEMORY_MMAP == component[i].ports[j].memory)
            {
	            if (component[i].ports[j].stAppV4lBuf)
	            {
		            for (nV4lBufCnt = 0; nV4lBufCnt < component[i].ports[j].buf_count; nV4lBufCnt++)
		            {
                        for (k = 0; k < 2; k++)
                        {
			                if (component[i].ports[j].stAppV4lBuf[nV4lBufCnt].addr[i])
			                {
				                munmap(component[i].ports[j].stAppV4lBuf[nV4lBufCnt].addr[k], component[i].ports[j].stAppV4lBuf[nV4lBufCnt].size[k]);
			                }
                        }
		            }
		            free((void *)component[i].ports[j].stAppV4lBuf);
	            }
            }
		}
	}

	for (i = 0; i < MAX_SUPPORTED_COMPONENTS; i++)
	{
		for (j = 0; j < MAX_STREAM_DIR; j++)
		{
			// close ports
			if (component[i].ports[j].opened)
			{
				component[i].ports[j].opened = ZOE_FALSE;
			}
		}

		// close device
		if (component[i].hDev > 0)
		{
			close(component[i].hDev);
			component[i].hDev = 0;
		}

	}

	return (lErr);
}
