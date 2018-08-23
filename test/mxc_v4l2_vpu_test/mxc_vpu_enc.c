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

#define _TEST_MMAP

#define MODULE_NAME			"mxc_vpu_enc.out"
#define MODULE_VERSION		"0.1"

#ifndef max
#define max(a,b)        (((a) < (b)) ? (b) : (a))
#endif //!max

#define DQEVENT
#define MAX_SUPPORTED_COMPONENTS	5

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

/**the function is used for to convert yuv420p to yuv420sp
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
	uv_size = height * width/2;
	uv_temp = (unsigned char *)malloc(sizeof(unsigned char)*uv_size);

	u_start = buf;
	v_start = u_start + uv_size/2;
	for (i = 0, j = 0; j < uv_size; j += 2, i++) {
		uv_temp[j] = u_start[i];
		uv_temp[j+1] = v_start[i];
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
					printf("%s()-> Got bus(%d) type(%d) inst(%d)\n", __FUNCTION__, busType, devType, devInstance);
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
	        pComponent->ports[STREAM_DIR_OUT].buf_count = 4;
            break;
        case COMPONENT_TYPE_CODEC:
        case COMPONENT_TYPE_DECODER:
        default:
	        pComponent->ports[STREAM_DIR_IN].portType = COMPONENT_PORT_COMP_IN;
	        pComponent->ports[STREAM_DIR_IN].frame_size = 256 * 1024;
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
void test_streamout(component_t *pComponent)
{
	int							lErr = 0;
	FILE						*fpOutput = 0;

	struct zvapp_v4l_buf_info	*stAppV4lBuf = pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf;
	struct v4l2_buffer			stV4lBuf;
	struct v4l2_decoder_cmd     v4l2cmd;
    struct v4l2_plane           stV4lPlanes[3];
	int							nV4lBufCnt;

	unsigned int				ulXferBufCnt = 0;

	int							frame_nb;

    fd_set                      fds;
    struct timeval              tv;
    int                         r;
    struct v4l2_event           evt;

    int                         i;
    int                         OutputType;

	printf("%s() [\n", __FUNCTION__);

	frame_nb = pComponent->ports[STREAM_DIR_OUT].buf_count;

    /***********************************************
    ** 1> Open output file descriptor
    ***********************************************/
    if(loopFlag == 0)
    {
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
    }

OUTPUT_StreamOn:
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
           	tv.tv_sec = 2;
           	tv.tv_usec = 0;

			r = select(pComponent->hDev + 1, &fds, NULL, NULL, &tv);

			if (-1 == r)
			{
				fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
				continue;
			}
			if (0 == r)
			{
				FD_ZERO(&fds);
				FD_SET(pComponent->hDev, &fds);

				// Timeout
				tv.tv_sec = 2;
				tv.tv_usec = 0;

				r = select(pComponent->hDev + 1, NULL, NULL, &fds, &tv);
				if (-1 == r)
				{
					fprintf(stderr, "%s() select errno(%d)\n", __FUNCTION__, errno);
					continue;
				}
				if (0 == r)
				{
					fprintf(stderr, "%s() select timeout\n", __FUNCTION__);
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
                        if(loopFlag == 0)
                        {
                            g_unCtrlCReceived = 1;
                            printf("EOS received\n");
                            gettimeofday(&end,NULL);
                            goto FUNC_END;
                        }
                        else
                        {
                            printf("EOS received\n");
                            gettimeofday(&end,NULL);
                            goto OUTPUT_LOOP;
                        }
					}
					else
						printf("%s() VIDIOC_DQEVENT type=%d\n",__FUNCTION__, evt.type);
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
				char *pBuf;

				stAppV4lBuf[stV4lBuf.index].sent = 0;

                if(loopFlag == 0)
                {
				    printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r", pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused, stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags, stV4lBuf.timestamp.tv_sec);
                }
                else
                {
                    if(setFrameNum != 0)
                    {
                        if(frame_count < setFrameNum)
                        {
                            printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r", pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused, stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags, stV4lBuf.timestamp.tv_sec);
                        }
                    }
                    else
                        printf("\t\t\t\t\t\t\t encXferBufCnt[%d]: %8u %8u %8u %8u 0x%08x t=%ld\r", pComponent->hDev, ulXferBufCnt++, stV4lBuf.m.planes[0].bytesused, stV4lBuf.m.planes[0].length, stV4lBuf.m.planes[0].data_offset, stV4lBuf.flags, stV4lBuf.timestamp.tv_sec);
                }
				frame_count++;
				if(frame_count==setFrameNum)
				{
                    printf("setFrameNum : %d , frame_count : %d\n",setFrameNum,frame_count);
                    if(loopFlag == 0)
					{
                        g_unCtrlCReceived = 1;
					    v4l2cmd.cmd = V4L2_ENC_CMD_STOP;
					    lErr = ioctl(pComponent->hDev, VIDIOC_ENCODER_CMD, &v4l2cmd);
					    if (lErr)
					    	printf("VIDIOC_ENCODER_CMD has error\n");
					    goto FUNC_END;
                    }
                    else
                    {
					    v4l2cmd.cmd = V4L2_ENC_CMD_STOP;
					    lErr = ioctl(pComponent->hDev, VIDIOC_ENCODER_CMD, &v4l2cmd);
					    if (lErr)
					    	printf("VIDIOC_ENCODER_CMD has error\n");
                    }
                }

				if (pComponent->ports[STREAM_DIR_OUT].eMediaType == MEDIA_FILE_OUT)
				{
					if(loopFlag == 0)
					{
						unsigned int bytesused;
						bytesused = stV4lBuf.m.planes[0].bytesused;
						for (i = 0; i < stV4lBuf.length; i++) {

							pBuf = stAppV4lBuf[stV4lBuf.index].addr[i] + stV4lBuf.m.planes[i].data_offset;
							fwrite((void*)pBuf, 1, bytesused, fpOutput);

						}
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

			usleep(1000);
   		}
	}

OUTPUT_LOOP:
    if(loopFlag == 1)
    {
        usleep(10000);

        loopCount++;
        printf("output thread is %d loop times.\n",loopCount);
        if (pComponent->ports[STREAM_DIR_OUT].opened)
        {
             OutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
             lErr = ioctl(pComponent->hDev, VIDIOC_STREAMOFF, &OutputType);
             if(lErr != 0)
             {
                printf("output thread streamoff error!!\n");
             }
        }
        loopSync = 1;
        pthread_mutex_lock(&lockSync);
        pthread_cond_wait(&condSync,&lockSync);
        pthread_mutex_unlock(&lockSync);
        if (pComponent->ports[STREAM_DIR_OUT].opened)
        {
             OutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
             lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &OutputType);
             if(lErr != 0)
             {
                printf("output thread streamon error!!\n");
             }
        }
        frame_count = 0;
        ulXferBufCnt = 0;
        for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
        {
	        stAppV4lBuf[nV4lBufCnt].sent = 0;
        }
        goto
            OUTPUT_StreamOn;
    }




FUNC_END:
	printf("\n");
	usleep(10000);

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
    int                         InputType;

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

INPUT_StreamOn:
	/***********************************************
	** 2> Stream on
	***********************************************/
	while (!pComponent->ports[STREAM_DIR_IN].unCtrlCReceived)
	{
        if(loopSync == 1)
        {
            goto INPUT_LOOP;
        }
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
                    fprintf(stderr, "%s() select timeout\n", __FUNCTION__);
                    continue;
                }

				stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
				stV4lBuf.memory = V4L2_MEMORY_MMAP;
                stV4lBuf.m.planes = stV4lPlanes;
	            stV4lBuf.length = 2;
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
						if (i==1)
							convert_feed_stream(pComponent, (unsigned char *)pBuf);
					    pstV4lBuf->m.planes[i].data_offset = 0;
						file_size -= pstV4lBuf->m.planes[i].bytesused;
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
						printf("%s() QBUF ioctl failed %d %s ", __FUNCTION__, errno, strerror(errno));
						if (errno == EAGAIN)
						{
						    printf("\n");
							lErr = 0;
						}
                        else
                        {
						    printf("v4l_buf index(%d) type(%d) memory(%d) sequence(%d) length(%d) planes(%p)\n",
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

					if (file_size == 0)
					{
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

INPUT_LOOP:
    if(loopSync == 1)
    {
        printf("input thread is %d loops\n",loopCount);
        if (pComponent->ports[STREAM_DIR_IN].opened)
        {
             InputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
             lErr = ioctl(pComponent->hDev, VIDIOC_STREAMOFF, &InputType);
             if(lErr == -1)
             {
                 printf("input streamoff error!!\n");
             }
        }
		fseek(fpInput, 0, SEEK_END);
		file_size = ftell(fpInput);
        if(0 != fseek(fpInput, 0, SEEK_SET))
            printf("fseek error\n");

        if (pComponent->ports[STREAM_DIR_IN].opened)
        {
            InputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
            lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &InputType);
            if(lErr == -1)
            {
                printf("input streamon error!!\n");
            }
        }
        first = pComponent->ports[STREAM_DIR_IN].buf_count;
        for (nV4lBufCnt = 0; nV4lBufCnt < frame_nb; nV4lBufCnt++)
        {
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

	if (fpInput)
	{
		fclose(fpInput);
	}

    pComponent->ports[STREAM_DIR_IN].unStarted = 0;

    //return lErr;
	printf("%s() ]\n", __FUNCTION__);
}

static void set_encoder_parameters(pMEDIAIP_ENC_PARAM pin_enc_param ,component_t *pComponent)
{
	int                         lErr = 0;
	struct v4l2_control         ctl;

	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
	ctl.value = profile;
	lErr = ioctl(pComponent->hDev, VIDIOC_S_CTRL, &ctl);
 	if (lErr)
 	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
 	}
	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MPEG_VIDEO_BITRATE;
	ctl.value = TarBitrate;
	lErr = ioctl(pComponent->hDev, VIDIOC_S_CTRL, &ctl);
 	if (lErr)
 	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
 	}
	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MPEG_VIDEO_BITRATE_MODE;
	ctl.value = V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
	lErr = ioctl(pComponent->hDev, VIDIOC_S_CTRL, &ctl);
 	if (lErr)
 	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
 	}
	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE;
	ctl.value = pin_enc_param->uGopBLength;
	lErr = ioctl(pComponent->hDev, VIDIOC_S_CTRL, &ctl);
 	if (lErr)
 	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
 	}
	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
	ctl.value = pin_enc_param->uInitSliceQP;
	lErr = ioctl(pComponent->hDev, VIDIOC_S_CTRL, &ctl);
	if (lErr)
 	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
 	}
	memset(&ctl, 0, sizeof(struct v4l2_control));
	ctl.id = V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;
	lErr = ioctl(pComponent->hDev, VIDIOC_G_CTRL, &ctl);
 	if (lErr)
	{
		printf("%s() VIDIOC_S_CTRL ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
	}
}


#define HAS_ARG_NUM(argc, argnow, need) ((nArgNow + need) < argc)

/***********help ********/
void showAllArgs(void)
{
    printf("Type 'HELP' to see the list. Type 'HELP NAME' to find out more about parameter 'NAME'.\n");
    printf("HELP     : fand out  more about parameter\n");
    printf("IFILE    : input filename\n");
    printf("OFILE    : output filename\n");
    printf("WIDTH    : set input file width\n");
    printf("OWIDTH   : set output file width\n");
    printf("HEIGHT   : set input file height\n");
    printf("OWIDTH   : set output file height\n");
    printf("GOP      : set group of picture\n");
    printf("QP       : set quantizer parameter\n");
    printf("MAXBR    : set encoder maximum boudrate\n");
    printf("MINBR    : set encoder minimum boudrate\n");
    printf("FRAMENUM : set output frame number\n");
    printf("LOOP     : set application in loops\n");
    return ;
}

void showArgs(int allArgs,int count,char* p[])
{
    while(count < allArgs)
    {
        if(!strcasecmp(p[count],"HELP"))
        {
            count++;
            printf("HELP: parameter 'HELP' is help to use the app\n");
        }
        else if(!strcasecmp(p[count],"IFILE"))
        {
            count++;
            printf("IFILE: parameter 'IFILE' is input file\n");
        }
        else if(!strcasecmp(p[count],"OFILE"))
        {
            count++;
            printf("OFILE: parameter 'OFILE' is output file\n");
        }
        else if(!strcasecmp(p[count],"WIDTH"))
        {
            count++;
            printf("WIDTH: parameter 'WIDTH' is input file width\n");
        }
        else if(!strcasecmp(p[count],"OWIDTH"))
        {
            count++;
            printf("OWIDTH: parameter 'OWIDTH' is output file width\n");
        }
        else if(!strcasecmp(p[count],"HEIGHT"))
        {
            count++;
            printf("HEIGHT: parameter 'HEIGHT' is input file height\n");
        }
        else if(!strcasecmp(p[count],"GOP"))
        {
            count++;
            printf("GOP: parameter 'GOP' is Group of picture\n");
        }
        else if(!strcasecmp(p[count],"QP"))
        {
            count++;
            printf("QP: parameter 'QP' is quantizer parameter,between 0 and 31. The smaller the value,the finer the quantization,the higher the image quality,the longer the code stream \n");
        }
        else if(!strcasecmp(p[count],"MAXBR"))
        {
            count++;
            printf("MAXBR: parameter 'MAXBR' is maximum boudrate \n");
        }
        else if(!strcasecmp(p[count],"MINBR"))
        {
            count++;
            printf("MINBR: parameter 'MINBR' is minimum boudrate \n");
        }
        else if(!strcasecmp(p[count],"FRAMENUM"))
        {
            count++;
            printf("FRAMENUM: parameter 'FRAMENUM' set output frame number \n");
        }
        else if(!strcasecmp(p[count],"LOOP"))
        {
            count++;
            printf("LOOP: parameter 'LOOP' set application in loops and no output file  \n");
        }
        else
        {
            printf("no help topics match %s,please try help help\n",p[count]);
            count++;
        }
    }
    return ;
}
/*********help end********/


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
//	int			                ch;
    uint32_t                    type = COMPONENT_TYPE_ENCODER;
	MEDIAIP_ENC_PARAM           enc_param;

	struct v4l2_buffer			stV4lBuf;
    struct v4l2_plane           stV4lPlanes[3];
	int							nV4lBufCnt;
    struct v4l2_format          format;
    struct v4l2_requestbuffers  req_bufs;
    struct v4l2_event_subscription  sub;
	float fps;

	//signal(SIGINT, SigIntHanlder);
	signal(SIGSTOP, SigStopHanlder);
	signal(SIGCONT, SigContHanlder);

	memset(&component[0],
		   0,
		   MAX_SUPPORTED_COMPONENTS * sizeof(component_t)
		   );
	memset(&enc_param,
			0,
			sizeof(MEDIAIP_ENC_PARAM)
			);

HAS_2ND_CMD:

	nHas2ndCmd = 0;

	component[nCmdIdx].busType = -1;
    pComponent = &component[nCmdIdx];
    pComponent->ports[STREAM_DIR_OUT].fmt = 0xFFFFFFFF;

    if(argc == 1)
    {
        printf("ERROR: Lack of necessary parameters\n");
        return 0;
    }
    while (nArgNow < argc)
	{
        if (!strcasecmp(argv[nArgNow],"help"))
        {
            if (!HAS_ARG_NUM(argc,nArgNow,1))
            {
                showAllArgs();
                return 0;
            }
            nArgNow++;
            showArgs(argc,nArgNow,argv);
            return 0;
        }
		else if (!strcasecmp(argv[nArgNow],"IFILE"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				printf("ERROR: no input file\n");
                return 0;
            }
			nArgNow++;
			component[nCmdIdx].ports[STREAM_DIR_IN].eMediaType = MEDIA_FILE_IN;
			component[nCmdIdx].ports[STREAM_DIR_IN].pszNameOrAddr = argv[nArgNow++];
		}
		else if (!strcasecmp(argv[nArgNow],"OFILE"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				printf("ERROR: no output file\n");
                return 0;
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
		else if(!strcasecmp(argv[nArgNow],"WIDTH"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uSrcWidth = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"HEIGHT"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uSrcHeight = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"OWIDTH"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uOutWidth = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"OHEIGHT"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uOutHeight = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"GOP"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uGopBLength = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"QP"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uInitSliceQP = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"MAXBR"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uMaxBitRate = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"MINBR"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			enc_param.uMinBitRate = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"TARBR"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
            {
				break;
            }
			nArgNow++;
			TarBitrate = atoi(argv[nArgNow++]);
		}
		else if(!strcasecmp(argv[nArgNow],"PROFILE"))
		{
			if (!HAS_ARG_NUM(argc, nArgNow, 1))
			{
				break;
			}
			nArgNow++;
			profile = atoi(argv[nArgNow++]);
		}
        else if(!strcasecmp(argv[nArgNow],"FRAMENUM"))
        {
            if(!HAS_ARG_NUM(argc, nArgNow, 1))
            {
                printf("set frame number error!\n");
                return 0;
            }
            nArgNow++;
            setFrameNum = atoi(argv[nArgNow++]);
            printf("setFrameNum is %d\n",setFrameNum);
        }
        else if(!strcasecmp(argv[nArgNow],"LOOP"))
        {
            nArgNow++;
            loopFlag = 1;
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

	//set v4l2 capture format (compressed data input)
	memset(&format, 0, sizeof(struct v4l2_format));
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;

	format.fmt.pix_mp.num_planes = 1;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = pComponent->ports[STREAM_DIR_OUT].frame_size;

	lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	if (lErr)
	{
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 6;
		goto FUNC_END;
	}
	// setup memory for v4l2 capture (encode stream output)
    // request number of buffer and memory type
    memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
    req_bufs.count = pComponent->ports[STREAM_DIR_OUT].buf_count;
    req_bufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req_bufs.memory = V4L2_MEMORY_MMAP;

    lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr)
	{
		printf("%s() VIDIOC_REQBUFS V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 7;
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
		lErr = 8;
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
		    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 1;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
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

				    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].size[i] = stV4lBuf.m.planes[i].length;
				    pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0, stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
				    if (pComponent->ports[STREAM_DIR_OUT].stAppV4lBuf[nV4lBufCnt].addr[i] <= 0)
				    {
					    printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
					    lErr = 9;
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
	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_OUT].memory)
	{
		lErr = pthread_create(&pComponent->ports[STREAM_DIR_OUT].threadId,
							  NULL,
							  (void *)test_streamout,
							  pComponent
							  );
	}
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
		lErr = 11;
		goto FUNC_END;
    }

    // set v4l2 output format (yuv data input)
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    format.fmt.pix_mp.num_planes = 2;
	format.fmt.pix_mp.width = enc_param.uSrcWidth;
	format.fmt.pix_mp.height = enc_param.uSrcHeight;
	pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nWidth = enc_param.uSrcWidth;
	pComponent->ports[STREAM_DIR_IN].openFormat.yuv.nHeight = enc_param.uSrcHeight;
	for (i = 0; i < format.fmt.pix_mp.num_planes; i++)
	{
		format.fmt.pix_mp.plane_fmt[i].bytesperline = enc_param.uSrcWidth;
	}
    format.fmt.pix_mp.plane_fmt[0].sizeimage = pComponent->ports[STREAM_DIR_IN].frame_size;

    printf("VIDIOC_S_FMT format %d\n", format.fmt.pix_mp.pixelformat);

    lErr = ioctl(pComponent->hDev, VIDIOC_S_FMT, &format);
	if (lErr)
	{
		printf("%s() VIDIOC_S_FMT ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 12;
		goto FUNC_END;
	}

	set_encoder_parameters(&enc_param, pComponent);
    pComponent->ports[STREAM_DIR_IN].auto_rewind = ((VPU_PIX_FMT_LOGO == format.fmt.pix_mp.pixelformat) ? ZOE_TRUE : ZOE_FALSE);
    printf("auto_rewind %d, pixelformat %d\n", pComponent->ports[STREAM_DIR_IN].auto_rewind, format.fmt.pix_mp.pixelformat);

    // setup memory for v4l2 output (compressed data input)
    //
    // request number of buffer and memory type
    memset(&req_bufs, 0, sizeof(struct v4l2_requestbuffers));
    req_bufs.count = pComponent->ports[STREAM_DIR_IN].buf_count;
    req_bufs.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    req_bufs.memory = V4L2_MEMORY_MMAP;
    lErr = ioctl(pComponent->hDev, VIDIOC_REQBUFS, &req_bufs);
	if (lErr)
	{
		printf("%s() VIDIOC_REQBUFS ioctl failed %d %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 13;
		goto FUNC_END;
	}

    // save memory type and actual buffer number
    pComponent->ports[STREAM_DIR_IN].memory = req_bufs.memory;
    pComponent->ports[STREAM_DIR_IN].buf_count = req_bufs.count;

    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf = malloc(pComponent->ports[STREAM_DIR_IN].buf_count * sizeof(struct zvapp_v4l_buf_info));
	if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf)
	{
		printf("%s() Unable to allocate memory for V4L app structure \n", __FUNCTION__);
		lErr = 14;
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
		    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].stV4lBuf.length = 2;

			stV4lBuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
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

				    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].size[i] = stV4lBuf.m.planes[i].length;
				    pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i] = mmap(0, stV4lBuf.m.planes[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, pComponent->hDev, stV4lBuf.m.planes[i].m.mem_offset);
				    if (!pComponent->ports[STREAM_DIR_IN].stAppV4lBuf[nV4lBufCnt].addr[i])
				    {
					    printf("%s() V4L mmap failed index=%d \n", __FUNCTION__, nV4lBufCnt);
					    lErr = 15;
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
				lErr = 16;
				goto FUNC_END;
			}
		}
    }

    // this port is opened if the buffers are allocated
    pComponent->ports[STREAM_DIR_IN].opened = 1;

	gettimeofday(&start,NULL);
    // start the v4l2 output streaming thread
    printf("%s() want to create input thread\n", __FUNCTION__);
	if (V4L2_MEMORY_MMAP == pComponent->ports[STREAM_DIR_IN].memory)
	{
    printf("%s () call test_streamin\n", __FUNCTION__);
		lErr = pthread_create(&pComponent->ports[STREAM_DIR_IN].threadId,
							  NULL,
							  (void *)test_streamin,
							  pComponent
							  );
	}
	if (!lErr)
	{
		pComponent->ports[STREAM_DIR_IN].ulThreadCreated = 1;
	}

	// wait for 100 msec
	usleep(100000);

    // stream on v4l2 output
	printf("start hDev(%d) V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE\n", pComponent->hDev);
    nType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	lErr = ioctl(pComponent->hDev, VIDIOC_STREAMON, &nType);
	if (!lErr)
	{
        pComponent->ports[STREAM_DIR_IN].unStarted = 1;
	}
    else
    {
		printf("%s() VIDIOC_STREAMON V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE failed errno(%d) %s\n", __FUNCTION__, errno, strerror(errno));
		lErr = 17;
		goto FUNC_END;
    }

	if (nHas2ndCmd)
	{
		nCmdIdx++;
		goto HAS_2ND_CMD;
	}



    while(g_unCtrlCReceived == 0)
    {
        usleep(50000);
    }

	time_total += 1000000 * (end.tv_sec-start.tv_sec)+ end.tv_usec-start.tv_usec;
	fps=frame_count*1000000.0/time_total;
	printf("time_total=%f,frame_count=%d,fps=%f\n", time_total / 1000000.0, frame_count, fps);

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
	// propagate the signal to all the threads
	for (i = MAX_SUPPORTED_COMPONENTS - 1; i >= 0; i--)
	{
		for (j = 0; j < MAX_STREAM_DIR; j++)
        {
				component[i].ports[j].unCtrlCReceived = 1;
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

    pthread_cond_destroy(&condSync);

	return (lErr);
}

