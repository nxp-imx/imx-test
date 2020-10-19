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
#include <string.h>

#include "vsi_parse.h"

#ifdef WEBM_ENABLED
#include <stdarg.h>
#include "nestegg/nestegg.h"
#endif /* WEBM_ENABLED */

/* Module defines */
#define VP8_FOURCC (0x30385056)
#define VP9_FOURCC (0x30395056)

struct FfReader {
  enum RdrFileFormat format;
  i32 bitstream_format;
  u32 ivf_headers_read;
  FILE* file;
#ifdef WEBM_ENABLED
  struct InputCtx input_ctx;
#endif /* WEBM_ENABLED */
};

typedef struct {
  unsigned char signature[4];  //='DKIF';
  unsigned short version;      //= 0;
  unsigned short headersize;   //= 32;
  unsigned int FourCC;
  unsigned short width;
  unsigned short height;
  unsigned int rate;
  unsigned int scale;
  unsigned int length;
  unsigned char unused[4];
} IVF_HEADER;

#pragma pack(4)
typedef struct {

  unsigned int frame_size;
  unsigned long long time_stamp;

} IVF_FRAME_HEADER;
#pragma pack()


#ifdef WEBM_ENABLED
struct InputCtx {
  nestegg* nestegg_ctx;
  nestegg_packet* pkt;
  unsigned int chunk;
  unsigned int chunks;
  unsigned int video_track;
};
#endif /* WEBM_ENABLED */

static i32 ReadIvfFileHeader(FILE* fin) {
  IVF_HEADER ivf;
  u32 tmp;

  tmp = fread(&ivf, sizeof(char), sizeof(IVF_HEADER), fin);
  if (tmp == 0) return -1;

  return 0;
}
static i32 ReadIvfFrameHeader(FILE* fin, u32* frame_size) {
  union {
    IVF_FRAME_HEADER ivf;
    u8 p[12];
  } fh;
  u32 tmp;

  tmp = fread(&fh, sizeof(char), sizeof(IVF_FRAME_HEADER), fin);
  if (tmp == 0) return -1;

  *frame_size = fh.p[0] + (fh.p[1] << 8) + (fh.p[2] << 16) + (fh.p[3] << 24);

  return 0;
}

int VpxRdrReadFrame(BSParserInst instance, u8* buffer, u8 *stream[2], i32* size, u8 is_ringbuffer, enum RdrFileFormat format) {
  u32 tmp;
  off_t frame_header_pos;
  u32 buf_len, offset;
  u32 frame_size = 0;
  u8* strm = stream[1];
  static u32 ivf_headers_read = 0;
  struct BSParser* inst = (struct BSParser* )instance;
  FILE* fin = inst->file;
  /* Read VPx IVF file header */
  if ((format == FF_VP8 || format == FF_VP9) && !ivf_headers_read) {
    tmp = ReadIvfFileHeader(fin);
    if (tmp != 0) return tmp;
    ivf_headers_read = 1;
  }

  frame_header_pos = ftello(fin);
  /* Read VPx IVF frame header */
  if (format == FF_VP8 || format == FF_VP9) {
    tmp = ReadIvfFrameHeader(fin, &frame_size);
    if (tmp != 0) return tmp;
  } else if (format == FF_VP7) {
    u8 size[4];
    tmp = fread(size, sizeof(u8), 4, fin);
    if (tmp != 4) return -1;
    frame_size = size[0] + (size[1] << 8) + (size[2] << 16) + (size[3] << 24);
  } else if (format == FF_WEBP) {
    char signature[] = "WEBP";
    char format[] = "VP8 ";
    char tmp[4];
    fseeko(fin, 8, SEEK_CUR);
    if (!fread(tmp, sizeof(u8), 4, fin)) return -1;
    if (strncmp(signature, tmp, 4)) return -1;
    if (!fread(tmp, sizeof(u8), 4, fin)) return -1;
    if (strncmp(format, tmp, 4)) return -1;
    if (!fread(tmp, sizeof(u8), 4, fin)) return -1;
    frame_size = tmp[0] + (tmp[1] << 8) + (tmp[2] << 16) + (tmp[3] << 24);
  }
  if (feof(fin)) {
    fprintf(stderr, "EOF: Input\n");
    return -1;
  }

  if (frame_size > *size) {
    fprintf(stderr, "Frame size %d > buffer size %d\n", frame_size, *size);
    fseeko(fin, frame_header_pos, SEEK_SET);
    *size = frame_size;
    return -1;
  }

    size_t result;
    if(is_ringbuffer) {
      offset = (u32)(strm - buffer);
      stream[0] = stream[1];
      buf_len = *size;
      if(offset + frame_size < buf_len) {
        result = fread((u8*)strm, sizeof(u8), frame_size, fin);
        stream[1] = strm + result;
      } else {
        u32 tmp_len;
        result = fread((u8*)strm, sizeof(u8), buf_len - offset, fin);
        tmp_len = fread((u8*)buffer, sizeof(u8), frame_size - (buf_len - offset), fin);
        result += tmp_len;
        stream[1] = buffer + tmp_len;
      }
    } else {
      result = fread((u8*)buffer, sizeof(u8), frame_size, fin);
      stream[0] = buffer;
      stream[1] = buffer + result;
    }

    if (result != frame_size) {
      /* fread failed. */
      return -1;
    }

  return frame_size;
}
