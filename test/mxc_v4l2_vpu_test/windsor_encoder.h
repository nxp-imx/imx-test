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
 * @file windsor_encoder.h
 *
 */

#ifndef __WINDSOR_ENCODER_H__
#define __WINDSOR_ENCODER_H__

#include <linux/types.h>

typedef  enum
{
  MEDIAIP_ENC_FMT_H264 = 0, // Only supported format
  MEDIAIP_ENC_FMT_VC1,
  MEDIAIP_ENC_FMT_MPEG2,
  MEDIAIP_ENC_FMT_MPEG4SP,
  MEDIAIP_ENC_FMT_H263,
  MEDIAIP_ENC_FMT_MPEG1,
  MEDIAIP_ENC_FMT_SHORT_HEADER,
  MEDIAIP_ENC_FMT_NULL

} MEDIAIP_ENC_FMT;

typedef enum
{
  MEDIAIP_ENC_PROF_MPEG2_SP = 0,
  MEDIAIP_ENC_PROF_MPEG2_MP,
  MEDIAIP_ENC_PROF_MPEG2_HP,
  MEDIAIP_ENC_PROF_H264_BP, // Only supported profiles
  MEDIAIP_ENC_PROF_H264_MP, // Only supported profiles
  MEDIAIP_ENC_PROF_H264_HP, // Only supported profiles
  MEDIAIP_ENC_PROF_MPEG4_SP,
  MEDIAIP_ENC_PROF_MPEG4_ASP,
  MEDIAIP_ENC_PROF_VC1_SP,
  MEDIAIP_ENC_PROF_VC1_MP,
  MEDIAIP_ENC_PROF_VC1_AP

} MEDIAIP_ENC_PROFILE;

typedef enum
{
  MEDIAIP_ENC_BITRATECONTROLMODE_VBR          = 0x00000001, // Not supported
  MEDIAIP_ENC_BITRATECONTROLMODE_CBR          = 0x00000002,
  MEDIAIP_ENC_BITRATECONTROLMODE_CONSTANT_QP  = 0x00000004   

} MEDIAIP_ENC_BITRATE_MODE, *pMEDIAIP_ENC_BITRATE_MODE;

typedef struct
{
  MEDIAIP_ENC_FMT           eCodecMode;      // Always set to MEDIAIP_ENC_FMT_H264
  MEDIAIP_ENC_PROFILE       eProfile;        // Set to MEDIAIP_ENC_PROF_H264_BP, MEDIAIP_ENC_PROF_H264_MP or MEDIAIP_ENC_PROF_H264_HP

  uint32_t                   uMemChunkAddr;   // Ignore
  uint32_t                   uMemChunkSize;   // Ignore

  uint32_t                   uFrameRate;      // Ignore
  uint32_t                   uSrcStride;      // Source YUV stride in pixels
  uint32_t                   uSrcWidth;       // Source YUV width in pixels
  uint32_t                   uSrcHeight;      // Source YUV height in pixels
  uint32_t                   uSrcOffset_x;    // Cropping support – ignore for the moment
  uint32_t                   uSrcOffset_y;    // Cropping support – ignore for the moment
  uint32_t                   uSrcCropWidth;   // Cropping support – ignore for the moment
  uint32_t                   uSrcCropHeight;  // Cropping support – ignore for the moment
  uint32_t                   uOutWidth;       // Encoded stream width in pixels
  uint32_t                   uOutHeight;      // Encoded stream height in pixels
  uint32_t                   uIFrameInterval; // GOP length
  uint32_t                   uGopBLength;     // GOP B length if B pictures enabled
  uint32_t                   uLowLatencyMode; // Ignore, placeholder for future development

  MEDIAIP_ENC_BITRATE_MODE  eBitRateMode;    // MEDIAIP_ENC_BITRATECONTROLMODE_CBR or MEDIAIP_ENC_BITRATECONTROLMODE_CONSTANT_QP
  uint32_t                   uTargetBitrate;  // Only relevant for MEDIAIP_ENC_BITRATECONTROLMODE_CBR, the target average bitrate in Kbps
  uint32_t                   uMaxBitRate;     // Only relevant for MEDIAIP_ENC_BITRATECONTROLMODE_CBR, the maximum tolerated bitrate in Kbps
  uint32_t                   uMinBitRate;     // Only relevant for MEDIAIP_ENC_BITRATECONTROLMODE_CBR, the minimum tolerated bitrate in Kbps
  uint32_t                   uInitSliceQP;    // Specify an initial slice QP for encode

} MEDIAIP_ENC_PARAM, *pMEDIAIP_ENC_PARAM;

#endif
