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

static int vc1g_check_frame(uint8_t *p, uint32_t size)
{
	uint8_t type;

	if (!p || !size)
		return PARSER_TYPE_UNKNOWN;

	type = p[0];
	switch (type) {
	case 0x0D: //Frame
		return PARSER_TYPE_FRAME;
	case 0x0F: //Sequence Header
		return PARSER_TYPE_FRAME;
	default:
		return PARSER_TYPE_UNKNOWN;
	}
}

static struct pitcher_parser_scode vc1g_scode = {
	.scode = 0x000001,
	.mask = 0xffffff,
	.num = 3,
	.check_frame = vc1g_check_frame,
};

int vc1g_parse(Parser p, void *arg)
{
	return pitcher_parse_startcode(p, &vc1g_scode);
}
