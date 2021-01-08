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
 * vpx_parse.c
 *
 * For VP8 / VP9
 *
 * Author Shijie Qin<Shijie.qin@nxp.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "parse.h"


struct ivf_header_t {
  unsigned char signature[4];
  unsigned short version;
  unsigned short headersize;
  unsigned int FourCC;
  unsigned short width;
  unsigned short height;
  unsigned int rate;
  unsigned int scale;
  unsigned int length;
  unsigned char unused[4];
};

#pragma pack(4)
struct ivf_frame_header_t {
  int frame_size;
  int64_t time_stamp;
};
#pragma pack()


static int vpx_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser;
	char *current = NULL;
	long left_bytes;
	int64_t frame_size;
	int64_t frame_idx = 0;
	int64_t ivf_hdr_size = sizeof(struct ivf_header_t);
	int64_t frame_hdr_size = sizeof(struct ivf_frame_header_t);

	if (!p)
		return -RET_E_INVAL;

	parser = (struct pitcher_parser *)p;
	current = parser->virt;
	left_bytes = parser->size;
	if (left_bytes < ivf_hdr_size)
	{
		PITCHER_ERR("source file is %ld, too small\n", left_bytes);
		return -RET_E_INVAL;
	}

	if (strncmp("DKIF", current, 5)) {
		PITCHER_ERR("source file signature incorrect\n");
		return -RET_E_INVAL;
	}

	if ((strncmp("VP80", (current + 8), 4) != 0 &&
	     strncmp("VP90", (current + 8), 4)) != 0) {
		PITCHER_ERR("source file format incorrect\n");
		return -RET_E_INVAL;
	}

	current += ivf_hdr_size;
	left_bytes -= ivf_hdr_size;

	while (left_bytes > frame_hdr_size) {
		/* parse frame size from frame header */
		frame_size = current[0] + (current[1] << 8) + (current[2] << 16) + (current[3] << 24);
		/* do not deliver frame header */
		current += frame_hdr_size;
		left_bytes -= frame_hdr_size;
		if (left_bytes < frame_size) {
			PITCHER_LOG("left_bytes(%ld) < frame_size(%ld)\n",
					left_bytes, frame_size);
			break;
		}

		pitcher_parser_push_new_frame(parser,
					      parser->size - left_bytes,
					      frame_size,
					      frame_idx++,
					      0);
		current += frame_size;
		left_bytes -= frame_size;
	}

	return RET_OK;
}

int vp8_parse(Parser p, void *arg)
{
	return vpx_parse(p, arg);
}

int vp9_parse(Parser p, void *arg)
{
	return vpx_parse(p, arg);
}
