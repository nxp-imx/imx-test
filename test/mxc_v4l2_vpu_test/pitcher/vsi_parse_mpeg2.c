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
#include "vsi_parse.h"

#define MPEG2_WHOLE_STREAM_SAFETY_LIMIT (10*10*1024)

static u32 StartCode;
i32 strm_rew = 0;
u32 trace_used_stream = 0;
u32 previous_used = 0;
u8 *byte_strm_start;

u32 readDecodeUnit(FILE * fp, u8 * frame_buffer, u32 length, u32 whole_stream_mode)
{
  u32 idx = 0, VopStart = 0;
  u8 temp;
  u8 next_packet = 0;
  int ret;

    StartCode = 0;
    while(!VopStart) {

      ret = fread(&temp, sizeof(u8), 1, fp);

      if(feof(fp)) {

        fprintf(stdout, "TB: End of stream noticed in readDecodeUnit\n");
        idx += 4;
        break;
      }
      /* Reading the whole stream at once must be limited to buffer size */
      if((idx > (length - MPEG2_WHOLE_STREAM_SAFETY_LIMIT)) &&
          whole_stream_mode) {

        whole_stream_mode = 0;

      }

      frame_buffer[idx] = temp;

      if(idx >= (frame_buffer == byte_strm_start ? 4 : 3)) {
        if(!whole_stream_mode) {
          {
            /*-----------------------------------
                MPEG2 Start code
            -----------------------------------*/
            if(((frame_buffer[idx - 3] == 0x00) &&
                (frame_buffer[idx - 2] == 0x00) &&
                (((frame_buffer[idx - 1] == 0x01) &&
                  (frame_buffer[idx] == 0x00))))) {
              VopStart = 1;
              StartCode = ((frame_buffer[idx] << 24) |
                           (frame_buffer[idx - 1] << 16) |
                           (frame_buffer[idx - 2] << 8) |
                           frame_buffer[idx - 3]);
              /* MPEG2 start code found */
            }
          }
        }
      }
      if(idx >= length) {
        fprintf(stdout, "idx = %d,lenght = %d \n", idx, length);
        fprintf(stdout, "TB: Out Of Stream Buffer\n");
        break;
      }
      if(idx > strm_rew + 128) {
        idx -= strm_rew;
      }
      idx++;
      /* stop reading if truncated stream size is reached */
    }

    trace_used_stream = previous_used;
    previous_used += idx;
    return (idx);
}
