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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "pitcher.h"
#include "pitcher_def.h"
#include "pitcher_v4l2.h"
#include "platform_8x.h"
#include "convert.h"

#define CVRT_MID_FMT16	PIX_FMT_P016

struct sw_cvrt_t {
	struct pix_fmt_info format;
	struct pitcher_buffer *mid;
	struct pix_fmt_info format16;
	struct pitcher_buffer *mid16;
};

static void unpack_tile_2_nv12(uint8_t * src, uint32_t tw, uint32_t x,
			       uint32_t y, uint8_t * dst, uint32_t stride,
			       uint32_t offset)
{
	uint32_t i;

	for (i = 0; i < y; i++) {
		memcpy(dst + offset, src, x);
		src += tw;
		dst += stride;
	}
}

static void swc_unpack_tile_2_nv12(uint8_t * src, uint32_t tw, uint32_t th,
				   uint32_t w, uint32_t h, uint32_t ntx,
				   uint32_t nty, uint8_t * dst, uint32_t stride,
				   uint32_t offset)
{
	uint32_t x;
	uint32_t i;
	uint32_t j;
	uint32_t ts = tw * th;
	uint32_t len_x = tw;
	uint32_t len_y;

	for (j = 0; j < nty; j++) {
		i = 0;
		len_y = min(th, h - th * j);
		for (x = 0, i = 0; x < w; x += len_x, i++) {
			len_x = min(tw, w - x);
			unpack_tile_2_nv12(src + ts * i, tw, len_x, len_y, dst,
					   stride, offset + tw * i);
		}
		src += ntx * ts;
		dst += (stride * th);
	}
}

static int swc_unpack_tiled_nv12(struct pitcher_buffer *src,
				 struct pitcher_buffer *dst)
{
	uint32_t tw = src->format->tile_ws;
	uint32_t th = src->format->tile_hs;
	uint32_t width = src->format->width;
	uint32_t height = src->format->height;
	uint32_t h;
	struct pitcher_buf_ref splane;
	struct pitcher_buf_ref dplane;

	if (src->format->interlaced) {
		h = height / 2;
		pitcher_get_buffer_plane(src, 0, &splane);
		pitcher_get_buffer_plane(dst, 0, &dplane);
		swc_unpack_tile_2_nv12(splane.virt, tw, th, width, h,
				       src->format->planes[0].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[0].line * 2, 0);
		swc_unpack_tile_2_nv12(splane.virt + splane.size / 2, tw, th,
				       width, h,
				       src->format->planes[0].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[0].line * 2, width);

		h = DIV_ROUND_UP(h, 2);
		pitcher_get_buffer_plane(src, 1, &splane);
		pitcher_get_buffer_plane(dst, 1, &dplane);
		swc_unpack_tile_2_nv12(splane.virt, tw, th, width, h,
				       src->format->planes[1].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[1].line * 2, 0);
		swc_unpack_tile_2_nv12(splane.virt + splane.size / 2, tw, th,
				       width, h,
				       src->format->planes[1].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[1].line * 2, width);
	} else {
		h = height;
		pitcher_get_buffer_plane(src, 0, &splane);
		pitcher_get_buffer_plane(dst, 0, &dplane);
		swc_unpack_tile_2_nv12(splane.virt, tw, th, width, h,
				       src->format->planes[0].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[0].line, 0);

		h = DIV_ROUND_UP(height, 2);
		pitcher_get_buffer_plane(src, 1, &splane);
		pitcher_get_buffer_plane(dst, 1, &dplane);
		swc_unpack_tile_2_nv12(splane.virt, tw, th, width, h,
				       src->format->planes[1].line / tw,
				       DIV_ROUND_UP(h, th), dplane.virt,
				       dst->format->planes[1].line, 0);
	}

	return 0;
}

#define GET_U8_BITS(__pdata, __mask, __shift)   (((*(uint8_t *)(__pdata)) >> (__shift)) & (__mask))
#define TILE_POS_TO_ADDR(base, offset, ts, tw, y, x) ((base) + (offset) + (((ts) * ((x) / (tw))) + (tw) * (y) + ((x) % (tw))))

static void get_10BE_pos(uint32_t x, int pos[2], int bits[2], uint8_t mask[2])
{
	const int depth = 10;
	int bit_index = depth * x;

	pos[0] = bit_index / 8;
	bits[0] = 8 - (bit_index % 8);
	bits[1] = depth - bits[0];
	pos[1] = pos[0] + 1;
	mask[0] = (1 << bits[0]) - 1;
	mask[1] = (1 << bits[1]) - 1;
}

static int swc_unpack_nv12_10be_8l128(struct pitcher_buffer *src,
				      struct pitcher_buffer *dst)
{
	uint32_t width = src->format->width;
	uint32_t height = src->format->height;
	uint32_t tw = src->format->tile_ws;
	uint32_t th = src->format->tile_hs;
	uint32_t ts = tw * th;
	uint8_t *luma = pitcher_get_frame_line_vaddr(src, 0, 0);
	uint8_t *chroma = pitcher_get_frame_line_vaddr(src, 1, 0);
	uint16_t *py;
	uint16_t *uv;
	uint32_t y;
	uint32_t x;
	uint32_t offset[2];
	uint32_t line_size[2];
	const uint8_t *pdata[2];
	int pos[2];
	int bits[2];
	uint8_t mask[2];
	uint16_t Y = 0, U = 0, V = 0;

	line_size[0] = src->format->planes[0].line * th;
	line_size[1] = src->format->planes[1].line * th;

	for (y = 0; y < height; y++) {
		py = pitcher_get_frame_line_vaddr(dst, 0, y);
		uv = pitcher_get_frame_line_vaddr(dst, 1, y / 2);

		offset[0] = (y / th) * line_size[0];
		offset[1] = ((y / 2) / th) * line_size[1];
		for (x = 0; x < width; x++) {
			get_10BE_pos(x, pos, bits, mask);
			pdata[0] = TILE_POS_TO_ADDR(luma, offset[0] ,ts, tw, y % th, pos[0]);
			pdata[1] = TILE_POS_TO_ADDR(luma, offset[0],ts, tw, y % th, pos[1]);
			Y = (GET_U8_BITS(pdata[0], mask[0], 0) << bits[1]) |
					GET_U8_BITS(pdata[1], mask[1], 8 - bits[1]);
			py[x] = Y << 6;

			if ((y % 2 == 0) && (x % 2 == 0)) {
				get_10BE_pos(x, pos, bits, mask);
				pdata[0] = TILE_POS_TO_ADDR(chroma, offset[1], ts, tw, (y / 2) % th, pos[0]);
				pdata[1] = TILE_POS_TO_ADDR(chroma, offset[1], ts, tw, (y / 2) % th, pos[1]);
				U = (GET_U8_BITS(pdata[0], mask[0], 0) << bits[1]) | GET_U8_BITS(pdata[1], mask[1], 8 - bits[1]);

				get_10BE_pos(x + 1, pos, bits, mask);
				pdata[0] = TILE_POS_TO_ADDR(chroma, offset[1], ts, tw, (y / 2) % th, pos[0]);
				pdata[1] = TILE_POS_TO_ADDR(chroma, offset[1], ts, tw, (y / 2) % th, pos[1]);
				V = (GET_U8_BITS(pdata[0], mask[0], 0) << bits[1]) | GET_U8_BITS(pdata[1], mask[1], 8 - bits[1]);

				uv[x] = U << 6;
				uv[x + 1] = V << 6;
			}
		}
	}

	return 0;
}

static int swc_unpack_i420(struct pitcher_buffer *src,
			   struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint8_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint8_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		memcpy(pdst, psrc, w);

		if (y < DIV_ROUND_UP(h, 2)) {
			uint8_t *pu = pitcher_get_frame_line_vaddr(src, 1, y);
			uint8_t *pv = pitcher_get_frame_line_vaddr(src, 2, y);
			uint8_t *uv = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				uv[x * 2] = pu[x];
				uv[x * 2 + 1] = pv[x];
			}
		}
	}

	return 0;
}

static int swc_pack_i420(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint8_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint8_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		memcpy(pdst, psrc, w);

		if (y < DIV_ROUND_UP(h, 2)) {
			uint8_t *uv = pitcher_get_frame_line_vaddr(src, 1, y);
			uint8_t *pu = pitcher_get_frame_line_vaddr(dst, 1, y);
			uint8_t *pv = pitcher_get_frame_line_vaddr(dst, 2, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				pu[x] = uv[x * 2];
				pv[x] = uv[x * 2 + 1];
			}
		}
	}

	return 0;
}

static int swc_unpack_yuyv(struct pitcher_buffer *src,
			   struct pitcher_buffer *dst)
{
	uint32_t x;
	uint32_t y;
	uint8_t *yuv;
	uint8_t *py;
	uint8_t *uv;

	for (y = 0; y < src->format->height; y++) {
		yuv = pitcher_get_frame_line_vaddr(src, 0, y);
		py = pitcher_get_frame_line_vaddr(dst, 0, y);
		if (y % 2 == 0)
			uv = pitcher_get_frame_line_vaddr(dst, 1, y / 2);

		for (x = 0; x < src->format->width; x++) {
			py[x] = yuv[x * 2];
			if (y % 2 == 0)
				uv[x] = yuv[x * 2 + 1];
		}
	}

	return 0;
}

static int swc_pack_yuyv(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t x;
	uint32_t y;
	uint8_t *yuv;
	uint8_t *py;
	uint8_t *uv;

	for (y = 0; y < src->format->height; y++) {
		py = pitcher_get_frame_line_vaddr(src, 0, y);
		uv = pitcher_get_frame_line_vaddr(src, 1, y / 2);
		yuv = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < src->format->width; x++) {
			yuv[x * 2] = py[x];
			yuv[x * 2 + 1] = uv[x];
		}
	}

	return 0;
}

static int swc_copy_nv12(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	struct pitcher_buf_ref splane;
	struct pitcher_buf_ref dplane;

	for (y = 0; y < h; y++) {
		pitcher_get_buffer_plane(src, 0, &splane);
		pitcher_get_buffer_plane(dst, 0, &dplane);
		memcpy(dplane.virt + y * dst->format->planes[0].line,
		       splane.virt + y * src->format->planes[0].line, w);

		if (y < DIV_ROUND_UP(h, 2)) {
			pitcher_get_buffer_plane(src, 1, &splane);
			pitcher_get_buffer_plane(dst, 1, &dplane);
			memcpy(dplane.virt + y * dst->format->planes[1].line,
			       splane.virt + y * src->format->planes[1].line,
			       w);
		}
	}

	return 0;
}

static int swc_copy_p0xx(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t width = src->format->width;
	uint32_t height = src->format->height;
	uint16_t *py;
	uint16_t *uv;
	uint16_t *pdst;
	uint32_t y;

	for (y = 0; y < height; y++) {
		py = pitcher_get_frame_line_vaddr(src, 0, y);
		pdst = pitcher_get_frame_line_vaddr(dst, 0, y);
		memcpy(pdst, py, width * 2);
		if (y < DIV_ROUND_UP(height, 2)) {
			uv = pitcher_get_frame_line_vaddr(src, 1, y);
			pdst = pitcher_get_frame_line_vaddr(dst, 1, y);
			memcpy(pdst, uv, width * 2);
		}
	}

	return 0;
}

static int swc_unpack_nvx2(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t width = src->format->width;
	uint32_t height = src->format->height;
	uint16_t *py;
	uint16_t *uv;
	uint8_t *psrc;
	uint32_t y;
	uint32_t x;
	uint32_t nr = 0;
	uint32_t size;

	for (y = 0; y < height; y++) {
		py = pitcher_get_frame_line_vaddr(dst, 0, y);
		psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		nr = 0;
		size = src->format->planes[0].line;
		for (x = 0; x < width; x++) {
			py[x] = pitcher_get_bits_val_le(psrc, size, nr, 10) << 6;
			nr += 10;
		}
		if (y < DIV_ROUND_UP(height, 2)) {
			uv = pitcher_get_frame_line_vaddr(dst, 1, y);
			psrc = pitcher_get_frame_line_vaddr(src, 1, y);
			size = src->format->planes[1].line;

			nr = 0;
			for (x = 0; x < width; x++) {
				uv[x] = pitcher_get_bits_val_le(psrc, size, nr, 10) << 6;
				nr += 10;
			}
		}
	}

	return 0;
}

static int swc_unpack_nv12_10le(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < w; x++)
			pdst[x] = psrc[x] << 6;

		if (y < DIV_ROUND_UP(h, 2)) {
			uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 1, y);
			uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < w; x++)
				pdst[x] = psrc[x] << 6;
		}
	}

	return 0;
}

static int swc_unpack_i420_10le(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < w; x++)
			pdst[x] = psrc[x] << 6;

		if (y < DIV_ROUND_UP(h, 2)) {
			uint16_t *pu = pitcher_get_frame_line_vaddr(src, 1, y);
			uint16_t *pv = pitcher_get_frame_line_vaddr(src, 2, y);
			uint16_t *uv = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				uv[x * 2] = pu[x] << 6;
				uv[x * 2 + 1] = pv[x] << 6;
			}
		}
	}

	return 0;
}

static int swc_pack_nv12_10le(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		memcpy(pdst, psrc, w * 2);
		for (x = 0; x < w; x++)
			pdst[x] = psrc[x] >> 6;

		if (y < DIV_ROUND_UP(h, 2)) {
			uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 1, y);
			uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < w; x++)
				pdst[x] = psrc[x] >> 6;
		}
	}

	return 0;
}

static int swc_pack_i420_10le(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;
	uint32_t y;
	uint32_t x;

	for (y = 0; y < h; y++) {
		uint16_t *psrc = pitcher_get_frame_line_vaddr(src, 0, y);
		uint16_t *pdst = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < w; x++)
			pdst[x] = psrc[x] >> 6;

		if (y < DIV_ROUND_UP(h, 2)) {
			uint16_t *uv = pitcher_get_frame_line_vaddr(src, 1, y);
			uint16_t *pu = pitcher_get_frame_line_vaddr(dst, 1, y);
			uint16_t *pv = pitcher_get_frame_line_vaddr(dst, 2, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				pu[x] = uv[x * 2] >> 6;
				pv[x] = uv[x * 2 + 1] >> 6;
			}
		}
	}

	return 0;
}

int pitcher_sw_unpack(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	int ret = -RET_E_NOT_SUPPORT;

	assert(dst->format->format == PIX_FMT_NV12);

	switch (src->format->format) {
	case PIX_FMT_NV12_8L128:
		ret = swc_unpack_tiled_nv12(src, dst);
		break;
	case PIX_FMT_I420:
		ret = swc_unpack_i420(src, dst);
		break;
	case PIX_FMT_YUYV:
		ret = swc_unpack_yuyv(src, dst);
		break;
	case PIX_FMT_NV21:
		break;
	case PIX_FMT_NV12:
		ret = swc_copy_nv12(src, dst);
		break;
	default:
		break;
	}

	return ret;
}

int pitcher_sw_pack(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	int ret = -RET_E_NOT_SUPPORT;

	assert(src->format->format == PIX_FMT_NV12);

	switch (dst->format->format) {
	case PIX_FMT_I420:
		ret = swc_pack_i420(src, dst);
		break;
	case PIX_FMT_YUYV:
		ret = swc_pack_yuyv(src, dst);
		break;
	case PIX_FMT_NV21:
		break;
	case PIX_FMT_NV12:
		ret = swc_copy_nv12(src, dst);
		break;
	default:
		break;
	}

	return ret;
}


int pitcher_sw_unpack_16(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	int ret = -RET_E_NOT_SUPPORT;

	assert(dst->format->format == PIX_FMT_P016);

	switch (src->format->format) {
	case PIX_FMT_NV12_10LE:
		ret = swc_unpack_nv12_10le(src, dst);
		break;
	case PIX_FMT_I420_10LE:
		ret = swc_unpack_i420_10le(src, dst);
		break;
	case PIX_FMT_NVX2:
		ret = swc_unpack_nvx2(src, dst);
		break;
	case PIX_FMT_P010:
	case PIX_FMT_P012:
		ret = swc_copy_p0xx(src, dst);
		break;
	case PIX_FMT_NV12_10BE_8L128:
		ret = swc_unpack_nv12_10be_8l128(src, dst);
		break;
	default:
		break;
	}

	return ret;
}

int pitcher_sw_pack_16(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	int ret = -RET_E_NOT_SUPPORT;

	assert(src->format->format == CVRT_MID_FMT16);

	switch (dst->format->format) {
	case PIX_FMT_NV12_10LE:
		ret = swc_pack_nv12_10le(src, dst);
		break;
	case PIX_FMT_I420_10LE:
		ret = swc_pack_i420_10le(src, dst);
		break;
	case PIX_FMT_P010:
	case PIX_FMT_P012:
		ret = swc_copy_p0xx(src, dst);
		break;
	default:
		break;
	}

	return ret;
}

int pitcher_sw_cvrt_8_to_16(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint8_t *p8;
	uint16_t *p16;
	uint32_t y;
	uint32_t x;
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;

	assert(src->format->format == PIX_FMT_NV12);
	assert(dst->format->format == CVRT_MID_FMT16);

	for (y = 0; y < h; y++) {
		p8 = pitcher_get_frame_line_vaddr(src, 0, y);
		p16 = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < w; x++)
			p16[x] = p8[x] << 8;

		if (y < DIV_ROUND_UP(h, 2)) {
			p8 = pitcher_get_frame_line_vaddr(src, 1, y);
			p16 = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				p16[x * 2] = p8[x * 2] << 8;
				p16[x * 2 + 1] = p8[x * 2 + 1] << 8;
			}
		}
	}

	return 0;
}

int pitcher_sw_cvrt_16_to_8(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint8_t *p8;
	uint16_t *p16;
	uint32_t y;
	uint32_t x;
	uint32_t w = src->format->width;
	uint32_t h = src->format->height;

	assert(src->format->format == CVRT_MID_FMT16);
	assert(dst->format->format == PIX_FMT_NV12);

	for (y = 0; y < h; y++) {
		p16 = pitcher_get_frame_line_vaddr(src, 0, y);
		p8 = pitcher_get_frame_line_vaddr(dst, 0, y);

		for (x = 0; x < w; x++)
			p8[x] = p16[x] >> 8;

		if (y < DIV_ROUND_UP(h, 2)) {
			p16 = pitcher_get_frame_line_vaddr(src, 1, y);
			p8 = pitcher_get_frame_line_vaddr(dst, 1, y);

			for (x = 0; x < DIV_ROUND_UP(w, 2); x++) {
				p8[x * 2] = p16[x * 2] >> 8;
				p8[x * 2 + 1] = p16[x * 2 + 1] >> 8;
			}
		}
	}

	return 0;
}

static int sw_convert_init_plane(struct pitcher_buf_ref *plane,
				 unsigned int index, void *arg)
{
	struct pix_fmt_info *format = arg;

	plane->size = format->planes[index].size;
	return pitcher_alloc_plane(plane, index, arg);
}

static int pitcher_sw_convert_alloc_mid_buffer(struct convert_ctx *ctx)
{
	struct sw_cvrt_t *swc = ctx->priv;
	struct pix_fmt_info format;
	struct pitcher_buffer_desc desc;

	memset(&format, 0, sizeof(format));
	format.format = PIX_FMT_NV12;
	format.width = ctx->src->format->width;
	format.height = ctx->src->format->height;
	pitcher_get_pix_fmt_info(&format, 0);
	if (swc->mid) {
		if (!pitcher_compare_format(swc->mid->format, &format))
			return 0;
		SAFE_RELEASE(swc->mid, pitcher_put_buffer);
	}
	swc->format = format;

	desc.init_plane = sw_convert_init_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.plane_count = swc->format.num_planes;
	desc.recycle = pitcher_auto_remove_buffer;
	desc.arg = &swc->format;
	swc->mid = pitcher_new_buffer(&desc);
	if (!swc->mid)
		return -RET_E_NO_MEMORY;
	swc->mid->format = &swc->format;

	return 0;
}

static int pitcher_sw_convert_alloc_mid16_buffer(struct convert_ctx *ctx)
{
	struct sw_cvrt_t *swc = ctx->priv;
	struct pix_fmt_info format;
	struct pitcher_buffer_desc desc;

	memset(&format, 0, sizeof(format));
	format.format = CVRT_MID_FMT16;
	format.width = ctx->src->format->width;
	format.height = ctx->src->format->height;
	pitcher_get_pix_fmt_info(&format, 0);
	if (swc->mid16) {
		if (!pitcher_compare_format(swc->mid16->format, &format))
			return 0;
		SAFE_RELEASE(swc->mid16, pitcher_put_buffer);
	}
	swc->format16 = format;

	desc.init_plane = sw_convert_init_plane;
	desc.uninit_plane = pitcher_free_plane;
	desc.plane_count = swc->format16.num_planes;
	desc.recycle = pitcher_auto_remove_buffer;
	desc.arg = &swc->format16;
	swc->mid16 = pitcher_new_buffer(&desc);
	if (!swc->mid16)
		return -RET_E_NO_MEMORY;
	swc->mid16->format = &swc->format16;

	return 0;
}

int pitcher_sw_convert_check_depth(struct pitcher_buffer *src, struct pitcher_buffer *dst)
{
	uint32_t src_depth = src->format->desc->comp[0].depth;
	uint32_t dst_depth = dst->format->desc->comp[0].depth;

	if (src_depth == 8 && dst_depth == 8)
		return TRUE;
	if (src_depth > 8 && dst_depth > 8)
		return TRUE;

	return FALSE;
}

int pitcher_sw_convert_frame(struct convert_ctx *ctx)
{
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	struct sw_cvrt_t *swc;
	int ret = 0;

	if (!ctx || !ctx->priv || !ctx->src || !ctx->dst)
		return -RET_E_NULL_POINTER;

	if (!ctx->src->format || !ctx->dst->format)
		return -RET_E_NULL_POINTER;
	if (ctx->src->format->width > ctx->dst->format->width ||
	    ctx->src->format->height > ctx->dst->format->height)
		return -RET_E_INVAL;
	if (ctx->dst->format->interlaced) {
		PITCHER_ERR("not support to convert to interlaced format\n");
		return -RET_E_NOT_SUPPORT;
	}
	if (ctx->src->format->interlaced &&
	    ctx->src->format->format != PIX_FMT_NV12_8L128) {
		PITCHER_ERR("not support to convert interlaced format %s\n",
		     pitcher_get_format_name(ctx->src->format->format));
		return -RET_E_NOT_SUPPORT;

	}

	if (ctx->src->format->format == PIX_FMT_NV12
	    && !ctx->src->format->interlaced) {
		src = ctx->src;
		dst = ctx->dst;
		return pitcher_sw_pack(src, dst);
	}

	if (ctx->src->format->format == CVRT_MID_FMT16 && !ctx->src->format->interlaced &&
			ctx->dst->format->desc->comp[0].depth > 8) {
		src = ctx->src;
		dst = ctx->dst;
		return pitcher_sw_pack_16(src, dst);
	}

	swc = ctx->priv;
	if (pitcher_sw_convert_check_depth(ctx->src, ctx->dst)) {
		if (ctx->dst->format->desc->comp[0].depth == 8) {
			if (ctx->dst->format->format != PIX_FMT_NV12) {
				ret = pitcher_sw_convert_alloc_mid_buffer(ctx);
				if (ret)
					return ret;
				dst = swc->mid;
			} else
				dst = ctx->dst;
		} else {
			if (ctx->dst->format->format != CVRT_MID_FMT16) {
				ret = pitcher_sw_convert_alloc_mid16_buffer(ctx);
				if (ret)
					return ret;
				dst = swc->mid16;
			} else
				dst = ctx->dst;
		}
	} else {
		ret = pitcher_sw_convert_alloc_mid_buffer(ctx);
		if (ret)
			return ret;
		ret = pitcher_sw_convert_alloc_mid16_buffer(ctx);
		if (ret)
			return ret;
		if (ctx->src->format->desc->comp[0].depth == 8)
			dst = swc->mid;
		else
			dst = swc->mid16;

	}

	src = ctx->src;
	if (src->format->desc->comp[0].depth == 8)
		ret = pitcher_sw_unpack(src, dst);
	else
		ret = pitcher_sw_unpack_16(src, dst);
	if (ret)
		return ret;
	if (dst == ctx->dst)
		return 0;

	src = dst;
	if (!pitcher_sw_convert_check_depth(src, ctx->dst)) {
		if (src->format->desc->comp[0].depth == 8) {
			dst = swc->mid16;
			ret = pitcher_sw_cvrt_8_to_16(src, dst);
		} else {
			dst = swc->mid;
			ret = pitcher_sw_cvrt_16_to_8(src, dst);
		}
		if (ret)
			return ret;
		src = dst;
	}

	dst = ctx->dst;
	if (src->format->desc->comp[0].depth == 8)
		return pitcher_sw_pack(src, dst);
	else
		return pitcher_sw_pack_16(src, dst);
}

void pitcher_free_sw_convert(struct convert_ctx *cvrt_ctx)
{
	struct sw_cvrt_t *swc;

	if (!cvrt_ctx)
		return;
	swc = cvrt_ctx->priv;
	cvrt_ctx->priv = NULL;
	if (swc) {
		SAFE_RELEASE(swc->mid, pitcher_put_buffer);
		SAFE_RELEASE(swc->mid16, pitcher_put_buffer);
	}
	SAFE_RELEASE(swc, pitcher_free);
	SAFE_RELEASE(cvrt_ctx, pitcher_free);
}

struct convert_ctx *pitcher_create_sw_convert(void)
{
	struct convert_ctx *ctx = NULL;
	struct sw_cvrt_t *swc = NULL;

	ctx = pitcher_calloc(1, sizeof(*ctx));
	if (!ctx)
		goto error;
	swc = pitcher_calloc(1, sizeof(*swc));
	if (!swc)
		goto error;

	ctx->convert_frame = pitcher_sw_convert_frame;
	ctx->free = pitcher_free_sw_convert;
	ctx->priv = swc;

	return ctx;
error:
	SAFE_RELEASE(swc, pitcher_free);
	SAFE_RELEASE(ctx, pitcher_free);
	return NULL;
}
