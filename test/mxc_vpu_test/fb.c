/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/* 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
   in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from 
   this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE 
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "vpu_test.h"

struct frame_buf *framebuf_alloc(struct frame_buf *fb, int stdMode, int format, int strideY, int height, int mvCol)
{
	int err;
	int divX, divY;

	if (fb == NULL)
		return NULL;

	divX = (format == MODE420 || format == MODE422) ? 2 : 1;
	divY = (format == MODE420 || format == MODE224) ? 2 : 1;

	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
	fb->desc.size = (strideY * height  + strideY / divX * height / divY * 2);
	if (mvCol)
		fb->desc.size += strideY / divX * height / divY;

	err = IOGetPhyMem(&fb->desc);
	if (err) {
		printf("Frame buffer allocation failure\n");
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	fb->addrY = fb->desc.phy_addr;
	fb->addrCb = fb->addrY + strideY * height;
	fb->addrCr = fb->addrCb + strideY / divX * height / divY;
	fb->strideY = strideY;
	fb->strideC =  strideY / divX;
	if (mvCol)
		fb->mvColBuf = fb->addrCr + strideY / divX * height / divY;

	if (IOGetVirtMem(&(fb->desc)) == -1) {
		IOFreePhyMem(&fb->desc);
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	return fb;
}

int tiled_framebuf_base(FrameBuffer *fb, Uint32 frame_base, int strideY, int height, int mapType)
{
	int align;
	int divX, divY;
	Uint32 lum_top_base, lum_bot_base, chr_top_base, chr_bot_base;
	Uint32 lum_top_20bits, lum_bot_20bits, chr_top_20bits, chr_bot_20bits;
	int luma_top_size, luma_bot_size, chroma_top_size, chroma_bot_size;

	divX = 2;
	divY = 2;

	/*
	 * The buffers is luma top, chroma top, luma bottom and chroma bottom for
	 * tiled map type, and only 20bits for the address description, so we need
	 * to do 4K page align for each buffer.
	 */
	align = SZ_4K;
	if (mapType == TILED_FRAME_MB_RASTER_MAP) {
		/* luma_top_size means the Y size of one frame, chroma_top_size
		 * means the interleaved UV size of one frame in frame tiled map type*/
		luma_top_size = (strideY * height + align - 1) & ~(align - 1);
		chroma_top_size = (strideY / divX * height / divY * 2 + align - 1) & ~(align - 1);
		luma_bot_size = chroma_bot_size = 0;
	} else {
		/* This is FIELD_FRAME_MB_RASTER_MAP case, there are two fields */
		luma_top_size = (strideY * height / 2 + align - 1) & ~(align - 1);
		luma_bot_size = luma_top_size;
		chroma_top_size = (strideY / divX * height / divY + align - 1) & ~(align - 1);
		chroma_bot_size = chroma_top_size;
	}

	lum_top_base = (frame_base + align - 1) & ~(align -1);
	chr_top_base = lum_top_base + luma_top_size;
	if (mapType == TILED_FRAME_MB_RASTER_MAP) {
		lum_bot_base = 0;
		chr_bot_base = 0;
	} else {
		lum_bot_base = chr_top_base + chroma_top_size;
		chr_bot_base = lum_bot_base + luma_bot_size;
	}

	lum_top_20bits = lum_top_base >> 12;
	lum_bot_20bits = lum_bot_base >> 12;
	chr_top_20bits = chr_top_base >> 12;
	chr_bot_20bits = chr_bot_base >> 12;

	/*
	 * In tiled map format the construction of the buffer pointers is as follows:
	 * 20bit = addrY [31:12]: lum_top_20bits
	 * 20bit = addrY [11: 0], addrCb[31:24]: chr_top_20bits
	 * 20bit = addrCb[23: 4]: lum_bot_20bits
	 * 20bit = addrCb[ 3: 0], addrCr[31:16]: chr_bot_20bits
	 */
	fb->bufY = (lum_top_20bits << 12) + (chr_top_20bits >> 8);
	fb->bufCb = (chr_top_20bits << 24) + (lum_bot_20bits << 4) + (chr_bot_20bits >> 16);
	fb->bufCr = chr_bot_20bits << 16;

	return 0;
}

struct frame_buf *tiled_framebuf_alloc(struct frame_buf *fb, int stdMode, int format, int strideY, int height, int mvCol, int mapType)
{
	int err, align;
	int divX, divY;
	Uint32 lum_top_base, lum_bot_base, chr_top_base, chr_bot_base;
	Uint32 lum_top_20bits, lum_bot_20bits, chr_top_20bits, chr_bot_20bits;
	int luma_top_size, luma_bot_size, chroma_top_size, chroma_bot_size;

	if (fb == NULL)
		return NULL;

	divX = (format == MODE420 || format == MODE422) ? 2 : 1;
	divY = (format == MODE420 || format == MODE224) ? 2 : 1;

	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));

	/*
	 * The buffers is luma top, chroma top, luma bottom and chroma bottom for
	 * tiled map type, and only 20bits for the address description, so we need
	 * to do 4K page align for each buffer.
	 */
	align = SZ_4K;
	if (mapType == TILED_FRAME_MB_RASTER_MAP) {
		/* luma_top_size means the Y size of one frame, chroma_top_size
		 * means the interleaved UV size of one frame in frame tiled map type*/
		luma_top_size = (strideY * height + align - 1) & ~(align - 1);
		chroma_top_size = (strideY / divX * height / divY * 2 + align - 1) & ~(align - 1);
		luma_bot_size = chroma_bot_size = 0;
	} else {
		/* This is FIELD_FRAME_MB_RASTER_MAP case, there are two fields */
		luma_top_size = (strideY * height / 2 + align - 1) & ~(align - 1);
		luma_bot_size = luma_top_size;
		chroma_top_size = (strideY / divX * height / divY + align - 1) & ~(align - 1);
		chroma_bot_size = chroma_top_size;
	}
	fb->desc.size = luma_top_size + chroma_top_size + luma_bot_size + chroma_bot_size;
	/* There is possible fb->desc.phy_addr in IOGetPhyMem not 4K page align,
	 * so add more SZ_4K byte here for alignment */
	fb->desc.size += align - 1;

	if (mvCol)
		fb->desc.size += strideY / divX * height / divY;

	err = IOGetPhyMem(&fb->desc);
	if (err) {
		printf("Frame buffer allocation failure\n");
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	if (IOGetVirtMem(&(fb->desc)) == -1) {
		IOFreePhyMem(&fb->desc);
		memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
		return NULL;
	}

	lum_top_base = (fb->desc.phy_addr + align - 1) & ~(align -1);
	chr_top_base = lum_top_base + luma_top_size;
	if (mapType == TILED_FRAME_MB_RASTER_MAP) {
		lum_bot_base = 0;
		chr_bot_base = 0;
	} else {
		lum_bot_base = chr_top_base + chroma_top_size;
		chr_bot_base = lum_bot_base + luma_bot_size;
	}

	lum_top_20bits = lum_top_base >> 12;
	lum_bot_20bits = lum_bot_base >> 12;
	chr_top_20bits = chr_top_base >> 12;
	chr_bot_20bits = chr_bot_base >> 12;

	/*
	 * In tiled map format the construction of the buffer pointers is as follows:
	 * 20bit = addrY [31:12]: lum_top_20bits
	 * 20bit = addrY [11: 0], addrCb[31:24]: chr_top_20bits
	 * 20bit = addrCb[23: 4]: lum_bot_20bits
	 * 20bit = addrCb[ 3: 0], addrCr[31:16]: chr_bot_20bits
	 */
	fb->addrY = (lum_top_20bits << 12) + (chr_top_20bits >> 8);
	fb->addrCb = (chr_top_20bits << 24) + (lum_bot_20bits << 4) + (chr_bot_20bits >> 16);
	fb->addrCr = chr_bot_20bits << 16;
	fb->strideY = strideY;
	fb->strideC = strideY / divX;
	if (mvCol) {
		if (mapType == TILED_FRAME_MB_RASTER_MAP) {
			fb->mvColBuf = chr_top_base + chroma_top_size;
		} else {
			fb->mvColBuf = chr_bot_base + chroma_bot_size;
		}
	}

	return fb;
}

void framebuf_free(struct frame_buf *fb)
{
	if (fb == NULL)
		return;

	if (fb->desc.virt_uaddr) {
		IOFreeVirtMem(&fb->desc);
	}

	if (fb->desc.phy_addr) {
		IOFreePhyMem(&fb->desc);
	}

	memset(&(fb->desc), 0, sizeof(vpu_mem_desc));
}

