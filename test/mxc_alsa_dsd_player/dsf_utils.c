/*
 * Copyright 2018 NXP
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "read_utils.h"

struct dsd_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='DSD ' */
	uint64_t chunk_size;     /* 8 bytes, chunk size, =28 */
	uint64_t file_size;      /* 8 bytes, file size */
	uint64_t metadata_ptr;   /* 8 bytes, pointer to metadata */
};

struct fmt_chunk {
	uint8_t chunk_header[4];    /* 4 bytes, ='fmt ' */
	uint64_t chunk_size;        /* 8 bytes, chunk size, =52 */
	uint32_t format_version;    /* 4 bytes, =1 */
	uint32_t format_id;         /* 4 bytes, =0:DSD raw */
	uint32_t channel_type;      /* 4 bytes, */
	uint32_t channel_num;       /* 4 bytes, */
	uint32_t sampling_freq;     /* 4 bytes, */
	uint32_t bits_per_sample;   /* 4 bytes, */
	uint64_t sample_count;      /* 8 bytes, */
	uint32_t block_size_per_ch; /* 4 bytes, =4096 */
	uint32_t reserved;          /* 4 bytes, =0 */
};

struct data_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='data' */
	uint64_t chunk_size;     /* 8 bytes, chunk size, =n+12 */
};

struct dsf_file_header {
	struct dsd_chunk dsd;
	struct fmt_chunk fmt;
	struct data_chunk data;
};

#define BE 0

int read_dsf_file(int fd, struct dsd_params *params)
{
	int err;
	struct dsf_file_header header;

	err = read(fd, header.dsd.chunk_header, sizeof(header.dsd.chunk_header));
	if (err < 0) return err;
	err = read_u64_t(fd, &header.dsd.chunk_size, BE);
	if (err < 0) return err;
	err = read_u64_t(fd, &header.dsd.file_size, BE);
	if (err < 0) return err;
	err = read_u64_t(fd, &header.dsd.metadata_ptr, BE);
	if (err < 0) return err;
	err = read(fd, header.fmt.chunk_header, sizeof(header.fmt.chunk_header));
	if (err < 0) return err;
	err = read_u64_t(fd, &header.fmt.chunk_size, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.format_version, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.format_id, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.channel_type, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.channel_num, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.sampling_freq, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.bits_per_sample, BE);
	if (err < 0) return err;
	err = read_u64_t(fd, &header.fmt.sample_count, BE);
	if (err < 0) return err;
	err = read_u32_t(fd, &header.fmt.block_size_per_ch, BE);
	if (err < 0) return err;

	if (header.fmt.block_size_per_ch != DSF_BLOCK_SIZE)
		return -1;

	err = read_u32_t(fd, &header.fmt.reserved, BE);
	if (err < 0) return err;

	err = read(fd, header.data.chunk_header, sizeof(header.data.chunk_header));
	if (err < 0) return err;
	err = read_u64_t(fd, &header.data.chunk_size, BE);
	if (err < 0) return err;

	printf("DSD chunk header: [%.4s]\n", header.dsd.chunk_header);
	printf("DSD chunk size:   [%lu][%016lX]\n", header.dsd.chunk_size, header.dsd.chunk_size);
	printf("DSD file size:    [%lu][%016lX]\n", header.dsd.file_size, header.dsd.file_size);
	printf("DSD metadata ptr: [%lu][%016lX]\n", header.dsd.metadata_ptr, header.dsd.metadata_ptr);
	printf("FMT chunk header: [%.4s]\n", header.fmt.chunk_header);
	printf("FMT chunk size:   [%lu]\n",  header.fmt.chunk_size);
	printf("FMT format ver:   [%u]\n",   header.fmt.format_version);
	printf("FMT format id:    [%u]\n",   header.fmt.format_id);
	printf("FMT channel type: [%u]\n",   header.fmt.channel_type);
	printf("FMT channel num:  [%u]\n",   header.fmt.channel_num);
	printf("FMT sampling freq:[%u]\n",   header.fmt.sampling_freq);
	printf("FMT bits/sample:  [%u]\n",   header.fmt.bits_per_sample);
	printf("FMT sample count: [%lu]\n",  header.fmt.sample_count);
	printf("FMT blk size/ch:  [%u]\n",   header.fmt.block_size_per_ch);
	printf("FMT reserved:     [%u]\n",   header.fmt.reserved);
	printf("DAT chunk header: [%.4s]\n", header.data.chunk_header);
	printf("DAT chunk size:   [%lu]\n",  header.data.chunk_size);

	params->sampling_freq   = header.fmt.sampling_freq;
	params->bits_per_sample = header.fmt.bits_per_sample;
	params->channel_num     = header.fmt.channel_num;
	params->dsd_chunk_size  = header.data.chunk_size;

	return 0;
}

void interleaveDsfBlock(uint8_t *dest, const uint8_t *src, unsigned channels, unsigned format)
{
	unsigned i, c;
	uint8_t *d;

	switch (channels) {
	case 1:
		memcpy(dest, src, DSF_BLOCK_SIZE);
		break;
	case 2:
		if (format == SND_PCM_FORMAT_DSD_U32_LE) {
			for (i = 0; i < DSF_BLOCK_SIZE/4; i=i+1) {
				dest[8*i]   = src[4*i ];
				dest[8*i + 1]   = src[4*i + 1];
				dest[8*i + 2]   = src[4*i + 2];
				dest[8*i + 3]   = src[4*i + 3];

				dest[8*i+4] = src[DSF_BLOCK_SIZE + 4*i];
				dest[8*i+5] = src[DSF_BLOCK_SIZE + 4*i+1];
				dest[8*i+6] = src[DSF_BLOCK_SIZE + 4*i+2];
				dest[8*i+7] = src[DSF_BLOCK_SIZE + 4*i+3];
			}

		}

		if (format == SND_PCM_FORMAT_DSD_U16_LE) {
			for (i = 0; i < DSF_BLOCK_SIZE/2; i=i+1) {
				dest[4*i]       = src[2*i ];
				dest[4*i + 1]   = src[2*i + 1];

				dest[4*i + 2]   = src[DSF_BLOCK_SIZE + 2*i];
				dest[4*i + 3]   = src[DSF_BLOCK_SIZE + 2*i+1];
			}
		}

		if (format == SND_PCM_FORMAT_DSD_U8) {
			for (i = 0; i < DSF_BLOCK_SIZE; i=i+1) {
				dest[2*i]     = src[i ];
				dest[2*i + 1] = src[DSF_BLOCK_SIZE + i];
			}

		}
		break;
	default:
		for (c = 0; c < channels; c++, dest++, src += DSF_BLOCK_SIZE) {
			for (i = 0, d = dest; i < DSF_BLOCK_SIZE; i++, d += channels, src++)
				*d = *src;
		}
		break;
	}
}


