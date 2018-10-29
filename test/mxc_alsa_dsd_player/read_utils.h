#ifndef READ_UTILS_H_
#define READ_UTILS_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <alsa/asoundlib.h>

#define DSF_BLOCK_SIZE		4096
#define DSF_MAX_CHANNELS	6

struct dsd_params {
	uint32_t sampling_freq;
	uint32_t bits_per_sample;
	uint32_t channel_num;
	uint64_t dsd_chunk_size;
};

int read_u16_t(int fd, uint16_t *val, int be);
int read_u32_t(int fd, uint32_t *val, int be);
int read_u64_t(int fd, uint64_t *val, int be);
size_t read_full(int fd, void *_buffer, size_t size);

int read_dsf_file(int fd, struct dsd_params *params);
int read_dff_file(int fd, struct dsd_params *params);

void interleaveDsfBlock(uint8_t *dest, const uint8_t *src, unsigned channels, unsigned format);
void interleaveDffBlock(uint8_t *dest, const uint8_t *src, unsigned channels, unsigned format);

#endif
