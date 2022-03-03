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
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "parse.h"

int vp6_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *current = NULL;
	unsigned long left_bytes;
	int64_t frame_size;
	int64_t frame_idx = 0;
        int64_t offset = 0;

        if (!p)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;
	left_bytes = parser->size;

	PITCHER_LOG("total file size: 0x%lx\n", left_bytes);
	while (left_bytes > 4) {
		frame_size = current[0] | (current[1] << 8) | (current[2] << 16) | (current[3] << 24);
		current += 4;
		left_bytes -= 4;
                offset += 4;
		if (left_bytes < frame_size) {
			PITCHER_LOG("left_bytes(0x%lx) < frame_size(0x%lx)\n",
					left_bytes, frame_size);
			break;
		}
		pitcher_parser_push_new_frame(parser,
					      offset,
					      frame_size,
					      frame_idx++,
					      0);
		current += frame_size;
                offset += frame_size;
                left_bytes -= frame_size;
        }

	return RET_OK;
}
