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
#include <string.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "parse.h"

#define VP8_IVF_SEQ_HEADER_LEN		32
#define VP8_IVF_FRAME_HEADER_LEN	12


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

	parser->width = current[12] | (current[13] << 8);
	parser->height = current[14] | (current[15] << 8);
	PITCHER_LOG("resolution: <%d x %d>\n", parser->width, parser->height);

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

void vp8_insert_ivf_seqhdr(FILE *file, uint32_t width, uint32_t height,
			   uint32_t frame_rate)
{
	char data[VP8_IVF_SEQ_HEADER_LEN] = {0};

	if (!file)
		return;

	/* 0-3 signature "DKIF" */
	data[0] = 0x44;
	data[1] = 0x4b;
	data[2] = 0x49;
	data[3] = 0x46;
	/* 4-5 version: should be 0*/
	data[4] = 0x00;
	data[5] = 0x00;
	/* 6-7 header length */
	data[6] = VP8_IVF_SEQ_HEADER_LEN;
	data[7] = VP8_IVF_SEQ_HEADER_LEN >> 8;
	/* 8-11 VP80 fourcc */
	data[8] = 0x56;
	data[9] = 0x50;
	data[10] = 0x38;
	data[11] = 0x30;
	/* 12-13 width in pixels */
	data[12] = width;
	data[13] = width >> 8;
	/* 14-15 height in pixels */
	data[14] = height;
	data[15] = height >> 8;
	/* 16-19 frame rate */
	data[16] = frame_rate;
	data[17] = frame_rate >> 8;
	data[18] = frame_rate >> 16;
	data[19] = frame_rate >> 24;
	/* 20-23 time scale */
	data[20] = 0x01;
	data[21] = 0x00;
	data[22] = 0x00;
	data[23] = 0x00;
	/* 24-27 frames count */
	data[24] = 0xff;
	data[25] = 0xff;
	data[26] = 0xff;
	data[27] = 0xff;
	/* 28-31 reserved */

	fwrite(data, 1, VP8_IVF_SEQ_HEADER_LEN, file);
}

void vp8_insert_ivf_pichdr(FILE *file, unsigned long frame_size)
{
	char data[VP8_IVF_FRAME_HEADER_LEN] = {0};

	if (!file)
		return;

	/* 0-3 frame size */
	data[0] = frame_size;
	data[1] = frame_size >> 8;
	data[2] = frame_size >> 16;
	data[3] = frame_size >> 24;
	/* 4-11 timestamp */

	fwrite(data, 1, VP8_IVF_FRAME_HEADER_LEN, file);
}
