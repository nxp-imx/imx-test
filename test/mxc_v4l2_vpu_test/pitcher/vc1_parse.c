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
 * vc1_parse.c
 *
 * for VC1_L / VC1_G, support WMV3 only
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"


#define VC1_G_FRAME_TYPE        0x0D


static int byte_to_int(char *p, int n)
{
	int dst = 0;
	int i = 0;

	for (i = 0; i < n; i++)
		dst |= p[i] << i * 8;

	return dst;
}

static int rcv_parse_header(char *src, int *rcv_version, int *meta_size)
{
	int data = 0;
	char *p = src;
	int size = 0;

	/* number of frames */
	data = byte_to_int(p, 3);
	p += 3;
	/* extension bit, rcv codec version */
	data = byte_to_int(p, 1);
	p += 1;
	if (data & 0x40)
		*rcv_version = 1;
	else
		*rcv_version = 0;
	/* meta data size */
	*meta_size = byte_to_int(p, 4);
	p += 4;
	size = (4 + 4 * (*rcv_version)) * 4 + (*meta_size);

	return size;
}

static int rcv_frame_size(char *src)
{
	return byte_to_int(src, 3);
}

int vc1l_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *current = NULL;
	int64_t offset = 0;
	int64_t size = 0;
	int index = 0;
	int rcv_version = 0;
	int meta_size = 0;

	if (!p)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;

	PITCHER_LOG("total file size: 0x%lx\n", parser->size);

	size = rcv_parse_header(current, &rcv_version, &meta_size);
	/* Input meta data only */
	pitcher_parser_push_new_frame(parser, 8, meta_size, index++, 0);
	offset += size + (4 + 4 * rcv_version);
	current += size;

	while (offset < parser->size) {
		size = rcv_frame_size(current);

		if (offset + size > parser->size)
			break;
		pitcher_parser_push_new_frame(parser, offset, size, index++, 0);
		offset += size + (4 + 4 * rcv_version);
		current += size + (4 + 4 * rcv_version);
	}

	return RET_OK;
}

int vc1g_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *current = NULL;
	char scode[] = {0x0, 0x0, 0x1};
	int64_t next[] = {0x0, 0x0, 0x1};
	int64_t scode_size = ARRAY_SIZE(scode);
	int64_t start = -1;
	int64_t offset = 0;
	int64_t end = 0;
	long left_bytes;
	int index = 0;

	if (!p)
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

		/* first input containing codec data only */
		if (current[0] == VC1_G_FRAME_TYPE) {
			end = (current - parser->virt - scode_size);
			pitcher_parser_push_new_frame(parser, start, end - start,
						      index++, 0);
			start = end;
		}
	}

	end = parser->size;
	pitcher_parser_push_new_frame(parser, start, end - start,
				      index++, 1);

	return RET_OK;
}
