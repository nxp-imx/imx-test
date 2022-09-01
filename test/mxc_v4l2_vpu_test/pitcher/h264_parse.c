/*
 * Copyright 2018-2022 NXP
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
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"
#include "bitstream.h"

struct h264_parse_t {
	uint32_t header_cnt;
	uint32_t config_found;

	uint32_t max_frame_num;
	uint32_t frame_mbs_only_flag;
	uint32_t tf_found;
	uint32_t bf_found;
	uint32_t pre_frame_num;
	uint32_t cur_frame_num;
	uint32_t frame_cnt;		//number of frame or field
};

#define MAX_SLICE_HDR_SZ	32   //only need to parse part of slice header now !

static void scaling_list(uint32_t idx)
{

	uint32_t last_scale = 8;
	uint32_t next_scale = 8;
	uint32_t i, size;
	int delta;
	uint32_t scaling_val;

	size = idx < 6 ? 16 : 64;
	for (i = 0; i < size; i++) {
		if (next_scale) {
			READ_SVLC(&delta, "delta_scale");
			next_scale = (last_scale + delta + 256)&0xFF;
			if (!i && !next_scale) {
				// use default
				break;
			}
		}
		scaling_val = next_scale ? next_scale : last_scale;
		last_scale = (uint8_t)scaling_val;
	}
}

static void parse_sps_info(struct h264_parse_t *info)
{
	uint32_t value;
	uint32_t profile_idc;
	uint32_t chroma_format_idc;
	uint32_t pic_order_cnt_type;
	uint32_t mb_adaptive_frame_field_flag;
	int i;
	int val;

	PITCHER_DBG("=========== sps parse ===========\n");
	READ_CODE(&profile_idc, "profile_idc", 8);
	READ_FLAG(&value, "constraint_set0_flag");
	READ_FLAG(&value, "constraint_set1_flag");
	READ_FLAG(&value, "constraint_set2_flag");
	READ_FLAG(&value, "constraint_set3_flag");
	READ_FLAG(&value, "constraint_set4_flag");
	READ_FLAG(&value, "constraint_set5_flag");
	READ_CODE(&value, "reserved_zero_2bits", 2);
	READ_CODE(&value, "level_idc", 8);
	READ_UVLC(&value, "seq_parameter_set_id");
	if (profile_idc >= 100) {
		READ_UVLC(&chroma_format_idc, "chroma_format_idc");
		if (chroma_format_idc == 3) {
			READ_FLAG(&value, "separate_colour_plane_flag");
		}
		READ_UVLC(&value, "bit_depth_luma_minus8");
		READ_UVLC(&value, "bit_depth_chroma_minus8");
		READ_FLAG(&value, "qpprime_y_zero_transform_bypass_flag");
		READ_FLAG(&value, "seq_scaling_matrix_present_flag");
		if (value) {
			assert(chroma_format_idc != 3);
			for (i = 0; i < 8; i++) {
				READ_FLAG(&value, "seq_scaling_list_present_flag[i]");
				if (value) {
					scaling_list(i);
				}
			}
		}
	}

	READ_UVLC(&value, "log2_max_frame_num_minus4");
	info->max_frame_num = 1 << (value+4);
	READ_UVLC(&pic_order_cnt_type, "pic_order_cnt_type");
	if (pic_order_cnt_type == 0) {
		READ_UVLC(&pic_order_cnt_type, "log2_max_pic_order_cnt_lsb_minus4");
	} else if (pic_order_cnt_type == 1) {
		READ_FLAG(&value, "delta_pic_order_always_zero_flag");
		READ_SVLC(&val, "offset_for_non_ref_pic");
		READ_SVLC(&val, "offset_for_top_to_bottom_field");
		READ_UVLC(&value, "num_ref_frames_in_pic_order_cnt_cycle");
		for (i = 0; i < value ; i++) {
			READ_SVLC(&val, "offset_for_ref_frame[i]");
		}
	}

	READ_UVLC(&value, "max_num_ref_frames");
	READ_FLAG(&value, "gaps_in_frame_num_value_allowed_flag");
	READ_UVLC(&value, "pic_width_in_mbs_minus1");
	READ_UVLC(&value, "pic_height_in_map_units_minus1");
	READ_FLAG(&value, "frame_mbs_only_flag");
	info->frame_mbs_only_flag = value;
	if (info->frame_mbs_only_flag == 0) {
		READ_FLAG(&mb_adaptive_frame_field_flag, "mb_adaptive_frame_field_flag");
	}

	READ_FLAG(&value, "direct_8x8_inference_flag");
	READ_FLAG(&value, "frame_cropping_flag");
	if (value) {
		READ_UVLC(&value, "frame_crop_left_offset");
		READ_UVLC(&value, "frame_crop_right_offset");
		READ_UVLC(&value, "frame_crop_top_offset");
		READ_UVLC(&value, "frame_crop_bottom_offset");
	}
	READ_FLAG(&value, "vui_parameters_present_flag");
	if (value) {
		// skip vui parse !
	}
}

static void parse_slice_header_info(struct h264_parse_t *info, uint32_t *new_frame, int idr_flag)
{
	uint32_t value;
	uint32_t first_mb_in_slice;
	uint32_t frame_num;
	uint32_t field_pic_flag;
	uint32_t bottom_field_flag;
	uint32_t boundary_find = 0;
	int i;

	PITCHER_DBG("====== slice header parse =======\n");

	*new_frame = 0;
	READ_UVLC(&first_mb_in_slice, "first_mb_in_slice");

	READ_UVLC(&value, "slice_type");
	READ_UVLC(&value, "pic_parameter_set_id");
	//assert(separate_colour_plane_flag!=1);

	i = 0;
	while (info->max_frame_num >> i)
		i++;
	i--;
	READ_CODE(&frame_num, "frame_num", i);

	if (info->frame_mbs_only_flag == 0) {
		READ_FLAG(&field_pic_flag, "field_pic_flag");
		if (field_pic_flag) {
			READ_FLAG(&bottom_field_flag, "bottom_field_flag");
		}
	}

	if (first_mb_in_slice == 0) {			//TODO: only support first slice mb addr = 0 !!!
		info->pre_frame_num = (info->frame_cnt == 0) ? -1 : info->cur_frame_num;
		info->cur_frame_num = frame_num;
		info->frame_cnt++;
		if (info->frame_mbs_only_flag) {	// non-interlaced stream
			boundary_find = 1;
		} else {								// interlaced stream
			if (field_pic_flag == 0) {		// frame
				boundary_find = 1;
			} else {						// top/bottom fields
				if (info->bf_found == info->tf_found) {
					boundary_find = 1;
					info->tf_found = 0;
					info->bf_found = 0;
				}
				if (bottom_field_flag) {
					info->bf_found = 1;
				} else {
					info->tf_found = 1;
				}
			}
			if ((boundary_find == 0) &&
				(info->cur_frame_num != info->pre_frame_num)) {
				PITCHER_LOG("warning: stream may be corrupted !!!!!!\n");
				boundary_find = 1;			// corrupt stream
			}
		}
	}

	if (boundary_find) {
		*new_frame = 1;
	}

	if (idr_flag)	{	// if(IdrPicFlag)
		READ_UVLC(&value, "idr_pic_id");
	}

	//skip left parse !!!!
	//...
	//...

	PITCHER_DBG("[parse done]: pre frm: %d, cur frm: %d, frame boundary: %d\n", info->pre_frame_num, info->cur_frame_num, *new_frame);
}

static int h264_check_frame(uint8_t *p, uint32_t size, void *priv)
{
	uint8_t type;
	struct h264_parse_t *info = priv;
	bitstream_buf bs;
	uint8_t *nalbuf;
	int val_size;
	uint32_t new_frame = 0;
	static uint8_t *psps;

	if (size < 2)
		return PARSER_TYPE_UNKNOWN;

	type = p[0] & 0x1f;

	if (psps) {
		uint32_t sps_size = p - psps;

		nalbuf = malloc(sps_size);
		val_size = nal_remove_emul_bytes(psps, nalbuf, sps_size, 1, 0x000003);  // p hold nal header(1byte), nalbuf does't hold nal header
		bs_init(&bs, nalbuf, val_size);
		parse_sps_info(info);
		assert(((bs_consumed_bits(&bs) + 7) >> 3) <= sps_size);
		free(nalbuf);

		psps = NULL;
	}

	switch (type) {
	case 1: //Non-IDR
	case 5: //IDR
		if (!info->header_cnt)
			return PARSER_TYPE_UNKNOWN;

		nalbuf = malloc(MAX_SLICE_HDR_SZ);
		val_size = nal_remove_emul_bytes(p, nalbuf, min(size, MAX_SLICE_HDR_SZ), 1, 0x000003);  // p hold nal header(1byte), nalbuf does't hold nal header
		bs_init(&bs, nalbuf, val_size);
		parse_slice_header_info(info, &new_frame, type == 5);
		assert(((bs_consumed_bits(&bs) + 7) >> 3) <= MAX_SLICE_HDR_SZ);
		free(nalbuf);
		return new_frame ? PARSER_TYPE_FRAME : PARSER_TYPE_UNKNOWN;
	case 7: //SPS
		info->header_cnt++;
		psps = p;
	case 8: //PPS
	case 6: //SEI
		info->config_found = 1;
		return PARSER_TYPE_CONFIG;
	default:
		return PARSER_TYPE_UNKNOWN;
	}
}

static struct pitcher_parser_scode h264_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.extra_num = 4,
	.extra_code = 0x00000001,
	.extra_mask = 0xffffffff,
	.force_extra_on_first = 0,
	.check_frame = h264_check_frame,
	.priv_data_size = sizeof(struct h264_parse_t),
};

int h264_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &h264_scode);
}
