/*
 * Copyright 2020 NXP
 *
 * include/platform_8x.h
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */
#ifndef _INCLUDE_PLATFORM_8X_H
#define _INCLUDE_PLATFORM_8X_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <linux/videodev2.h>


#define V4L2_CID_USER_RAW_BASE                  (V4L2_CID_USER_BASE + 0x1100)
#define V4L2_CID_USER_STREAM_INPUT_MODE         (V4L2_CID_USER_BASE + 0x1109)

#define VPU_PIX_FMT_AVS				v4l2_fourcc('A', 'V', 'S', '0')
#define VPU_PIX_FMT_ASP				v4l2_fourcc('A', 'S', 'P', '0')
#define VPU_PIX_FMT_RV				v4l2_fourcc('R', 'V', '0', '0')
#define VPU_PIX_FMT_VP6				v4l2_fourcc('V', 'P', '6', '0')
#define VPU_PIX_FMT_SPK				v4l2_fourcc('S', 'P', 'K', '0')
#define VPU_PIX_FMT_DIVX			v4l2_fourcc('D', 'I', 'V', 'X')
#define VPU_PIX_FMT_LOGO			v4l2_fourcc('L', 'O', 'G', 'O')
#define V4L2_PIX_FMT_NV12_TILE                  v4l2_fourcc('N', 'A', '1', '2')  /* Y/CbCr 4:2:0 for i.MX8X 8bit */
#define V4L2_PIX_FMT_NV12_TILE_10BIT            v4l2_fourcc('N', 'T', '1', '2')  /* Y/CbCr 4:2:0 for i.MX8X 10bit */

#define IMX8X_HORIZONTAL_STRIDE			512
#define IMX8X_VERTICAL_STRIDE			512

int set_decoder_parameter_8x(void *arg);


#ifdef __cplusplus
}
#endif
#endif
