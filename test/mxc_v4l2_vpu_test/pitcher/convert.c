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
 * convert.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
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


static void convert_i420_to_nv12(struct convert_ctx *cvrt_ctx)
{
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	uint32_t width;
	uint32_t height;
	uint8_t *u_start;
	uint8_t *v_start;
	uint8_t *uv_temp;
	uint32_t uv_size;
	int i;
	int j;

	assert(cvrt_ctx);

	src = cvrt_ctx->src_buf;
	dst = cvrt_ctx->dst_buf;
	width = max(cvrt_ctx->width, cvrt_ctx->bytesperline);
	height = cvrt_ctx->height;
	uv_size = width * height / 2;

	switch (src->count) {
	case 1:
		u_start = src->planes[0].virt + width * height;
		v_start = u_start + uv_size / 2;
		break;
	case 2:
		u_start = src->planes[1].virt;
		v_start = u_start + uv_size / 2;
		break;
	case 3:
		u_start = src->planes[1].virt;
		v_start = src->planes[2].virt;
		break;
	default:
		return;
	}

	switch (dst->count) {
	case 1:
		uv_temp = dst->planes[0].virt + width * height;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
								width, height);
		break;
	case 2:
		dst->planes[0].bytesused = width * height;
		dst->planes[1].bytesused = uv_size;
		uv_temp = dst->planes[1].virt;
		break;
	default:
		return;
	}

	memcpy(dst->planes[0].virt, src->planes[0].virt, width * height);
	for (i = 0, j = 0; j < uv_size; j += 2, i++) {
		uv_temp[j] = u_start[i];
		uv_temp[j + 1] = v_start[i];
	}
}

static void __convert_nv12_tiled_to_linear(uint8_t *src, uint8_t *dst,
			uint32_t width, uint32_t height, uint32_t stride)
{
	uint32_t h_cnt;
	uint32_t v_cnt;
	uint32_t v_remain;
	uint32_t h_num;
	uint32_t v_num;
	uint32_t v_base_offset;
	uint8_t *inter_buf;
	uint8_t *cur_addr;
	uint32_t line_num;
	uint32_t line_base;
	uint32_t i;
	uint32_t j;
	uint32_t n;

	inter_buf = pitcher_calloc(1, 8 * 128);
	if (!inter_buf)	{
		PITCHER_ERR("failed to alloc inter_buf\n");
		return;
	}

	h_cnt = width / 8;
	v_cnt = (height + 127) / 128;
	v_remain = height % 128;

	for (v_num = 0; v_num < v_cnt; v_num++)	{
		v_base_offset = stride * 128 * v_num;

		for (h_num = 0; h_num < h_cnt; h_num++) {
			cur_addr = (uint8_t *)(src + h_num * (8 * 128) + v_base_offset);
			memcpy(inter_buf, cur_addr, 8 * 128);

			if (v_num == (v_cnt - 1) && v_remain != 0)
				n = v_remain;
			else
				n = 128;
			for (i = 0; i < n; i++) {
				line_num  = i + 128 * v_num;
				line_base = line_num * width;
				for (j = 0; j < 8; j++) {
					dst[line_base + (8 * h_num) + j] = inter_buf[8 * i + j];
				}
			}
		}
	}

	SAFE_RELEASE(inter_buf, pitcher_free);
}

static void convert_nv12_interlace_to_progress(uint8_t *y_src, uint8_t *uv_src,
		uint8_t *y_dst, uint8_t *uv_dst, uint32_t width, uint32_t height)
{
	uint32_t i;
	uint8_t *y_src_top = y_src;
	uint8_t *y_src_bot = y_src + width * height / 2;
	uint8_t *uv_src_top = uv_src;
	uint8_t *uv_src_bot = uv_src_top + width * height / 4;

	for (i = 0; i < height; i ++) {
		if (i & 1) {
			memcpy(y_dst, y_src_bot, width);
			y_src_bot += width;
		} else {
			memcpy(y_dst, y_src_top, width);
			y_src_top += width;
		}
		y_dst += width;
	}

	for (i = 0; i < height / 2; i ++) {
		if (i & 1) {
			memcpy(uv_dst, uv_src_bot, width);
			uv_src_bot += width;
		} else {
			memcpy(uv_dst, uv_src_top, width);
			uv_src_top += width;
		}
		uv_dst += width;
	}
}

static void convert_nv12_tiled_to_linear(struct convert_ctx *cvrt_ctx)
{
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	uint32_t height;
	uint32_t stride;
	uint8_t *y_src_start;
	uint8_t *uv_src_start;
	uint8_t *y_dst_start;
	uint8_t *uv_dst_start;

	assert(cvrt_ctx);

	src = cvrt_ctx->src_buf;
	dst = cvrt_ctx->dst_buf;
	height = ALIGN(cvrt_ctx->height, MALONE_ALIGN_H);
	stride = cvrt_ctx->bytesperline ? cvrt_ctx->bytesperline : ALIGN(cvrt_ctx->width, MALONE_ALIGN_LINE);

	switch (src->count) {
	case 1:
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[0].virt + stride * height;
		break;
	case 2:
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[1].virt;
		break;
	default:
		return;
	}

	switch (dst->count) {
	case 1:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[0].virt + cvrt_ctx->width * cvrt_ctx->height;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
						cvrt_ctx->width, cvrt_ctx->height);
		break;
	case 2:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[1].virt;
		dst->planes[0].bytesused = cvrt_ctx->width * cvrt_ctx->height;
		dst->planes[1].bytesused = cvrt_ctx->width * cvrt_ctx->height / 2;
		break;
	default:
		return;
	}

	if (cvrt_ctx->field == V4L2_FIELD_INTERLACED) {
		uint8_t *tmp_dst_buf = NULL;
		uint8_t *tmp_buf = NULL;
		uint8_t *uv_src = NULL;
		uint32_t dst_size = 0;
		int i;
		uint8_t *y_src_start_bot = y_src_start + stride * height / 2;
		uint8_t *uv_src_start_bot = uv_src_start + stride * height / 4;
		uint8_t *y_dst_start_bot = y_dst_start + cvrt_ctx->width * cvrt_ctx->height / 2;
		uint8_t *uv_dst_start_bot = uv_dst_start + cvrt_ctx->width * cvrt_ctx->height / 4;

		__convert_nv12_tiled_to_linear(y_src_start, y_dst_start, cvrt_ctx->width, cvrt_ctx->height / 2, stride);
		__convert_nv12_tiled_to_linear(y_src_start_bot, y_dst_start_bot, cvrt_ctx->width, cvrt_ctx->height / 2, stride);

		__convert_nv12_tiled_to_linear(uv_src_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height / 4, stride);
		__convert_nv12_tiled_to_linear(uv_src_start_bot, uv_dst_start_bot, cvrt_ctx->width, cvrt_ctx->height / 4, stride);

		for (i = 0; i < dst->count; i++)
			dst_size += dst->planes[i].bytesused;

		tmp_dst_buf = malloc(dst_size);
		if (!tmp_dst_buf) {
			PITCHER_ERR("allocate buffer fail\n");
			return;
		}
		tmp_buf = tmp_dst_buf;
		for (i = 0; i < cvrt_ctx->dst_buf->count; i++) {
			memcpy(tmp_buf, cvrt_ctx->dst_buf->planes[i].virt, cvrt_ctx->dst_buf->planes[i].bytesused);
			tmp_buf = tmp_dst_buf + cvrt_ctx->dst_buf->planes[i].bytesused;
		}

		uv_src = tmp_dst_buf + cvrt_ctx->width * cvrt_ctx->height;
		convert_nv12_interlace_to_progress(tmp_dst_buf, uv_src,
			y_dst_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height);

		SAFE_RELEASE(tmp_dst_buf, free);
	} else {
		__convert_nv12_tiled_to_linear(y_src_start, y_dst_start, cvrt_ctx->width, cvrt_ctx->height, stride);
		__convert_nv12_tiled_to_linear(uv_src_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height / 2, stride);
	}
}

static void __convert_nv12_tiled_to_linear_10bit(uint8_t *src, uint8_t *dst,
			uint32_t width, uint32_t height, uint32_t stride)
{
	int h_num, v_num, h_cnt, v_cnt, v_remain;
	int sx, sy, dx, dy;
	int i, j, n;
	uint8_t *psrc;
	uint8_t *pdst;
	int bit_pos, bit_loc;
	uint8_t first_byte, second_byte;
	uint16_t byte_16;
	int start_pos = 0;

	h_cnt = (width * 10 / 8) / 8;
	v_cnt = (height + 127) / 128;
	v_remain = height % 128;

	for (v_num = 0; v_num < v_cnt; v_num++)	{
		for (h_num = 0; h_num < h_cnt; h_num++) {
			sx = h_num * 8;
			sy = v_num * 128;
			dx = (sx * 8 + 9) / 10;
			dy = sy;

			if (v_num == (v_cnt - 1) && v_remain != 0)
				n = v_remain;
			else
				n = 128;
			for (i = 0; i < n; i++) {
				psrc = &src[sy * (stride) + h_num * (8 * 128) + i * 8];
				pdst = &dst[dy * width + i * width + dx];
				bit_pos = 10 * dx;

				/*
				 * front remain 2 bit, that means the first byte(8bit)
				 * all belone front pixel, should skip it
				 */
				if ((sx * 8 % 10) == 2)
					start_pos = 1;
				else
					start_pos = 0;

				for (j = start_pos; j < 8; j++) {
					first_byte = psrc[j];
					second_byte = (j != 7) ? psrc[j + 1] : psrc[8 * 128];
					byte_16 = (first_byte << 8) | second_byte;
					bit_loc = bit_pos % 8;


					*pdst = byte_16 >> (8 - bit_loc);
					pdst++;
					bit_pos += 10;
					/*
					 * first_byte use 2 bit, second_byte use 8 bit
					 * seconed_byte exhaused, should skip in next cycle
					 */
					if (bit_loc == 6)
						j++;
				}
			}
		}
	}
}

static void convert_nv12_tiled_to_linear_10bit(struct convert_ctx *cvrt_ctx)
{
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint8_t *y_src_start;
	uint8_t *uv_src_start;
	uint8_t *y_dst_start;
	uint8_t *uv_dst_start;

	assert(cvrt_ctx);

	src = cvrt_ctx->src_buf;
	dst = cvrt_ctx->dst_buf;
	width = ALIGN(cvrt_ctx->width, MALONE_ALIGN_W);
	height = ALIGN(cvrt_ctx->height, MALONE_ALIGN_H);
	stride = cvrt_ctx->bytesperline ? cvrt_ctx->bytesperline : ALIGN(width * 10 / 8, MALONE_ALIGN_LINE);

	switch (src->count) {
	case 1:
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[0].virt + stride * height;
		break;
	case 2:
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[1].virt;
		break;
	default:
		return;
	}

	switch (dst->count) {
	case 1:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[0].virt + cvrt_ctx->width * cvrt_ctx->height;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
						cvrt_ctx->width, cvrt_ctx->height);
		break;
	case 2:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[1].virt;
		dst->planes[0].bytesused = cvrt_ctx->width * cvrt_ctx->height;
		dst->planes[1].bytesused = cvrt_ctx->width * cvrt_ctx->height / 2;
		break;
	default:
		return;
	}

	if (cvrt_ctx->field == V4L2_FIELD_INTERLACED) {
		uint8_t *tmp_dst_buf = NULL;
		uint8_t *tmp_buf = NULL;
		uint8_t *uv_src = NULL;
		uint32_t dst_size = 0;
		int i;
		uint8_t *y_src_start_bot = y_src_start + stride * height / 2;
		uint8_t *uv_src_start_bot = uv_src_start + stride * height / 4;
		uint8_t *y_dst_start_bot = y_dst_start + cvrt_ctx->width * cvrt_ctx->height / 2;
		uint8_t *uv_dst_start_bot = uv_dst_start + cvrt_ctx->width * cvrt_ctx->height / 4;

		__convert_nv12_tiled_to_linear_10bit(y_src_start, y_dst_start, cvrt_ctx->width, cvrt_ctx->height / 2, stride);
		__convert_nv12_tiled_to_linear_10bit(y_src_start_bot, y_dst_start_bot, cvrt_ctx->width, cvrt_ctx->height / 2, stride);

		__convert_nv12_tiled_to_linear_10bit(uv_src_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height / 4, stride);
		__convert_nv12_tiled_to_linear_10bit(uv_src_start_bot, uv_dst_start_bot, cvrt_ctx->width, cvrt_ctx->height / 4, stride);

		for (i = 0; i < dst->count; i++)
			dst_size += dst->planes[i].bytesused;

		tmp_dst_buf = malloc(dst_size);
		if (!tmp_dst_buf) {
			PITCHER_ERR("allocate buffer fail\n");
			return;
		}
		tmp_buf = tmp_dst_buf;
		for (i = 0; i < cvrt_ctx->dst_buf->count; i++) {
			memcpy(tmp_buf, cvrt_ctx->dst_buf->planes[i].virt, cvrt_ctx->dst_buf->planes[i].bytesused);
			tmp_buf = tmp_dst_buf + cvrt_ctx->dst_buf->planes[i].bytesused;
		}

		uv_src = tmp_dst_buf + cvrt_ctx->width * cvrt_ctx->height;
		convert_nv12_interlace_to_progress(tmp_dst_buf, uv_src,
			y_dst_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height);

		SAFE_RELEASE(tmp_dst_buf, free);
		tmp_buf = NULL;
	} else {
		__convert_nv12_tiled_to_linear_10bit(y_src_start, y_dst_start, cvrt_ctx->width, cvrt_ctx->height, stride);
		__convert_nv12_tiled_to_linear_10bit(uv_src_start, uv_dst_start, cvrt_ctx->width, cvrt_ctx->height / 2, stride);
	}
}

static void convert_nv12_to_i420(struct convert_ctx *cvrt_ctx)
{
	struct pitcher_buffer *src;
	struct pitcher_buffer *dst;
	uint32_t width;
	uint32_t height;
	uint8_t *uv_start;
	uint8_t *u_temp;
	uint8_t *v_temp;
	uint32_t uv_size;
	int i;
	int j;

	assert(cvrt_ctx);

	src = cvrt_ctx->src_buf;
	dst = cvrt_ctx->dst_buf;
	width = max(cvrt_ctx->width, cvrt_ctx->bytesperline);
	height = cvrt_ctx->height;
	uv_size = width * height / 2;

	switch (src->count) {
	case 1:
		uv_start = src->planes[0].virt + width * height;
		break;
	case 2:
		uv_start = src->planes[1].virt;
		break;
	default:
		return;
	}

	switch (dst->count) {
	case 1:
		u_temp = dst->planes[0].virt + width * height;
		v_temp = u_temp + uv_size / 2;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
							  width, height);
		break;
	case 2:
		u_temp = dst->planes[1].virt;
		v_temp = u_temp + uv_size / 2;
		dst->planes[0].bytesused = width * height;
		dst->planes[1].bytesused = uv_size;
		break;
	case 3:
		u_temp = dst->planes[1].virt;
		v_temp = dst->planes[2].virt;
		dst->planes[0].bytesused = width * height;
		dst->planes[1].bytesused = uv_size / 2;
		dst->planes[1].bytesused = uv_size / 2;
		break;
	default:
		return;
	}

	memcpy(dst->planes[0].virt, src->planes[0].virt, width * height);
	for (i = 0, j = 0; j < uv_size; j += 2, i++) {
		u_temp[i] = uv_start[j];
		v_temp[i] = uv_start[j+1];
	}
}

static void __convert_nv12_tiled_to_i420(struct convert_ctx *cvrt_ctx)
{
	uint8_t *tmp_dst_buf = NULL;
	uint8_t *tmp_buf = NULL;
	uint32_t dst_size = 0;
	uint32_t width;
	uint32_t height;
	uint8_t *y_src, *uv_src;
	uint8_t *y_dst, *u_dst, *v_dst;
	uint32_t uv_size;
	int i, j;

	assert(cvrt_ctx);
	width = cvrt_ctx->width;
	height = cvrt_ctx->height;
	uv_size = width * height / 2;

	if (cvrt_ctx->src_fmt == V4L2_PIX_FMT_NV12_TILE)
		convert_nv12_tiled_to_linear(cvrt_ctx);
	else
		convert_nv12_tiled_to_linear_10bit(cvrt_ctx);

	for (i = 0; i < cvrt_ctx->dst_buf->count; i++)
		dst_size += cvrt_ctx->dst_buf->planes[i].bytesused;

	tmp_dst_buf = malloc(dst_size);
	if (!tmp_dst_buf) {
		PITCHER_ERR("allocate buf fail\n");
		return;
	}

	tmp_buf = tmp_dst_buf;
	for (i = 0; i < cvrt_ctx->dst_buf->count; i++) {
		memcpy(tmp_buf, cvrt_ctx->dst_buf->planes[i].virt, cvrt_ctx->dst_buf->planes[i].bytesused);
		tmp_buf = tmp_dst_buf + cvrt_ctx->dst_buf->planes[i].bytesused;
	}

	y_src = tmp_dst_buf;
	uv_src = tmp_dst_buf + width * height;

	y_dst = cvrt_ctx->dst_buf->planes[0].virt;
	switch (cvrt_ctx->dst_buf->count) {
	case 1:
		u_dst = cvrt_ctx->dst_buf->planes[0].virt + width * height;
		v_dst = u_dst + uv_size / 2;
		break;
	case 2:
		u_dst = cvrt_ctx->dst_buf->planes[1].virt;
		v_dst = u_dst + uv_size / 2;
		break;
	case 3:
		u_dst = cvrt_ctx->dst_buf->planes[1].virt;
		v_dst = cvrt_ctx->dst_buf->planes[2].virt;
		break;
	default:
		return;
	}

	memcpy(y_dst, y_src, width * height);
	for (i = 0, j = 0; j < uv_size; j += 2, i++) {
		u_dst[i] = uv_src[j];
		v_dst[i] = uv_src[j+1];
	}

	SAFE_RELEASE(tmp_dst_buf, free);
	tmp_buf = NULL;
}

static void convert_nv12_tiled_to_i420(struct convert_ctx *cvrt_ctx)
{
	__convert_nv12_tiled_to_i420(cvrt_ctx);
}

static void convert_nv12_tiled_to_i420_10bit(struct convert_ctx *cvrt_ctx)
{
	__convert_nv12_tiled_to_i420(cvrt_ctx);
}

static void convert_frame_to_nv12(struct convert_ctx *cvrt_ctx)
{
	switch (cvrt_ctx->src_fmt) {
	case V4L2_PIX_FMT_YUV420:
		convert_i420_to_nv12(cvrt_ctx);
		break;
        case V4L2_PIX_FMT_NV12_TILE:
                convert_nv12_tiled_to_linear(cvrt_ctx);
                break;
	case V4L2_PIX_FMT_NV12_TILE_10BIT:
		convert_nv12_tiled_to_linear_10bit(cvrt_ctx);
		break;
	default:
		break;
	}
}

static void convert_frame_to_i420(struct convert_ctx *cvrt_ctx)
{
	switch (cvrt_ctx->src_fmt) {
	case V4L2_PIX_FMT_NV12:
		convert_nv12_to_i420(cvrt_ctx);
		break;
        case V4L2_PIX_FMT_NV12_TILE:
                convert_nv12_tiled_to_i420(cvrt_ctx);
                break;
	case V4L2_PIX_FMT_NV12_TILE_10BIT:
		convert_nv12_tiled_to_i420_10bit(cvrt_ctx);
		break;
	default:
		break;
	}
}

void convert_frame(struct convert_ctx *cvrt_ctx)
{
	switch (cvrt_ctx->dst_fmt) {
	case V4L2_PIX_FMT_NV12:
		convert_frame_to_nv12(cvrt_ctx);
		break;
	case V4L2_PIX_FMT_YUV420:
		convert_frame_to_i420(cvrt_ctx);
		break;
	default:
		break;
	}
}

