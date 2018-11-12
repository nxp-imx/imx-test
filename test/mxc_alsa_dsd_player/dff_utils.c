/*
 * Copyright 2018 NXP
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "read_utils.h"

struct dff_format_version_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='FVER' */
	uint64_t chunk_size;     /* 8 bytes, */
	uint32_t version;        /* 4 bytes, = 0x01050000 version 1.5.0.0 DSDIFF */
};

struct dff_sample_rate_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='FS  ' */
	uint64_t chunk_size;     /* 8 bytes, = 4 */
	uint32_t sample_rate;    /* 4 bytes, = in Hz */
};

struct dff_channels_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='CHNL' */
	uint64_t chunk_size;     /* 8 bytes, = 4 */
	uint16_t num_channels;   /* 2 bytes, = */
};

struct dff_compression_type_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='CMPR' */
	uint64_t chunk_size;     /* 8 bytes, = 4 */
	uint8_t compr_type[4];   /* 4 bytes, ='DSD ' */
	uint8_t count;
	uint8_t compr_name[256]; /* compression name, length limited by count size */
};

struct dff_property_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='PROP' */
	uint64_t chunk_size;     /* 8 bytes, */
	uint8_t prop_type[4];    /* 4 bytes, ='SND ' */

	struct dff_sample_rate_chunk fs;
	struct dff_channels_chunk chnl;
	struct dff_compression_type_chunk compr;
};

struct dff_dsd_data_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='DSD ' */
	uint64_t chunk_size;     /* 8 bytes, = 4 */
};

struct dff_form_dsd_chunk {
	uint8_t chunk_header[4]; /* 4 bytes, ='FRM8' */
	uint64_t chunk_size;     /* 8 bytes, */
	uint8_t form_type[4];    /* 4 bytes, ='DSD ' */

	struct dff_format_version_chunk version;
	struct dff_property_chunk prop;
	struct dff_dsd_data_chunk dsd_data;
};

#define BE 1

static int read_dff_dsd_data(int fd, struct dff_dsd_data_chunk *dsd_data)
{
	return read_u64_t(fd, &dsd_data->chunk_size, BE);
}

static int read_dff_prop(int fd, struct dff_property_chunk *prop)
{
	int err;
	uint8_t next_chunk[4];
	uint64_t next_chunk_size;
	int fs = 0, cc = 0, ctc = 0;

	err = read_u64_t(fd, &prop->chunk_size, BE);
	if (err < 0) return err;

	err = read(fd, prop->prop_type, sizeof(prop->prop_type));
	if (err < 0) return err;
	if (memcmp(prop->prop_type, "SND ", 4) != 0)
		return -1;

	while (!fs || !cc || !ctc) {
		err = read(fd, next_chunk, sizeof(next_chunk));
		if (err < 0) return err;

		if (!fs && !memcmp(next_chunk, "FS  ", 4)) {
			struct dff_sample_rate_chunk *fsr = &prop->fs;

			memcpy(fsr->chunk_header, next_chunk, 4);
			err = read_u64_t(fd, &fsr->chunk_size, BE);
			if (err < 0) return err;
			err = read_u32_t(fd, &fsr->sample_rate, BE);
			if (err < 0) return err;

			fs = 1;
		} else if (!cc && !memcmp(next_chunk, "CHNL", 4)) {
			struct dff_channels_chunk *chnl = &prop->chnl;

			memcpy(chnl->chunk_header, next_chunk, 4);
			err = read_u64_t(fd, &chnl->chunk_size, BE);
			if (err < 0) return err;
			err = read_u16_t(fd, &chnl->num_channels, BE);
			if (err < 0) return err;

			/* skip num_channels*4 bytes */
			uint8_t buff[4*chnl->num_channels];
			err = read_full(fd, buff, 4*chnl->num_channels);
			if (err < 0) return err;

			cc = 1;
		} else if (!ctc && !memcmp(next_chunk, "CMPR", 4)) {
			struct dff_compression_type_chunk *compr = &prop->compr;

			memcpy(compr->chunk_header, next_chunk, 4);
			err = read_u64_t(fd, &compr->chunk_size, BE);
			if (err < 0) return err;
			err = read(fd, compr->compr_type, sizeof(compr->compr_type));
			if (err < 0) return err;
			if (memcmp(compr->compr_type, "DSD ", 4) != 0)
				return -1;
			err = read(fd, &compr->count, sizeof(compr->count));
			if (err < 0) return err;
			err = read(fd, compr->compr_name, compr->chunk_size - 5);
			if (err < 0) return err;

			ctc = 1;
		} else { /* unknown chunk, skip it*/
			err = read_u64_t(fd, &next_chunk_size, BE);
			if (err < 0) return err;

			uint8_t buff[next_chunk_size];
			err = read_full(fd, buff, next_chunk_size);
			if (err < 0) return err;
		}
	}

	return 0;
}

static int read_dff_fver(int fd, struct dff_format_version_chunk *version)
{
	int err;

	err = read_u64_t(fd, &version->chunk_size, BE);
	if (err < 0) return err;

	return read_u32_t(fd, &version->version, BE);
}

static int read_dff_form(int fd, struct dff_form_dsd_chunk *form)
{
	int err;
	uint8_t next_chunk[4];
	uint64_t next_chunk_size;
	int v = 0, p = 0, d = 0;

	err = read(fd, form->chunk_header, sizeof(form->chunk_header));
	if (err < 0) return err;
	if (memcmp(form->chunk_header, "FRM8", 4) != 0)
		return -1;

	err = read_u64_t(fd, &form->chunk_size, BE);
	if (err < 0) return err;

	err = read(fd, form->form_type, sizeof(form->form_type));
	if (err < 0) return err;
	if (memcmp(form->form_type, "DSD ", 4) != 0)
		return -1;

	while (!v || !p || !d) {
		err = read(fd, next_chunk, sizeof(next_chunk));
		if (err < 0) return err;

		if (!v && !p && !d && !memcmp(next_chunk, "FVER", 4)) {
			struct dff_format_version_chunk *version = &form->version;

			memcpy(version->chunk_header, next_chunk, 4);
			err = read_dff_fver(fd, version);
			if (err < 0)
				return err;
			v = 1;
		} else if (v && !p && !d && !memcmp(next_chunk, "PROP", 4)) {
			struct dff_property_chunk *prop = &form->prop;

			memcpy(prop->chunk_header, next_chunk, 4);
			err = read_dff_prop(fd, prop);
			if (err < 0)
				return err;
			p = 1;
		} else if (v && p && !d && !memcmp(next_chunk, "DSD ", 4)) {
			struct dff_dsd_data_chunk *dsd_data = &form->dsd_data;

			memcpy(dsd_data->chunk_header, next_chunk, 4);
			err = read_dff_dsd_data(fd, dsd_data);
			if (err < 0)
				return err;
			d = 1;
		} else { /* unknown chunk, skip it*/
			err = read_u64_t(fd, &next_chunk_size, BE);
			if (err < 0) return err;

			uint8_t buff[next_chunk_size];
			err = read_full(fd, buff, next_chunk_size);
			if (err < 0) return err;
		}
	}

	return 0;
}

int read_dff_file(int fd, struct dsd_params *params)
{
	int err;
	struct dff_form_dsd_chunk form;

	err = read_dff_form(fd, &form);
	if (err < 0)
		return err;

	params->sampling_freq   = form.prop.fs.sample_rate;
	params->bits_per_sample = 8;
	params->channel_num     = form.prop.chnl.num_channels;
	params->dsd_chunk_size	= form.dsd_data.chunk_size;

	return 0;
}

void interleaveDffBlock(uint8_t *dest, const uint8_t *src, unsigned channels, snd_pcm_format_t format)
{
	unsigned i, c;
	uint8_t *d;

	switch (channels) {
	case 1:
		memcpy(dest, src, DSF_BLOCK_SIZE);
		break;
	case 2:
		if (format == SND_PCM_FORMAT_DSD_U32_LE) {
			for (i = 0; i < DSF_BLOCK_SIZE/4; i++) {
				dest[8*i]   = src[8*i+0];
				dest[8*i+1] = src[8*i+2];
				dest[8*i+2] = src[8*i+4];
				dest[8*i+3] = src[8*i+6];
				dest[8*i+4] = src[8*i+1];
				dest[8*i+5] = src[8*i+3];
				dest[8*i+6] = src[8*i+5];
				dest[8*i+7] = src[8*i+7];
			}
		}

		if (format == SND_PCM_FORMAT_DSD_U16_LE) {
			for (i = 0; i < DSF_BLOCK_SIZE/2; i++) {
				dest[4*i]   = src[4*i+0];
				dest[4*i+1] = src[4*i+2];
				dest[4*i+2] = src[4*i+1];
				dest[4*i+3] = src[4*i+3];
			}
		}

		if (format == SND_PCM_FORMAT_DSD_U8) {
			for (i = 0; i < DSF_BLOCK_SIZE; i++) {
				dest[2*i]   = src[2*i];
				dest[2*i+1] = src[2*i+1];
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


