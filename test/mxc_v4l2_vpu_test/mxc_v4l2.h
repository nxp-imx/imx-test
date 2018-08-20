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
 * @file mxc_v4l2.h
 *
 */

#ifndef __MXC_V4L2_H__
#define __MXC_V4L2_H__

#include <pthread.h>

#include <linux/version.h>
#include <linux/types.h>

#include <linux/videodev2.h>

typedef unsigned int zoe_bool_t;
#define VPU_PIX_FMT_AVS         v4l2_fourcc('A', 'V', 'S', '0') // AVS video
#define VPU_PIX_FMT_ASP         v4l2_fourcc('A', 'S', 'P', '0') // MPEG4 ASP video
#define VPU_PIX_FMT_RV8         v4l2_fourcc('R', 'V', '8', '0') // RV8 video
#define VPU_PIX_FMT_RV9         v4l2_fourcc('R', 'V', '9', '0') // RV9 video
#define VPU_PIX_FMT_VP6         v4l2_fourcc('V', 'P', '6', '0') // VP6 video
#define VPU_PIX_FMT_SPK         v4l2_fourcc('S', 'P', 'K', '0') // VP6 video
#define VPU_PIX_FMT_HEVC        v4l2_fourcc('H', 'E', 'V', 'C') // H.265 HEVC video
#define VPU_PIX_FMT_VP9         v4l2_fourcc('V', 'P', '9', '0') // VP9 video
#define VPU_PIX_FMT_LOGO        v4l2_fourcc('L', 'O', 'G', 'O') // logo

#define VPU_PIX_FMT_TILED_8     v4l2_fourcc('Z', 'T', '0', '8') // 8 bit tiled
#define VPU_PIX_FMT_TILED_10    v4l2_fourcc('Z', 'T', '1', '0') // 10 bit tiled

#define ZV_YUV_DATA_TYPE_NV12   1
#define ZV_YUV_DATA_TYPE_NV21   2

#define ZOE_TRUE 1
#define ZOE_FALSE 0
#ifndef SIZEOF_ARRAY
#define SIZEOF_ARRAY(ar)    (sizeof(ar) / sizeof((ar)[0]))
#endif
typedef uint32_t    ZOE_OBJECT_HANDLE, *PZOE_OBJECT_HANDLE; //handle of an object

typedef enum _COMPONENT_TYPE
{
	COMPONENT_TYPE_CODEC = 0,
	COMPONENT_TYPE_DECODER,
	COMPONENT_TYPE_ENCODER
} COMPONENT_TYPE, *PCOMPONENT_TYPE;

typedef struct _ZVDEV_INFO
{
	uint32_t            busType;
	uint32_t            devInstance;
    uint32_t            type; // COMPONENT_TYPE
	ZOE_OBJECT_HANDLE   hTask;
} ZVDEV_INFO, *PZVDEV_INFO;

typedef enum _media_type_e
{
    MEDIA_FILE_IN = 0,
    MEDIA_FILE_OUT,
    MEDIA_UDP_IN,
    MEDIA_UDP_OUT,
    MEDIA_TCP_IN,
    MEDIA_TCP_OUT,
    MEDIA_SDL_OUT,
    MEDIA_NULL_OUT
} media_type_e;

struct zvapp_v4l_buf_info
{
	char				*addr[2];
	unsigned int		size[2];

	// for userptr
	struct v4l2_buffer	stV4lBuf;
    struct v4l2_plane   stV4lPlanes[3];
	int					sent;
};

typedef enum _COMPONENT_PORT_TYPE
{
	COMPONENT_PORT_COMP_OUT = 0,
	COMPONENT_PORT_META_OUT,
	COMPONENT_PORT_YUV_OUT,
	COMPONENT_PORT_VIRTUAL_OUT,
	COMPONENT_PORT_YUV_IN,
	COMPONENT_PORT_COMP_IN,
	COMPONENT_PORT_VIRTUAL_IN,
	COMPONENT_PORT_END
} COMPONENT_PORT_TYPE, *PCOMPONENT_PORT_TYPE;

// open formats
typedef struct _ZV_YUV_DATAFORMAT
{
	int32_t         nWidth;
	int32_t         nHeight;
	int32_t         nBitCount;
	uint32_t        nFrameRate;

	uint32_t        nDataType;

} ZV_YUV_DATAFORMAT, *PZV_YUV_DATAFORMAT;

typedef union _COMPONENT_PORT_OPEN_FORMAT
{
	ZV_YUV_DATAFORMAT   yuv;
} COMPONENT_PORT_OPEN_FORMAT, *PCOMPONENT_PORT_OPEN_FORMAT;

typedef struct _stream_media_t
{
	zoe_bool_t			        opened;

	COMPONENT_PORT_TYPE	        portType;
	COMPONENT_PORT_OPEN_FORMAT	openFormat;
	int					        memory;	// v4l2_memory
	unsigned int                buf_count;
	unsigned int                frame_size;
	struct zvapp_v4l_buf_info	*stAppV4lBuf;

	pthread_t			        threadId;
	unsigned int		        ulThreadCreated;

	media_type_e		        eMediaType;
	const char		            *pszNameOrAddr;
	unsigned int		        unPort;
	int					        displayType;

	unsigned int		        unUserPTS;
	unsigned int		        unEOS;

	volatile unsigned int	    unCtrlCReceived;
	volatile unsigned int		unStarted;
	unsigned int		        unSentStopCmd;

	uint32_t			        fmt;
	zoe_bool_t                  auto_rewind;

	unsigned int                outFrameCount;
	unsigned int                inFrameCount;
	unsigned int                done_flag;
} stream_media_t;

typedef enum _stream_dir_e
{
	STREAM_DIR_IN = 0,
	STREAM_DIR_OUT,
	STREAM_DIR_END

} stream_dir_e;

#define MAX_STREAM_DIR	STREAM_DIR_END

typedef struct _component_t 
{
	uint32_t			    devInstance;
	uint32_t			    busType;

	stream_media_t			ports[MAX_STREAM_DIR];

	char					szDevName[32];
	int						hDev;

	char			        *pszScriptName;

	unsigned int			ulWidth;
	unsigned int			ulHeight;

	struct v4l2_crop        crop;

} component_t;

#endif //__MXC_V4L2_H__
