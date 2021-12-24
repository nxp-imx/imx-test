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
/*
 * pitcher/pixfmt.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pitcher.h"
#include "pitcher_def.h"
#include "pitcher_v4l2.h"
#include "pixfmt.h"
#include "platform_8x.h"

static const struct pixel_format_desc pix_fmt_descriptors[PIX_FMT_NB] = {
	[PIX_FMT_I420] = {
		.name = "i420",
		.fourcc = V4L2_PIX_FMT_YUV420,
		.fourcc_nc = V4L2_PIX_FMT_YUV420M,
		.num_planes = 3,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{8, 8},
			{8, 8},
			{8, 8},
		},
	},
	[PIX_FMT_NV12] = {
		.name = "nv12",
		.fourcc = V4L2_PIX_FMT_NV12,
		.fourcc_nc = V4L2_PIX_FMT_NV12M,
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{8, 8},
			{16, 8},
		},
	},
	[PIX_FMT_NV21] = {
		.name = "nv21",
		.fourcc = V4L2_PIX_FMT_NV21,
		.fourcc_nc = V4L2_PIX_FMT_NV21M,
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{8, 8},
			{16, 8},
		},
	},
	[PIX_FMT_NV16] = {
		.name = "nv16",
		.fourcc = V4L2_PIX_FMT_NV16,
		.fourcc_nc = V4L2_PIX_FMT_NV16M,
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 0,
		.comp = {
			{8, 8},
			{16, 8},
		},
	},
	[PIX_FMT_YUYV] = {
		.name = "yuyv",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.num_planes = 1,
		.comp = {
			{16, 8},
		},
	},
	[PIX_FMT_YUV444] = {
		.name = "yuv24",
		.fourcc = V4L2_PIX_FMT_YUV24,
		.num_planes = 1,
		.comp = {
			{24, 8},
		},
	},
	[PIX_FMT_I420_10LE] = {
		.name = "yuv420p10le",
		.num_planes = 3,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{16, 10},
			{16, 10},
			{16, 10},
		},
	},
	[PIX_FMT_NV12_10LE] = {
		.name = "yuv420sp10le",
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{16, 10},
			{32, 10},
		},
	},
	[PIX_FMT_NV12_8L128] = {
		.name = "na12",
		.fourcc_nc = V4L2_PIX_FMT_NV12M_8L128,
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.tile_ws = 8,
		.tile_hs = 128,
		.comp = {
			{8, 8},
			{16, 8},
		},
	},
	[PIX_FMT_NV12_10BE_8L128] = {
		.name = "nt12",
		.fourcc_nc = V4L2_PIX_FMT_NV12M_10BE_8L128,
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.tile_ws = 8,
		.tile_hs = 128,
		.comp = {
			{10, 10},
			{20, 10},
		},
	},
	[PIX_FMT_DTRC] = {
		.name = "dtrc",
		.fourcc = v4l2_fourcc('D', 'T', 'R', 'C'),
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.tile_ws = 4,
		.tile_hs = 4,
		.comp = {
			{8, 8},
			{16, 8},
		},
	},
	[PIX_FMT_DTRC10] = {
		.name = "dtrc10",
		.fourcc = v4l2_fourcc('D', 'T', 'R', 'X'),
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.tile_ws = 5,
		.tile_hs = 4,
		.comp = {
			{10, 10},
			{20, 10},
		},
	},
	[PIX_FMT_P010] = {
		.name = "p010",
		.fourcc = v4l2_fourcc('P', '0', '1', '0'),
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{16, 10},
			{32, 10},
		},
	},
	[PIX_FMT_NVX2] = {
		.name = "nvx2",
		.fourcc = v4l2_fourcc('N', 'V', 'X', '2'),
		.num_planes = 2,
		.log2_chroma_w = 1,
		.log2_chroma_h = 1,
		.comp = {
			{10, 10},
			{20, 10},
		},
	},
	[PIX_FMT_RFC] = {
		.name = "rfc",
		.fourcc = v4l2_fourcc('R', 'F', 'C', '0'),
		.num_planes = 1,
	},
	[PIX_FMT_RFCX] = {
		.name = "rfcx",
		.fourcc = v4l2_fourcc('R', 'F', 'C', 'X'),
		.num_planes = 1,
	},
	[PIX_FMT_RGB565] = {
		.name = "rgb565",
		.fourcc = V4L2_PIX_FMT_RGB565,
		.num_planes = 1,
		.comp = {
			{16, 6},
		},
	},
	[PIX_FMT_BGR565] = {
		.name = "bgr565",
		.fourcc = V4L2_PIX_FMT_BGR565,
		.num_planes = 1,
		.comp = {
			{16, 6},
		},
	},
	[PIX_FMT_RGB555] = {
		.name = "rgb555",
		.fourcc = V4L2_PIX_FMT_RGB555,
		.num_planes = 1,
		.comp = {
			{16, 5},
		},
	},
	[PIX_FMT_RGBA] = {
		.name = "rgba",
		.fourcc = V4L2_PIX_FMT_RGBA32,
		.num_planes = 1,
		.comp = {
			{32, 8},
		},
	},
	[PIX_FMT_BGR32] = {
		.name = "bgr32",
		.fourcc = V4L2_PIX_FMT_BGR32,
		.num_planes = 1,
		.comp = {
			{32, 8},
		},
	},
	[PIX_FMT_ARGB] = {
		.name = "argb",
		.fourcc = V4L2_PIX_FMT_ABGR32,
		.num_planes = 1,
		.comp = {
			{32, 8},
		},
	},
	[PIX_FMT_RGBX] = {
		.name = "rgbx",
		.fourcc = V4L2_PIX_FMT_RGBX32,
		.num_planes = 1,
		.comp = {
			{32, 8},
		},
	},
	[PIX_FMT_H264] = {
		.name = "h264",
		.fourcc = V4L2_PIX_FMT_H264,
		.num_planes = 1,
	},
	[PIX_FMT_H265] = {
		.name = "h265",
		.fourcc = V4L2_PIX_FMT_HEVC,
		.num_planes = 1,
	},
	[PIX_FMT_VP8] = {
		.name = "vp8",
		.fourcc = V4L2_PIX_FMT_VP8,
		.num_planes = 1,
	},
	[PIX_FMT_VP9] = {
		.name = "vp9",
		.fourcc = V4L2_PIX_FMT_VP9,
		.num_planes = 1,
	},
	[PIX_FMT_VP6] = {
		.name = "vp6",
		.fourcc = VPU_PIX_FMT_VP6,
		.num_planes = 1,
	},
	[PIX_FMT_VC1L] = {
		.name = "vc1l",
		.fourcc = V4L2_PIX_FMT_VC1_ANNEX_L,
		.num_planes = 1,
	},
	[PIX_FMT_VC1G] = {
		.name = "vc1g",
		.fourcc = V4L2_PIX_FMT_VC1_ANNEX_G,
		.num_planes = 1,
	},
	[PIX_FMT_XVID] = {
		.name = "xvid",
		.fourcc = V4L2_PIX_FMT_XVID,
		.num_planes = 1,
	},
	[PIX_FMT_DIVX] = {
		.name = "divx",
		.fourcc = VPU_PIX_FMT_DIVX,
		.num_planes = 1,
	},
	[PIX_FMT_MPEG4] = {
		.name = "mpeg4",
		.fourcc = V4L2_PIX_FMT_MPEG4,
		.num_planes = 1,
	},
	[PIX_FMT_MPEG2] = {
		.name = "mpeg2",
		.fourcc = V4L2_PIX_FMT_MPEG2,
		.num_planes = 1,
	},
	[PIX_FMT_H263] = {
		.name = "h263",
		.fourcc = V4L2_PIX_FMT_H263,
		.num_planes = 1,
	},
	[PIX_FMT_AVS] = {
		.name = "avs",
		.fourcc = VPU_PIX_FMT_AVS,
		.num_planes = 1,
	},
	[PIX_FMT_SPK] = {
		.name = "spk",
		.fourcc = VPU_PIX_FMT_SPK,
		.num_planes = 1,
	},
	[PIX_FMT_RV] = {
		.name = "rv",
		.fourcc = VPU_PIX_FMT_RV,
		.num_planes = 1,
	},
	[PIX_FMT_JPEG] = {
		.name = "jpeg",
		.fourcc = V4L2_PIX_FMT_JPEG,
		.num_planes = 1,
	}
};

uint32_t pitcher_get_format_by_name(const char *name)
{
	uint32_t format;

	if (!name)
		return PIX_FMT_NONE;

	if (!strcasecmp(name, "yuv420p"))
		return PIX_FMT_I420;

	for (format = PIX_FMT_I420; format < PIX_FMT_NB; format++) {
		if (!pix_fmt_descriptors[format].name)
			continue;
		if (!strcasecmp(name, pix_fmt_descriptors[format].name))
			return format;
	}

	PITCHER_ERR("unsupport pixelformat : %s\n", name);
	return PIX_FMT_NONE;
}

uint32_t pitcher_get_format_by_fourcc(uint32_t fourcc)
{
	uint32_t format;

	for (format = PIX_FMT_I420; format < PIX_FMT_NB; format++) {
		if (pix_fmt_descriptors[format].fourcc_nc == fourcc)
			return format;
		if (pix_fmt_descriptors[format].fourcc == fourcc)
			return format;
	}

	return PIX_FMT_NONE;
}

uint32_t pitcher_get_format_num_planes(uint32_t format)
{
	if (format >= PIX_FMT_NB)
		return 0;

	return pix_fmt_descriptors[format].num_planes;
}

const char *pitcher_get_format_name(uint32_t format)
{
	if (format >= PIX_FMT_NB)
		return "unknown foramt";

	return pix_fmt_descriptors[format].name;
}

const struct pixel_format_desc *pitcher_get_format_desc_by_name(const char *name)
{
	uint32_t format = pitcher_get_format_by_name(name);

	if (format == PIX_FMT_NONE)
		return NULL;

	return &pix_fmt_descriptors[format];
}

static void pitcher_fill_plane_desc(struct pix_plane_info *plane, uint32_t line, uint32_t h)
{
	plane->line = line;
	plane->size = line * h;
}

int pitcher_get_pix_fmt_info(struct pix_fmt_info *info, uint32_t alignment)
{
	const struct pixel_format_desc *desc;
	uint32_t al_w;
	uint32_t al_h;
	uint32_t line;
	uint32_t w;
	uint32_t h;
	uint32_t i;
	uint32_t size = 0;

	if (!info)
		return -RET_E_NULL_POINTER;
	if (info->format >= PIX_FMT_NB || !info->width || !info->height)
		return -RET_E_INVAL;

	if (!alignment)
		alignment = 1;
	desc = &pix_fmt_descriptors[info->format];
	al_w = 1 << desc->log2_chroma_w;
	al_h = 1 << desc->log2_chroma_h;
	if (desc->tile_ws)
		al_w = max(al_w, desc->tile_ws);
	if (desc->tile_hs)
		al_h = max(al_h, desc->tile_hs);
	if (info->interlaced)
		al_h = (al_h << 1);

	for (i = 0; i < desc->num_planes; i++) {
		w = ALIGN(info->width, al_w);
		h = ALIGN(info->height, al_h);
		if (i) {
			if (desc->tile_hs)
				h = ALIGN(info->height, al_h << desc->log2_chroma_h);
			w >>= desc->log2_chroma_w;
			h >>= desc->log2_chroma_h;
		}
		line = desc->comp[i].bpp ? ALIGN(w * desc->comp[i].bpp, 8) >> 3 : w;
		line = ALIGN(line, alignment);
		pitcher_fill_plane_desc(&info->planes[i], line, h);
		size += info->planes[i].size;
	}

	info->num_planes = desc->num_planes;
	info->desc = desc;
	info->tile_ws = desc->tile_ws;
	info->tile_hs = desc->tile_hs;
	if (info->size < size)
		info->size = size;

	return RET_OK;
}

int pitcher_get_buffer_plane(struct pitcher_buffer *buf, int index, struct pitcher_buf_ref *plane)
{
	int i;

	if (!buf || !plane)
		return -RET_E_NULL_POINTER;

	if (!buf->format) {
		if (index >= buf->count)
			return -RET_E_INVAL;
		*plane = buf->planes[index];
		return 0;
	}
	if (index >= buf->format->num_planes)
		return -RET_E_INVAL;
	if (buf->count != 1 && buf->count != buf->format->num_planes)
		return -RET_E_INVAL;

	if (buf->count == buf->format->num_planes) {
		*plane = buf->planes[index];
		return 0;
	}

	*plane = buf->planes[0];
	for (i = 0; i < index; i++) {
		plane->virt += buf->format->planes[i].size;
		if (plane->phys)
			plane->phys += buf->format->planes[i].size;
		if (plane->bytesused)
			plane->bytesused -= buf->format->planes[i].size;
		if (plane->dmafd >= 0)
			plane->offset += buf->format->planes[i].size;
	}
	if (index == 0)
		plane->bytesused = buf->format->planes[0].size;
	plane->size = buf->format->planes[index].size;
	return 0;
}

unsigned long pitcher_get_buffer_plane_size(struct pitcher_buffer *buf, int index)
{
	struct pitcher_buf_ref plane;

	if (pitcher_get_buffer_plane(buf, index, &plane))
		return 0;

	return plane.size;
}

void *pitcher_get_frame_line_vaddr(struct pitcher_buffer *buf, int index, int y)
{
	struct pitcher_buf_ref plane;
	int ret;

	ret = pitcher_get_buffer_plane(buf, index, &plane);
	if (ret)
		return NULL;

	return plane.virt + y * buf->format->planes[index].line;
}

int pitcher_compare_format(struct pix_fmt_info *src, struct pix_fmt_info *dst)
{
	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->format != dst->format)
		return -RET_E_NOT_MATCH;
	if (src->width != dst->width)
		return -RET_E_NOT_MATCH;
	if (src->height != dst->height)
		return -RET_E_NOT_MATCH;
	if (src->interlaced != dst->interlaced)
		return -RET_E_NOT_MATCH;

	return 0;
}
