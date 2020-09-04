/*
 * Copyright 2020 NXP
 *
 * jpeg_parse.c
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "jpeg_parse.h"

int jpeg_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *current = NULL;
	char scode[] = {0xFF, 0xD8};
	int64_t next[] = {0, 0};
	int64_t scode_size = ARRAY_SIZE(scode);
	int64_t start = -1;
	int64_t offset = 0;
	int64_t end = 0;
	long left_bytes;
	int frame_count = 0;
	int index = 0;

	if (!p)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;
	left_bytes = parser->size;
	get_kmp_next(scode, next, scode_size);

	PITCHER_LOG("total file size: 0x%lx\n", left_bytes);
	while (left_bytes > 0) {
		offset = kmp_search(current, left_bytes, scode, scode_size,
					next);
		if (offset < 0)
			break;
		if (start < 0)
			start = offset;

		current += offset + scode_size;
		left_bytes -= (offset + scode_size);
		if (left_bytes <= 0)
			break;

		end = (current - parser->virt - scode_size);
		frame_count++;

		if (frame_count > 1) {
			frame_count--;
			pitcher_parser_push_new_frame(parser, start, end - start,
						      index++, 0);
			start = end;
		}
	}

	end = parser->size;
	if (frame_count) {
		frame_count--;
		pitcher_parser_push_new_frame(parser, start, end - start,
					      index++, 1);
		start = end;
	}

	return RET_OK;
}
