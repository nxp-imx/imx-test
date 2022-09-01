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
#ifndef _INCLUDE_BITSTREAM_H
#define _INCLUDE_BITSTREAM_H
#ifdef __cplusplus
extern "C"
{
#endif

typedef struct {
	uint8_t *buf;
	uint32_t byte_alloc;
	uint32_t buf_rdptr; /// Read offset into buf
	uint32_t num_held_bits;
	uint8_t held_bits;
	uint32_t num_bits_read;
} bitstream_buf;

int nal_remove_emul_bytes(uint8_t *psrc, uint8_t *pdst, int nalsize, int nal_header_sz, uint32_t emul_byte);

void bs_init(bitstream_buf *bs, uint8_t *buf, int size);
void bs_read_scode(int *value, char *name, uint32_t length);
void bs_read_code(uint32_t *value, char *name, uint32_t length);
void bs_read_uvlc(uint32_t *value, char *name);
void bs_read_svlc(int *value, char *name);
void bs_read_flag(uint32_t *value, char *name);
uint32_t bs_consumed_bits(bitstream_buf *bs);

#define READ_SCODE(pval, name, length) bs_read_scode(pval, name, length)
#define READ_CODE(pval, name, length) bs_read_code(pval, name, length)
#define READ_UVLC(pval, name) bs_read_uvlc(pval, name)
#define READ_SVLC(pval, name) bs_read_svlc(pval, name)
#define READ_FLAG(pval, name) bs_read_flag(pval, name)

#ifdef __cplusplus
}
#endif
#endif
