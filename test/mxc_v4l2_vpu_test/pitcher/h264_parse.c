
/*
 * Copyright 2020 NXP
 *
 * h264_parse.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "h264_parse.h"


static int h264_check_nal_type(char nal, const char *arr, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (nal == arr[i])
			return TRUE;
	}

	return FALSE;
}

int h264_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *start = NULL;
	char *current = NULL;
	char scode[] = AVC_SCODE;
	int next[] = AVC_SCODE;
	int scode_size = ARRAY_SIZE(scode);
	char nal_type[] = AVC_NAL_TYPE;
	char nal_code = 0x0;
	int prefix_len = 0;
	int prefix_len2 = 0;
	int offset = 0;
	int idx = 0;
	int end_flag = 0;
	int file_size;

	if (!p)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = start = parser->virt;
	file_size = parser->size;

	get_kmp_next(scode, next, scode_size);
	prefix_len = kmp_search(start, file_size, scode, scode_size, next);
	if (prefix_len < 0)
		return -RET_E_EMPTY;
	offset = prefix_len;
	current = start + offset;

	while (offset < file_size && !end_flag) {
		prefix_len = kmp_search(current + scode_size, file_size - offset,
					scode, scode_size, next);
		if (prefix_len < 0) {
			end_flag = 1;
			prefix_len = file_size - offset;
		}

		nal_code = current[scode_size] & 0x1f;
		if (h264_check_nal_type(nal_code, nal_type, ARRAY_SIZE(nal_type)))
		{
			struct pitcher_frame *frame = pitcher_calloc(1, sizeof(*frame));
			if (!frame) {
				PITCHER_ERR("allco pitcher_frame fail\n");
				return -RET_E_INVAL;
			}

			if (idx == 0)
				frame->offset = 0;
			else
				frame->offset = offset;
			frame->size = prefix_len + scode_size + prefix_len2;
			frame->idx = idx;
			idx++;

			if ((parser->number > 0 && idx == parser->number)
				|| end_flag == 1) {
				frame->flag = PITCHER_BUFFER_FLAG_LAST;
				end_flag = 1;
			}
			pitcher_parser_push(parser, frame);
			prefix_len2 = 0;
		} else {
			if (end_flag) {
				/* for end of data, add it to last frame */
				struct pitcher_frame *frame = pitcher_parser_last_frame(parser);

				frame->size += prefix_len;
				frame->flag = PITCHER_BUFFER_FLAG_LAST;
			} else {
				prefix_len2 += prefix_len + scode_size;
			}
		}
		offset += prefix_len + scode_size;
		current = start + offset;
	}

	parser->number = idx;
	pitcher_parser_seek_to_begin(parser);
	PITCHER_LOG("parse frame count : %ld\n", parser->number);
	return RET_OK;
}
