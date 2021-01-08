/*
 * Copyright 2021 NXP
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
 * h263_parse.c
 *
 * for H263 / Sorenson Spark
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

static int h263_check_is_frame(int data)
{
	return ((data & 0xfc) == 0x80);
}

static int spk_check_is_frame(int data)
{
	return ((data & 0xf8) == 0x80);
}

static int __h263_parse(Parser p, int (*check_is_frame)(int))
{
	struct pitcher_parser *parser;
	char *current = NULL;
	char scode[] = {0x0, 0x0};
	int64_t next[] = {0x0, 0x0};
	int64_t scode_size = ARRAY_SIZE(scode);
	int64_t start = -1;
	int64_t offset = 0;
	int64_t end = 0;
	long left_bytes;
	int frame_count = 0;
	int index = 0;

	if (!p )
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;
	left_bytes = parser->size;
	get_kmp_next(scode, next, scode_size);

	PITCHER_LOG("total file size: 0x%lx\n", left_bytes);
	while (left_bytes > 0) {
		offset = kmp_search(current, left_bytes, scode, scode_size, next);
		if (offset < 0)
			break;

		if (start < 0)
			start = offset;

		current += offset + scode_size;
		left_bytes -= (offset + scode_size);
		if (left_bytes <= 0)
			break;
		if (check_is_frame(current[0])) {
			end = (current - parser->virt - scode_size);
			frame_count++;
		} else {
			/* for case of 00 00 00 80, need back 1 byte */
			current -= 1;
			left_bytes += 1;
		}

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

int h263_parse(Parser p, void *arg)
{
	return __h263_parse(p, h263_check_is_frame);
}
int spk_parse(Parser p, void *arg)
{
	return __h263_parse(p, spk_check_is_frame);
}
