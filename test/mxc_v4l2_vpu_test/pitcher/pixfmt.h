/*
 * Copyright(c) 2021 NXP. All rights reserved.
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
#ifndef _PITCHER_PIXFMT_H
#define _PITCHER_PIXFMT_H
#ifdef __cplusplus
extern "C"
{
#endif

#include <linux/videodev2.h>

#define MAX_PLANES	8

enum pixel_format {
	PIX_FMT_I420 = 0,
	PIX_FMT_NV12,
	PIX_FMT_NV21,
	PIX_FMT_NV16,
	PIX_FMT_YUYV,
	PIX_FMT_YUV24,
	PIX_FMT_I420_10LE,
	PIX_FMT_NV12_10LE,
	PIX_FMT_NV12_8L128,
	PIX_FMT_NV12_10BE_8L128,
	PIX_FMT_DTRC,
	PIX_FMT_DTRC10,
	PIX_FMT_P010,
	PIX_FMT_P012,
	PIX_FMT_P016,
	PIX_FMT_NVX2,
	PIX_FMT_RFC,
	PIX_FMT_RFCX,
	PIX_FMT_RGB24,
	PIX_FMT_RGB565,
	PIX_FMT_BGR565,
	PIX_FMT_RGB555,
	PIX_FMT_RGBA,
	PIX_FMT_BGR32,
	PIX_FMT_ARGB,
	PIX_FMT_RGBX,
	PIX_FMT_BGR24,
	PIX_FMT_ABGR,
	PIX_FMT_B312,
	PIX_FMT_B412,
	PIX_FMT_GRAY,
	PIX_FMT_Y012,
	PIX_FMT_Y16,
	PIX_FMT_Y212,
	PIX_FMT_Y312,

	PIX_FMT_COMPRESSED,
	PIX_FMT_H264 = PIX_FMT_COMPRESSED,
	PIX_FMT_H265,
	PIX_FMT_VP8,
	PIX_FMT_VP9,
	PIX_FMT_VP6,
	PIX_FMT_VC1L,
	PIX_FMT_VC1G,
	PIX_FMT_XVID,
	PIX_FMT_DIVX,
	PIX_FMT_MPEG4,
	PIX_FMT_MPEG2,
	PIX_FMT_H263,
	PIX_FMT_AVS,
	PIX_FMT_SPK,
	PIX_FMT_RV,
	PIX_FMT_JPEG,

	PIX_FMT_NB,
	PIX_FMT_NONE = 0xffffffff,
};

struct pixel_format_desc {
	const char *name;
	uint32_t fourcc;
	uint32_t fourcc_nc;
	uint32_t num_planes;
	uint32_t log2_chroma_w;
	uint32_t log2_chroma_h;
	struct {
		uint32_t bpp;
		uint32_t depth;
	} comp[MAX_PLANES];
	uint32_t tile_ws;
	uint32_t tile_hs;
};

struct pix_plane_info {
	uint32_t line;
	uint32_t size;
};

struct pix_fmt_info {
	uint32_t format;
	const struct pixel_format_desc *desc;
	uint32_t width;
	uint32_t height;
	uint32_t interlaced;
	size_t size;
	uint32_t num_planes;
	struct pix_plane_info planes[MAX_PLANES];
	uint32_t tile_ws;
	uint32_t tile_hs;
};

uint32_t pitcher_get_format_by_name(const char *name);
uint32_t pitcher_get_format_by_fourcc(uint32_t fourcc);
uint32_t pitcher_get_format_num_planes(uint32_t format);
const char *pitcher_get_format_name(uint32_t format);
const struct pixel_format_desc *pitcher_get_format_desc_by_name(const char *name);
int pitcher_get_pix_fmt_info(struct pix_fmt_info *info, uint32_t alignment);

int pitcher_compare_format(struct pix_fmt_info *src, struct pix_fmt_info *dst);
#ifdef __cplusplus
}
#endif
#endif
