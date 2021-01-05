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
#include "pitcher.h"
#include "pitcher_def.h"
#include "pitcher_v4l2.h"
#include "platform_8x.h"
#include "convert.h"


static void convert_i420_to_nv12(struct pitcher_buffer *src,
				struct pitcher_buffer *dst,
				uint32_t width, uint32_t height)
{
	uint8_t *u_start;
	uint8_t *v_start;
	uint8_t *uv_temp;
	uint32_t uv_size = width * height / 2;
	int i;
	int j;

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
					   uint32_t width, uint32_t height)
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
	uint32_t stride;

	inter_buf = pitcher_calloc(1, 8 * 128);
	if (!inter_buf)	{
		PITCHER_ERR("failed to alloc inter_buf\n");
		return;
	}

	h_cnt = width / 8;
	v_cnt = (height + 127) / 128;
	v_remain = height % 128;

	stride = ALIGN(width, IMX8X_HORIZONTAL_STRIDE);

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

static void convert_nv12_tiled_to_linear(struct pitcher_buffer *src,
					 struct pitcher_buffer *dst,
					 uint32_t width, uint32_t height)
{
	uint8_t *y_src_start;
	uint8_t *uv_src_start;
	uint8_t *y_dst_start;
	uint8_t *uv_dst_start;
	uint32_t align_width = ALIGN(width, IMX8X_HORIZONTAL_STRIDE);
	uint32_t align_height = ALIGN(height, IMX8X_VERTICAL_STRIDE);

	switch (src->count) {
	case 1:
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[0].virt + align_width * align_height;
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
		uv_dst_start = dst->planes[0].virt + width * height;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
								width, height);
		break;
	case 2:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[1].virt;
		dst->planes[0].bytesused = width * height;
		dst->planes[1].bytesused = width * height / 2;
		break;
	default:
		return;
	}

	__convert_nv12_tiled_to_linear(y_src_start, y_dst_start, width, height);
	__convert_nv12_tiled_to_linear(uv_src_start, uv_dst_start, width, height / 2);
}

static void __convert_nv12_tiled_to_linear_10bit(uint8_t *src, uint8_t *dst,
					         uint32_t width, uint32_t height)
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
	uint32_t stride;

	h_cnt = (width * 10 / 8) / 8;
	v_cnt = (height + 127) / 128;
	v_remain = height % 128;

	stride = ALIGN((width * 10 / 8), IMX8X_HORIZONTAL_STRIDE);

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

static void convert_nv12_tiled_to_linear_10bit(struct pitcher_buffer *src,
					       struct pitcher_buffer *dst,
					       uint32_t width, uint32_t height)
{
	uint8_t *y_src_start;
	uint8_t *uv_src_start;
	uint8_t *y_dst_start;
	uint8_t *uv_dst_start;
	uint32_t align_width;
	uint32_t align_height;

	switch (src->count) {
	case 1:
		align_width = width * 10 / 8;
		align_width = ALIGN(align_width, IMX8X_HORIZONTAL_STRIDE);
		align_height = ALIGN(height, IMX8X_VERTICAL_STRIDE);
		y_src_start = src->planes[0].virt;
		uv_src_start = src->planes[0].virt + align_width * align_height;
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
		uv_dst_start = dst->planes[0].virt + width * height;
		dst->planes[0].bytesused = get_image_size(V4L2_PIX_FMT_NV12,
								width, height);
		break;
	case 2:
		y_dst_start = dst->planes[0].virt;
		uv_dst_start = dst->planes[1].virt;
		dst->planes[0].bytesused = width * height;
		dst->planes[1].bytesused = width * height / 2;
		break;
	default:
		return;
	}

	__convert_nv12_tiled_to_linear_10bit(y_src_start, y_dst_start,
					     width, height);
	__convert_nv12_tiled_to_linear_10bit(uv_src_start, uv_dst_start,
					     width, height / 2);
}

static void convert_frame_to_nv12(struct pitcher_buffer *src,
				struct pitcher_buffer *dst,
				uint32_t fmt, uint32_t width, uint32_t height)
{
	switch (fmt) {
	case V4L2_PIX_FMT_YUV420:
		convert_i420_to_nv12(src, dst, width, height);
		break;
        case V4L2_PIX_FMT_NV12_TILE:
                convert_nv12_tiled_to_linear(src, dst, width, height);
                break;
	case V4L2_PIX_FMT_NV12_TILE_10BIT:
		convert_nv12_tiled_to_linear_10bit(src, dst, width, height);
		break;
	default:
		break;
	}
}

void convert_frame(struct pitcher_buffer *src,
		   struct pitcher_buffer *dst,
		   uint32_t src_fmt, uint32_t dst_fmt,
		   uint32_t width, uint32_t height)
{
	switch (dst_fmt) {
	case V4L2_PIX_FMT_NV12:
		convert_frame_to_nv12(src, dst, src_fmt, width, height);
		break;
	default:
		break;
	}
}

