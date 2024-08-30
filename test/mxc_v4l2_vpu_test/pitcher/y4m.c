/*
 * Copyright 2024 NXP
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
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "pitcher.h"
#include "pitcher_def.h"
#include "pixfmt.h"

#define MAX_Y4M_HEADER		128
#define MAX_FRM_HEADER		80

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_FRAME_MAGIC_LEN 6

static uint32_t parse_csp(const char *csp)
{
	if (!strncmp(csp, "420", 3))
		return PIX_FMT_I420;

	return PIX_FMT_NONE;
}

int y4m_read_header(uint8_t *pdata, uint32_t size, struct pix_fmt_info *format, uint32_t *fps)
{
	char header[MAX_Y4M_HEADER + 10] = {0};
	int header_size = 0;
	char *tokstart, *tokend, *header_end;
	uint32_t width = -1, height = -1, pixelformat = PIX_FMT_I420;
	uint32_t framerate = 25;
	uint32_t n, d;
	int interlaced = 0;
	int i;

	for (i = 0; i < min(size, MAX_Y4M_HEADER); i++) {
		header[i] = *pdata++;
		if (header[i] == '\n') {
			/* Add a space after last option.
			 * Makes parsing "444" vs "444alpha" easier.
			 */
			header[i + 1] = 0x20;
			header[i + 2] = 0x0;
			header_size = i + 1;
			break;
		}
	}
	if (!header_size) {
		PITCHER_ERR("Y4M header too large.\n");
		return -RET_E_INVAL;
	}

	if (strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC))) {
		PITCHER_ERR("Invalid magic number for yuv4mpeg.\n");
		return -RET_E_INVAL;
	}

	header_end = &header[i + 1];
	for (tokstart = header + sizeof(Y4M_MAGIC); tokstart < header_end; tokstart++) {
		if( *tokstart == 0x20 )
			continue;
		switch( *tokstart++ ) {
		case 'W': // Width. Required.
			width = strtol(tokstart, &tokend, 10);
			tokstart = tokend;
			break;
		case 'H': // Height. Required.
			height = strtol(tokstart, &tokend, 10);
			tokstart = tokend;
			break;
		case 'C': // Color space
			pixelformat = parse_csp(tokstart);
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'I': // Interlace type
			switch( *tokstart++ ) {
			case 't':
			case 'b':
			case 'm':
				interlaced = 1;
				break;
			}
			break;
		case 'F': // Frame rate
			sscanf(tokstart, "%d:%d", &n, &d);
			if (n && d)
				framerate = n / d;
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'A': // Pixel aspect
			sscanf(tokstart, "%d:%d", &n, &d);
			tokstart = strchr(tokstart, 0x20);
			break;
		case 'X': // Vendor extensions
			tokstart = strchr(tokstart, 0x20);
			break;
		}
	}

	if (width < 0 || height < 0 || pixelformat == PIX_FMT_NONE) {
		PITCHER_ERR("YUV4MPEG has invalid header.\n");
		return -RET_E_INVAL;
	}

	if (format) {
		format->format = pixelformat;
		format->width = width;
		format->height = height;
		format->interlaced = interlaced;
	}
	if (fps)
		*fps = framerate;

	return header_size;
}

int y4m_read_frame(uint8_t *pdata, uint32_t size)
{
	char header[MAX_FRM_HEADER + 1] = {0};
	int header_size = 0;
	int i;

	for (i = 0; i < min(size, MAX_FRM_HEADER); i++) {
		header[i] = *pdata++;
		if (header[i] == '\n') {
			header[i + 1] = 0;
			header_size = i + 1;
			break;
		}
	}

	if (!header_size) {
		PITCHER_ERR("Y4M frame header too large.\n");
		return -RET_E_INVAL;
	}

	if (strncmp(header, Y4M_FRAME_MAGIC, strlen(Y4M_FRAME_MAGIC))) {
		PITCHER_ERR("Y4M frame header error.\n");
		return -RET_E_INVAL;
	}

	return header_size;
}

int y4m_write_header(FILE *filp, uint32_t width, uint32_t height, uint32_t framerate)
{
	char header[MAX_Y4M_HEADER + 10] = {0};
	int index = 0;

	if (!filp)
		return -RET_E_NULL_POINTER;

	index += snprintf(header + index, sizeof(header) - index, "YUV4MPEG2");
	index += snprintf(header + index, sizeof(header) - index, " W%d", width);
	index += snprintf(header + index, sizeof(header) - index, " H%d", height);
	if (framerate)
		index += snprintf(header + index, sizeof(header) - index, " F%d:%d", framerate, 1);
	index += snprintf(header + index, sizeof(header) - index, " Ip");
	index += snprintf(header + index, sizeof(header) - index, " C420jpeg");
	index += snprintf(header + index, sizeof(header) - index, " XYSCSS=420JPEG");
	index += snprintf(header + index, sizeof(header) - index, "\n");

	fwrite(header, 1, index, filp);
	return index;
}

int y4m_write_frame(FILE *filp)
{
	char header[MAX_FRM_HEADER + 1] = {0};
	int index = 0;

	if (!filp)
		return -RET_E_NULL_POINTER;

	index = snprintf(header, sizeof(header), Y4M_FRAME_MAGIC "\n");
	fwrite(header, 1, index, filp);
	return index;
}
