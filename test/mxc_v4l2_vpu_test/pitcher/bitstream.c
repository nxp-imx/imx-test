/*
 * Copyright 2018-2022 NXP
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
#include <assert.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "platform_8x.h"
#include "parse.h"
#include "bitstream.h"

#define BS_DBG_LOG(...) //printf
#define BS_LOG(...) //printf

bitstream_buf *g_cur_bs;

int nal_remove_emul_bytes(uint8_t *psrc, uint8_t *pdst, int nalsize, int nal_header_sz, uint32_t emul_byte)
{
#define next_24_bits(p) ((p[0]<<16) | (p[1]<<8) | p[2])
	int i;
	int num_bytes_in_nal = nalsize;
	uint8_t *ps = psrc + nal_header_sz;         // skip nal header, h264:1; hevc:2
	int num_bytes_in_rbsp = 0;

	for (i = nal_header_sz; i < num_bytes_in_nal; i++) {
		if ((i + nal_header_sz < num_bytes_in_nal) && (next_24_bits(ps) == emul_byte)) {
			pdst[num_bytes_in_rbsp++] = ps[0];    //    b(8)
			pdst[num_bytes_in_rbsp++] = ps[1];    //    b(8)
			i += 2;
			//emulation_prevention_three_byte: equal to 0x03 // f(8)
			ps += 3;
			BS_LOG("remove one emulation byte '0x3'  !!!\n");
		} else {
			pdst[num_bytes_in_rbsp++] = ps[0];    //    b(8)
			ps += 1;
		}
	}
	return num_bytes_in_rbsp;
}

void bs_init(bitstream_buf *bs, uint8_t *buf, int size)
{
	bs->buf = buf;
	bs->byte_alloc = size;

	bs->buf_rdptr = 0;
	bs->num_held_bits = 0;
	bs->held_bits = 0;
	bs->num_bits_read = 0;

	g_cur_bs = bs;
}

void bs_read(bitstream_buf *pbs, uint32_t num_of_bits, uint32_t *pbits)
{
	uint32_t retval = 0;

	assert(num_of_bits <= 32);
	pbs->num_bits_read += num_of_bits;

	if (num_of_bits <= pbs->num_held_bits) {
		retval = pbs->held_bits >> (pbs->num_held_bits - num_of_bits);
		retval &= ~(0xff << num_of_bits);
		pbs->num_held_bits -= num_of_bits;
		*pbits = retval;
		BS_DBG_LOG("%d: ret: 0x%X\n", __LINE__, retval);
		return;
	}

	num_of_bits -= pbs->num_held_bits;
	retval = pbs->held_bits & ~(0xff << pbs->num_held_bits);
	retval <<= num_of_bits;
	BS_DBG_LOG("%d: ret: 0x%X,  num_of_bits: %d\n", __LINE__, retval, num_of_bits);

	uint32_t aligned_word = 0;
	uint32_t num_bytes_to_load = (num_of_bits - 1) >> 3;

	assert(pbs->buf_rdptr + num_bytes_to_load < pbs->byte_alloc);

	switch (num_bytes_to_load) {
	case 3:
		aligned_word  = pbs->buf[pbs->buf_rdptr++] << 24;
		BS_DBG_LOG("num_bytes_to_load: %d : aligned_word: 0x%X\n", num_bytes_to_load, aligned_word);
	case 2:
		aligned_word |= pbs->buf[pbs->buf_rdptr++] << 16;
		BS_DBG_LOG("num_bytes_to_load: %d : aligned_word: 0x%X\n", num_bytes_to_load, aligned_word);
	case 1:
		aligned_word |= pbs->buf[pbs->buf_rdptr++] <<  8;
		BS_DBG_LOG("num_bytes_to_load: %d : aligned_word: 0x%X\n", num_bytes_to_load, aligned_word);
	case 0:
		aligned_word |= pbs->buf[pbs->buf_rdptr++];
		BS_DBG_LOG("num_bytes_to_load: %d : aligned_word: 0x%X\n", num_bytes_to_load, aligned_word);
	}

	/* resolve remainder bits */
	uint32_t next_num_held_bits = (32 - num_of_bits) % 8;

	/* copy required part of aligned_word into retval */
	retval |= aligned_word >> next_num_held_bits;
	BS_DBG_LOG("%d: ret: 0x%X\n", __LINE__, retval);
	/* store held bits */
	pbs->num_held_bits = next_num_held_bits;
	pbs->held_bits = aligned_word;

	*pbits = retval;
	BS_DBG_LOG("%d: ret: 0x%X\n", __LINE__, retval);
}

void bs_read_scode(int *value, char *name, uint32_t length)
{
	uint32_t val;

	assert(length > 0 && length <= 32);

	bs_read(g_cur_bs, length, &val);
	//*value = length>=32 ? int(val) : ((-int(val & (uint32_t(1)<<(length-1)))) | int(val));
	*value = length >= 32 ? (int)(val) : ((-(int)(val & ((uint32_t)(1)<<(length-1)))) | (int)(val));
	BS_LOG("%s u(%d): code: 0x%X(%d)\n", name, length, *value, *value);
}

void bs_read_code(uint32_t *value, char *name, uint32_t length)
{
	assert(length > 0);
	bs_read(g_cur_bs, length, value);
	BS_LOG("%s u(%d): code: 0x%X(%d)\n", name, length, *value, *value);
}

void bs_read_uvlc(uint32_t *value, char *name)
{
	uint32_t val = 0;
	uint32_t code = 0;
	uint32_t length = 0;

	bs_read(g_cur_bs, 1, &code);

	if (code == 0) {
		length = 0;
		while (!(code & 1)) {
			bs_read(g_cur_bs, 1, &code);
			length++;
		}

		bs_read(g_cur_bs, length, &val);
		val += (1 << length)-1;
	}

	*value = val;
	BS_LOG("%s ue(%d): code: 0x%X(%d)\n", name, 1+2*length, val, val);
}

void bs_read_svlc(int *value, char *name)
{
	uint32_t bits = 0;
	uint32_t length = 0;

	bs_read(g_cur_bs, 1, &bits);

	if (bits == 0) {
		length = 0;
		while (!(bits & 1)) {
			bs_read(g_cur_bs, 1, &bits);
			length++;
		}

		bs_read(g_cur_bs, length, &bits);
		bits += (1 << length);
		*value = (bits & 1) ? -(int)(bits >> 1) : (int)(bits >> 1);
	} else {
		*value = 0;
	}
	BS_LOG("%s ue(%d): code: 0x%X(%d)\n", name, 1+2*length, *value, *value);
}

void bs_read_flag(uint32_t *value, char *name)
{
	bs_read(g_cur_bs, 1, value);
	BS_LOG("%s u(1): code: 0x%X(%d)\n", name, *value, *value);
}

uint32_t bs_consumed_bits(bitstream_buf *bs)
{
	BS_LOG("total consumed bits number: %d, bytes number: %d\n", bs->num_bits_read, (bs->num_bits_read + 7) >> 3);
	return bs->num_bits_read;
}
