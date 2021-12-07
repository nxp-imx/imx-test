/*
 * Copyright 2018-2021 NXP
 *
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * platform_8x.h
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


#define VPU_PIX_FMT_AVS				v4l2_fourcc('A', 'V', 'S', '0')
#define VPU_PIX_FMT_ASP				v4l2_fourcc('A', 'S', 'P', '0')
#define VPU_PIX_FMT_RV				v4l2_fourcc('R', 'V', '0', '0')
#define VPU_PIX_FMT_VP6				v4l2_fourcc('V', 'P', '6', '0')
#define VPU_PIX_FMT_SPK				v4l2_fourcc('S', 'P', 'K', '0')
#define VPU_PIX_FMT_DIVX			v4l2_fourcc('D', 'I', 'V', 'X')
#define VPU_PIX_FMT_LOGO			v4l2_fourcc('L', 'O', 'G', 'O')
#ifndef V4L2_PIX_FMT_NV12M_8L128
#define V4L2_PIX_FMT_NV12M_8L128		v4l2_fourcc('N', 'A', '1', '2') /* Y/CbCr 4:2:0 8x128 tiles */
#endif
#ifndef V4L2_PIX_FMT_NV12M_10BE_8L128
#define V4L2_PIX_FMT_NV12M_10BE_8L128		v4l2_fourcc_be('N', 'T', '1', '2') /* Y/CbCr 4:2:0 10-bit 8x128 tiles */
#endif

int set_decoder_parameter_8x(void *arg);

#ifdef __cplusplus
}
#endif
#endif
