/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <linux/videodev2.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "vsi_parse.h"

extern u32 readDecodeUnit(FILE * fp, u8 * frame_buffer, u32 length, u32 whole_stream_mode);
extern int ByteStreamParserReadFrameH264(BSParserInst instance, u8* buffer, u8 *stream[2],
                            i32* size, u8 is_ringbuffer);
extern int ByteStreamParserReadFrame(BSParserInst instance, u8* buffer,
                              u8 *stream[2], i32* size, u8 is_ringbuffer);
extern int VpxRdrReadFrame(BSParserInst instance, u8* buffer, u8 *stream[2],
    i32* size, u8 is_ringbuffer, enum RdrFileFormat format);


int vsi_parse(Parser p, void *arg)
{
	struct pitcher_parser *parser = NULL;;
	int64_t readf_beg=0, byte_offset = 0;
	u32 append_byte = 0;
	int64_t offset = 0;
	int index = 0;
	i32 read_byte = 0;
	u8 *buffer = NULL;
	u8 *stream[2];
	u32 size = 0;
	u32 stream_end = 0;
	enum RdrFileFormat vpxformat = FF_NONE;
	assert(p);
	if (!p)
		return -RET_E_INVAL;
	parser = (struct pitcher_parser *)p;
	size = parser->size;
	struct BSParser*instance  = (struct BSParser*)parser->h;
	buffer = calloc(sizeof(u8), size);
	assert(buffer);
	while (!stream_end) {
		readf_beg = ftell(instance->file);
        switch(parser->format) {
            case V4L2_PIX_FMT_H264:
				stream[0] = buffer;
				stream[1] = buffer;
				read_byte = ByteStreamParserReadFrameH264(instance,
							buffer, stream, &size, 0);
				break;

			case V4L2_PIX_FMT_HEVC:
				stream[0] = buffer;
				stream[1] = buffer;
				read_byte = ByteStreamParserReadFrame(instance,
							buffer, stream, &size, 0);
				break;
			case V4L2_PIX_FMT_VP8:
			case V4L2_PIX_FMT_VP9:
				vpxformat = (parser->format == V4L2_PIX_FMT_VP8) ? FF_VP8 : FF_VP9;
				stream[0] = buffer;
				stream[1] = buffer;
				read_byte = VpxRdrReadFrame(instance, buffer, stream, &size, 0, vpxformat);
				if((read_byte < 0) && feof(instance->file)) {
					stream_end = 1;
					goto end;
				}
				byte_offset = ftell(instance->file) - read_byte;
				break;

			case V4L2_PIX_FMT_MPEG2:
				read_byte = readDecodeUnit(instance->file, buffer, size, 0);
			/* decrease 4 because previous function call read the first sequence start code */
				if(readf_beg == 0) {
					read_byte -= 4;
				}
				break;

			default:
				printf("Un-implement format\n");
				assert(0);
				break;
		}
		pitcher_parser_push_new_frame(parser, byte_offset, read_byte, index++,0);
		if((V4L2_PIX_FMT_VP8 != parser->format) &&
			(V4L2_PIX_FMT_VP9 != parser->format)) {
			byte_offset += read_byte;
		}
		stream_end = (ftell(instance->file) == parser->size);
	}
end:
	free(buffer);
	return RET_OK;
}

