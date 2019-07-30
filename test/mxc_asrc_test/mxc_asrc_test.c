/*
 * Copyright 2008-2014 Freescale Semiconductor, Inc. All rights reserved.
 * Copyright 2019 NXP
 *
 * SPDX-License-Identifier: BSD-3
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <sys/time.h>
#include <alsa/asoundlib.h>
#include <linux/mxc_asrc.h>

#define DMA_BUF_SIZE 4096

/*
 * From 38 kernel, asrc driver only supports one pair of buffer
 * convertion per time
 */

struct audio_info_s {
	int sample_rate;
	int channel;
	int input_data_len;
	int output_data_len;
	int input_used;
	int output_used;
	int output_sample_rate;
	int output_dma_size;
	unsigned short frame_bits;
	unsigned short blockalign;
	snd_pcm_format_t input_format;
	snd_pcm_format_t output_format;
	int input_dma_buf_size;
	int iec958;
	int pcm;
	int inclk;
	int outclk;
	int audioformat;
};
struct audio_buf {
	char *start;
	unsigned int index;
	unsigned int length;
	unsigned int max_len;
};

#define WAVE_HEAD_SIZE 44 + 14 + 16
static enum asrc_pair_index pair_index;
uint64_t supported_in_format;
uint64_t supported_out_format;

static char header[WAVE_HEAD_SIZE];

static int *input_buffer;
static int *input_null;

char *infile;
char *outfile;

static enum asrc_inclk inclk;
static enum asrc_outclk outclk;

unsigned int convert_flag;
int fd_asrc;

void *asrc_input_thread(void *info);
void *asrc_output_thread(void *info);

static const char * const in_clocks_name[] = {
"INCLK_NONE",			/* 0 */
"INCLK_ESAI_RX",		/* 1 */
"INCLK_SSI1_RX",		/* 2 */
"INCLK_SSI2_RX",		/* 3 */
"INCLK_SPDIF_RX",		/* 4 */
"INCLK_MLB_CLK",		/* 5 */
"INCLK_ESAI_TX",		/* 6 */
"INCLK_SSI1_TX",		/* 7 */
"INCLK_SSI2_TX",		/* 8 */
"INCLK_SPDIF_TX",		/* 9 */
"INCLK_ASRCK1_CLK",		/* 10 */
"INCLK_AUD_PLL_DIV_CLK0",	/* 11 */
"INCLK_AUD_PLL_DIV_CLK1",	/* 12 */
"INCLK_AUD_CLK0",		/* 13 */
"INCLK_AUD_CLK1",		/* 14 */
"INCLK_ESAI0_RX_CLK",		/* 15 */
"INCLK_ESAI0_TX_CLK",		/* 16 */
"INCLK_SPDIF0_RX",		/* 17 */
"INCLK_SPDIF1_RX",		/* 18 */
"INCLK_SAI0_RX_BCLK",		/* 19 */
"INCLK_SAI0_TX_BCLK",		/* 20 */
"INCLK_SAI1_RX_BCLK",		/* 21 */
"INCLK_SAI1_TX_BCLK",		/* 22 */
"INCLK_SAI2_RX_BCLK",		/* 23 */
"INCLK_SAI3_RX_BCLK",		/* 24 */
"INCLK_ASRC0_MUX_CLK",		/* 25 */
"INCLK_ESAI1_RX_CLK",		/* 26 */
"INCLK_ESAI1_TX_CLK",		/* 27 */
"INCLK_SAI6_TX_BCLK",		/* 28 */
"INCLK_HDMI_RX_SAI0_RX_BCLK",	/* 29 */
"INCLK_HDMI_TX_SAI0_TX_BCLK",	/* 30 */
};

static const char * const out_clocks_name[] = {
"OUTCLK_NONE",			/* 0 */
"OUTCLK_ESAI_TX",		/* 1 */
"OUTCLK_SSI1_TX",		/* 2 */
"OUTCLK_SSI2_TX",		/* 3 */
"OUTCLK_SPDIF_TX",		/* 4 */
"OUTCLK_MLB_CLK",		/* 5 */
"OUTCLK_ESAI_RX",		/* 6 */
"OUTCLK_SSI1_RX",		/* 7 */
"OUTCLK_SSI2_RX",		/* 8 */
"OUTCLK_SPDIF_RX",		/* 9 */
"OUTCLK_ASRCK1_CLK",		/* 10 */
"OUTCLK_AUD_PLL_DIV_CLK0",	/* 11 */
"OUTCLK_AUD_PLL_DIV_CLK1",	/* 12 */
"OUTCLK_AUD_CLK0",		/* 13 */
"OUTCLK_AUD_CLK1",		/* 14 */
"OUTCLK_ESAI0_RX_CLK",		/* 15 */
"OUTCLK_ESAI0_TX_CLK",		/* 16 */
"OUTCLK_SPDIF0_RX",		/* 17 */
"OUTCLK_SPDIF1_RX",		/* 18 */
"OUTCLK_SAI0_RX_BCLK",		/* 19 */
"OUTCLK_SAI0_TX_BCLK",		/* 20 */
"OUTCLK_SAI1_RX_BCLK",		/* 21 */
"OUTCLK_SAI1_TX_BCLK",		/* 22 */
"OUTCLK_SAI2_RX_BCLK",		/* 23 */
"OUTCLK_SAI3_RX_BCLK",		/* 24 */
"OUTCLK_ASRC0_MUX_CLK",		/* 25 */
"OUTCLK_ESAI1_RX_CLK",		/* 26 */
"OUTCLK_ESAI1_TX_CLK",		/* 27 */
"OUTCLK_SAI6_TX_BCLK",		/* 28 */
"OUTCLK_HDMI_RX_SAI0_RX_BCLK",	/* 29 */
"OUTCLK_HDMI_TX_SAI0_TX_BCLK",	/* 30 */
};

void help_info(int ac, const char *av[])
{
	int i;

	printf("\n\n**************************************************\n");
	printf("* Test aplication for ASRC\n");
	printf("* Options : \n\n");
	printf("-o <output sample rate>\n");
	printf("-x <origin.wav>\n");
	printf("-z <converted.wav>\n");
	printf("-e : enable iec958\n");
	printf("-m : pcm\n");
	printf("-f : input format\n");
	printf("-F : output format\n");
	printf("-r : input rate\n");
	printf("-c : channel\n");
	printf("-p <input clock>\n");
	printf("-q <output clock>\n");

	printf("<input clock source> <output clock source>\n");
	printf("input clock source types are:\n\n");

	for (i = 0; i <= 10; i++)
		printf("%d  --  %s\n", i, in_clocks_name[i]);
	printf("    --  in clocks for imx8 platform\n");
	for (i = 11; i <= 30; i++)
		printf("%d  --  %s\n", i, in_clocks_name[i]);

	printf("default option for output clock source is 0\n");
	printf("output clock source types are:\n\n");

	for (i = 0; i <= 10; i++)
		printf("%d  --  %s\n", i, out_clocks_name[i]);
	printf("    --  out clocks for imx8 platform\n");
	for (i = 11; i <= 30; i++)
		printf("%d  --  %s\n", i, out_clocks_name[i]);

	printf("default option for output clock source is 10\n");
	printf("**************************************************\n\n");
}

int parse_arguments(int argc, const char *argv[], struct audio_info_s *info)
{
	/* Usage checking  */
	if( argc < 3 )
	{
		help_info(argc, argv);
		exit(1);
	}

	int c, option_index;
	static const char short_options[] = "ho:x:z:ep:q:f:c:r:F:C:m";
	static const struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"outFreq", 1, 0, 'o'},
		{"inputFile", 1, 0, 'x'},
		{"outputFile", 1, 0, 'z'},
		{"iec958", 0, 0, 'e'},
		{"pcm", 0, 0, 'm'},
		{"channels", 1, 0, 'c'},
		{"iformat", 1, 0, 'f'},
		{"irate", 1, 0, 'r'},
		{"oformat", 1, 0, 'F'},
		{"inclk", 1, 0, 'p'},
		{"outclk", 1, 0, 'q'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, (char * const*)argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'o':
			info->output_sample_rate = strtol(optarg, NULL, 0);
			break;
		case 'x':
			infile = optarg;
			break;
		case 'z':
			outfile = optarg;
			break;
		case 'e':
			info->iec958 = 1;
			break;
		case 'm':
			info->pcm = 1;
			break;
		case 'f':
			info->input_format = snd_pcm_format_value(optarg);
			break;
		case 'F':
			info->output_format = snd_pcm_format_value(optarg);
			break;
		case 'r':
			info->sample_rate = strtol(optarg, NULL, 0);
			break;
		case 'c':
			info->channel = strtol(optarg, NULL, 0);
			break;
		case 'p':
			info->inclk = strtol(optarg, NULL, 0);
			break;
		case 'q':
			info->outclk = strtol(optarg, NULL, 0);
			break;
		case 'h':
			help_info(argc, argv);
			exit(1);
		default:
			printf("Unknown Command  -%c \n", c);
			exit(1);
		}
	}

	return 0;
}


int request_asrc_channel(int fd_asrc, struct audio_info_s *info)
{
	int err = 0;
	struct asrc_req req;

	req.chn_num = info->channel;
	if ((err = ioctl(fd_asrc, ASRC_REQ_PAIR, &req)) < 0) {
		printf("Req ASRC pair FAILED\n");
		return err;
	}
	if (req.index == 0)
		printf("Pair A requested\n");
	else if (req.index == 1)
		printf("Pair B requested\n");
	else if (req.index == 2)
		printf("Pair C requested\n");
	else if (req.index == 3)
		printf("Pair D requested\n");

	supported_in_format = req.supported_in_format;
	supported_out_format = req.supported_out_format;
	pair_index = req.index;

	return 0;
}

int configure_asrc_channel(int fd_asrc, struct audio_info_s *info)
{
	int err = 0;
	struct asrc_config config;

	config.pair = pair_index;
	config.channel_num = info->channel;
	config.dma_buffer_size = info->input_dma_buf_size;
	config.input_sample_rate = info->sample_rate;
	config.output_sample_rate = info->output_sample_rate;
	config.input_format = info->input_format;
	config.output_format = info->output_format;
	config.inclk = inclk;
	config.outclk = outclk;
	if ((err = ioctl(fd_asrc, ASRC_CONFIG_PAIR, &config)) < 0)
		return err;

	return 0;
}

int asrc_get_output_buffer_size(int input_buffer_size,
				int input_sample_rate, int output_sample_rate,
				snd_pcm_format_t input_format,
				snd_pcm_format_t output_format)
{
	int i = 0;
	int outbuffer_size = 0;
	int outsample = output_sample_rate;
	int in_word_size, out_word_size;
	while (outsample >= input_sample_rate) {
		++i;
		outsample -= input_sample_rate;
	}
	outbuffer_size = i * input_buffer_size;
	i = 1;
	while (((input_buffer_size >> i) > 2) && (outsample != 0)) {
		if (((outsample << 1) - input_sample_rate) >= 0) {
			outsample = (outsample << 1) - input_sample_rate;
			outbuffer_size += (input_buffer_size >> i);
		} else {
			outsample = outsample << 1;
		}
		i++;
	}
	outbuffer_size = (outbuffer_size >> 3) << 3;

	in_word_size = snd_pcm_format_physical_width(input_format) / 8;
	out_word_size = snd_pcm_format_physical_width(output_format) / 8;

	outbuffer_size = outbuffer_size * out_word_size / in_word_size;

	return outbuffer_size;
}

int play_file(FILE * fd_dst, int fd_asrc, struct audio_info_s *info)
{
	int err = 0;
	struct asrc_convert_buffer buf_info;
	char *input_p;
	char *output_p;
	int output_dma_size;
	unsigned int tail;

	input_p = (char *)input_buffer;
	output_dma_size =
	    asrc_get_output_buffer_size(info->input_dma_buf_size,
					info->sample_rate,
					info->output_sample_rate,
					info->input_format,
					info->output_format);
	tail = info->channel * 4 * 16;

	output_p = (char *)malloc(output_dma_size + tail);

	convert_flag = 1;
	memset(input_null, 0, info->input_dma_buf_size);
	if ((err = ioctl(fd_asrc, ASRC_START_CONV, &pair_index)) < 0)
		goto error;

	info->output_used = 0;
	while (convert_flag) {
		buf_info.input_buffer_length =
		    (info->input_data_len > info->input_dma_buf_size) ?
			info->input_dma_buf_size : info->input_data_len;

		if (info->input_data_len > 0) {
			buf_info.input_buffer_vaddr = input_p;
			input_p = input_p + buf_info.input_buffer_length;
			info->input_data_len -= buf_info.input_buffer_length;
			buf_info.input_buffer_length = info->input_dma_buf_size;
		} else {
			buf_info.input_buffer_vaddr = (void *)input_null;
			buf_info.input_buffer_length = info->input_dma_buf_size;
		}

		buf_info.output_buffer_length = output_dma_size + tail;
		buf_info.output_buffer_vaddr = output_p;
		if ((err = ioctl(fd_asrc, ASRC_CONVERT, &buf_info)) < 0)
			goto error;
		if (info->output_data_len > buf_info.output_buffer_length) {
			info->output_data_len -= buf_info.output_buffer_length;
			info->output_used += buf_info.output_buffer_length;
			fwrite(output_p, buf_info.output_buffer_length, 1, fd_dst);

		} else {
			info->output_used += info->output_data_len;
			fwrite(output_p, info->output_data_len, 1, fd_dst);
			info->output_data_len = 0;
		}
		if (info->output_data_len == 0)
			break;
	}
	err = ioctl(fd_asrc, ASRC_STOP_CONV, &pair_index);

	free(output_p);

error:
	return err;
}


int update_datachunk_length(struct audio_info_s *info, int format_size)
{
	*(int *)&header[24 + format_size] = info->output_data_len;
	*(int *)&header[4] = info->output_data_len + 20 + format_size;
	return 0;
}

int update_blockalign(struct audio_info_s *info)
{
	*(unsigned short *)&header[32] = info->blockalign;
	*(unsigned short *)&header[20] = info->audioformat;
	return 0;
}

int update_sample_bitdepth(struct audio_info_s *info)
{
	*(unsigned short *)&header[34] = info->frame_bits;
	*(int *)&header[28] =
		info->output_sample_rate * (info->frame_bits / 8)
					* info->channel;
	return 0;
}

void bitshift(FILE * src, struct audio_info_s *info)
{
	unsigned int data;
	unsigned int zero;
	int nleft;
	int format_size;
	int i = 0;
	int slotwidth = 8 * info->blockalign / info->channel;
	format_size = *(int *)&header[16];

	info->input_dma_buf_size = DMA_BUF_SIZE;

	switch (slotwidth) {
	case 8:
		nleft = info->input_data_len;
		info->input_dma_buf_size = DMA_BUF_SIZE / 2;
		break;
	case 16:
		nleft = (info->input_data_len >> 1);
		break;
	case 24:
		nleft = info->input_data_len / 3;
		break;
	case 32:
		nleft = (info->input_data_len >> 2);
		break;
	default:
		printf("wrong slot width\n");
		return;
	}

	/*allocate input buffer*/
	input_buffer = (int *)malloc(sizeof(int) * nleft +
				info->input_dma_buf_size);
	if (input_buffer == NULL)
		printf("allocate input buffer error\n");
	input_null = (int *)malloc(info->input_dma_buf_size);
	if (input_null == NULL)
		printf("allocate input null error\n");

	if (info->frame_bits == 8) {
		/*change data format*/
		if (supported_in_format & (1ULL << SND_PCM_FORMAT_S8)) {
			char * buf = (char *) input_buffer;
			unsigned char c;
			signed char d;

			do {
				fread(&c, 1, 1, src);
				d = (char)((int)c - 128);
				buf[i++] = d;
			} while (--nleft);

			/*change data length*/
			info->output_data_len = info->output_data_len << 1;
			update_datachunk_length(info, format_size);

			info->frame_bits = 16;
			info->blockalign = info->channel * 2;
			info->input_format = SND_PCM_FORMAT_S8;
			info->output_format = SND_PCM_FORMAT_S16_LE;
		} else {
			short * buf = (short *) input_buffer;
			unsigned char c;
			signed short d;

			do {
				fread(&c, 1, 1, src);
				d = (short)((int)c - 128);
				buf[i++] = d << 8;
			} while (--nleft);

			/*change data length*/
			info->output_data_len = info->output_data_len << 1;
			update_datachunk_length(info, format_size);

			info->frame_bits = 16;
			info->blockalign = info->channel * 2;
			info->input_format = SND_PCM_FORMAT_S16_LE;
			info->output_format = SND_PCM_FORMAT_S16_LE;
		}
	} else if (info->frame_bits == 16) {
		short * buf = (short *) input_buffer;
		/*change data format*/
		do {
			fread(&data, 2, 1, src);
			buf[i++] = (short)(data & 0xFFFF);
		} while (--nleft);

		info->frame_bits = 16;
		info->blockalign = info->channel * 2;
		info->input_format = SND_PCM_FORMAT_S16_LE;
		info->output_format = SND_PCM_FORMAT_S16_LE;

	} else if (info->frame_bits == 20 && (slotwidth == 24)) {
		/*change data format*/
		if (supported_in_format & (1ULL << SND_PCM_FORMAT_S20_3LE)) {
			char * buf = (char *) input_buffer;
			do {
				fread(&data, 3, 1, src);
				buf[i++] = (char)(data & 0xFF);
				buf[i++] = (char)((data >> 8) & 0xFF);
				buf[i++] = (char)((data >> 16) & 0xFF);
			} while (--nleft);
			/*change data length*/
			info->input_dma_buf_size = (DMA_BUF_SIZE / 3) * 3;
			info->input_data_len = info->input_data_len;
			info->output_data_len = info->output_data_len;
			update_datachunk_length(info, format_size);

			info->frame_bits = 24;
			info->blockalign = info->channel * 3;
			info->input_format = SND_PCM_FORMAT_S20_3LE;
			info->output_format = SND_PCM_FORMAT_S20_3LE;
		} else {
			do {
				fread(&data, 3, 1, src);
				zero = (data << 4 ) & 0xFFFFFF;
				input_buffer[i++] = zero;
			} while (--nleft);

			info->frame_bits = 24;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_S24_LE;
			info->output_format = SND_PCM_FORMAT_S24_LE;
		}

	} else if (info->frame_bits == 24 && (slotwidth == 24)) {
		if (supported_in_format & (1ULL << SND_PCM_FORMAT_S24_3LE)) {
			char * buf = (char *) input_buffer;
			do {
				fread(&data, 3, 1, src);
				buf[i++] = (char)(data & 0xFF);
				buf[i++] = (char)((data >> 8) & 0xFF);
				buf[i++] = (char)((data >> 16) & 0xFF);
			} while (--nleft);
			/*change data length*/
			info->input_dma_buf_size = (DMA_BUF_SIZE / 3) * 3;
			info->input_data_len = info->input_data_len;
			info->output_data_len = info->output_data_len;
			update_datachunk_length(info, format_size);

			info->frame_bits = 24;
			info->blockalign = info->channel * 3;
			info->input_format = SND_PCM_FORMAT_S24_3LE;
			info->output_format = SND_PCM_FORMAT_S24_3LE;
		} else {
			do {
				fread(&data, 3, 1, src);
				zero = (data & 0xFFFF00);
				input_buffer[i++] = zero;
			} while (--nleft);
			/*change data length*/
			info->input_data_len = info->input_data_len * 4 / 3;
			info->output_data_len = info->output_data_len * 4 / 3;
			update_datachunk_length(info, format_size);

			info->frame_bits = 24;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_S24_LE;
			info->output_format = SND_PCM_FORMAT_S24_LE;
		}
	} else if (info->frame_bits == 24 && (slotwidth == 32)) {
		/*change data format*/
		do {
			fread(&data, 4, 1, src);
			input_buffer[i++] = data;
		} while (--nleft);

		info->frame_bits = 24;
		info->blockalign = info->channel * 4;
		info->input_format = SND_PCM_FORMAT_S24_LE;
		info->output_format = SND_PCM_FORMAT_S24_LE;
	} else if (info->frame_bits == 32) {
		/*change data format*/
		if ((supported_in_format & (1ULL << SND_PCM_FORMAT_S32_LE)) &&
			(supported_out_format & (1ULL << SND_PCM_FORMAT_IEC958_SUBFRAME_LE)) &&
			info->iec958 && info->audioformat == 1) {
			do {
				fread(&data, 4, 1, src);
				input_buffer[i++] = data;
			} while (--nleft);

			info->frame_bits = 32;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_S32_LE;
			info->output_format = SND_PCM_FORMAT_IEC958_SUBFRAME_LE;
		} else if ((supported_in_format & (1ULL << SND_PCM_FORMAT_S32_LE)) &&
			(supported_out_format & (1ULL << SND_PCM_FORMAT_FLOAT_LE)) &&
			info->audioformat == 1) {
			do {
				fread(&data, 4, 1, src);
				input_buffer[i++] = data;
			} while (--nleft);

			info->frame_bits = 32;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_S32_LE;
			info->output_format = SND_PCM_FORMAT_FLOAT_LE;
			info->audioformat = 3;
		} else if ((supported_in_format & (1ULL << SND_PCM_FORMAT_FLOAT_LE)) &&
			(supported_out_format & (1ULL << SND_PCM_FORMAT_S32_LE)) &&
			info->audioformat == 3) {
			do {
				fread(&data, 4, 1, src);
				input_buffer[i++] = data;
			} while (--nleft);

			info->frame_bits = 32;
			info->audioformat = 1;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_FLOAT_LE;
			info->output_format = SND_PCM_FORMAT_S32_LE;

		} else {
			do {
				fread(&data, 4, 1, src);
				/*change data bit from 32bit to 24bit*/
				zero = (data >> 8) & 0x00FFFFFF;
				input_buffer[i++] = zero;
			} while (--nleft);

			info->frame_bits = 24;
			info->blockalign = info->channel * 4;
			info->input_format = SND_PCM_FORMAT_S24_LE;
			info->output_format = SND_PCM_FORMAT_S24_LE;
		}
	}

	/*change block align*/
	update_blockalign(info);

	update_sample_bitdepth(info);
}

int read_file_length(FILE *src, struct audio_info_s *info) {

	fseek(src, 0, SEEK_END);
	info->input_data_len = ftell(src);

	fseek(src, 0, SEEK_SET);

	info->output_data_len =
	    asrc_get_output_buffer_size(info->input_data_len,
					info->sample_rate,
					info->output_sample_rate,
					SND_PCM_FORMAT_S16_LE,
					SND_PCM_FORMAT_S16_LE);
	return 0;
}

int header_parser(FILE * src, struct audio_info_s *info)
{

	int format_size;
	char chunk_id[4];
	int chunk_size;

	/* check the "RIFF" chunk */
	fseek(src, 0, SEEK_SET);
	fread(chunk_id, 4, 1, src);
	while (strncmp(chunk_id, "RIFF", 4) != 0){
		fread(&chunk_size, 4, 1, src);
		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) == 0) {
			printf("Wrong wave file format \n");
			return -1;
		}
	}
	fseek(src, -4, SEEK_CUR);
	fread(&header[0], 1, 12, src);

	/* check the "fmt " chunk */
	fread(chunk_id, 4, 1, src);
	while (strncmp(chunk_id, "fmt ", 4) != 0){
		fread(&chunk_size, 4, 1, src);
		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) == 0) {
			printf("Wrong wave file format \n");
			return -1;
		}
	}
	/* fmt chunk size */
	fread(&format_size, 4, 1, src);

	fseek(src, -8, SEEK_CUR);
	fread(&header[12], 1, format_size + 8, src);

	/* AudioFormat(2) */
	info->audioformat = *(short *)&header[12 + 8];

	/* NumChannel(2) */
	info->channel = *(short *)&header[12 + 8 + 2];

	/* SampleRate(4) */
	info->sample_rate = *(int *)&header[12 + 8 + 2 + 2];

	/* ByteRate(4) */

	/* BlockAlign(2) */
	info->blockalign = *(short *)&header[12 + 8 + 2 + 2 + 4 + 4];

	/* BitsPerSample(2) */
	info->frame_bits = *(short *)&header[12 + 8 + 2 + 2 + 4 + 4 + 2];


	/* check the "data" chunk */
	fread(chunk_id, 4, 1, src);
	while (strncmp(chunk_id, "data", 4) != 0) {
		fread(&chunk_size, 4, 1, src);
		/* seek to next chunk if it is not "data" chunk */
		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) == 0) {
		    printf("No data chunk found \nWrong wave file format \n");
		    return -1;
		}
	}
	/* wave data length */
	fread(&info->input_data_len, 4, 1, src);
	fseek(src, -8, SEEK_CUR);
	fread(&header[format_size + 20], 1, 8, src);

	info->output_data_len =
	    asrc_get_output_buffer_size(info->input_data_len,
					info->sample_rate,
					info->output_sample_rate,
					SND_PCM_FORMAT_S16_LE,
					SND_PCM_FORMAT_S16_LE);

	*(int *)&header[24 + format_size] = info->output_data_len;

	*(int *)&header[4] = info->output_data_len + 20 + format_size;

	*(int *)&header[24] = info->output_sample_rate;

	*(int *)&header[28] =
	    info->output_sample_rate * info->channel * (info->frame_bits / 8);

	return 1;

}

void header_update(FILE * dst, struct audio_info_s *info)
{
	int format_size;

	format_size = *(int *)&header[16];

	*(int *)&header[24 + format_size] = info->output_used;
	*(int *)&header[4] = info->output_used + 20 + format_size;


	fseek(dst, 4,  SEEK_SET);
	fwrite(&header[4], 4, 1, dst);

	fseek(dst, 24 + format_size,  SEEK_SET);
	fwrite(&header[24 + format_size], 4, 1, dst);

	fseek(dst, 0, SEEK_END);
}

void header_write(FILE * dst)
{
	int format_size;
	int i = 0;

	format_size = *(int *)&header[16];

	while (i < (format_size + 28)) {
		fwrite(&header[i], 1, 1, dst);
		i++;
	}
}

void header_default(struct audio_info_s *info)
{
	memcpy(&header[0], "RIFF", 4);
	*(int *)&header[4] = 0;
	memcpy(&header[8], "WAVE", 4);
	memcpy(&header[12], "fmt ", 4);
	*(int *)&header[16] = 16;
	*(short *)&header[22] = info->channel;
	*(int *)&header[24] = info->output_sample_rate;
	memcpy(&header[36], "data", 4);
}

int main(int ac, const char *av[])
{
	FILE *fd_dst = NULL;
	FILE *fd_src = NULL;
	struct audio_info_s audio_info;
	int i = 0, err = 0;

	convert_flag = 0;
	pair_index = ASRC_INVALID_PAIR;

	memset(&audio_info, 0, sizeof(struct audio_info_s));

	audio_info.inclk = 0;
	audio_info.outclk = 10;
	if (parse_arguments(ac, av, &audio_info) != 0 )
		return -1;

	printf("\n---- Running < %s > test ----\n\n", av[0]);

	fd_asrc = open("/dev/mxc_asrc", O_RDWR);
	if (fd_asrc < 0)
		printf("Unable to open device\n");

	switch (audio_info.inclk) {
	case 0:
	    inclk = INCLK_NONE;
	    printf("inclk : INCLK_NONE\n");
	    break;
	case 1:
	    inclk = INCLK_ESAI_RX;
	    printf("inclk : INCLK_ESAI_RX\n");
	    break;
	case 2:
	    inclk = INCLK_SSI1_RX;
	    printf("inclk : INCLK_SSI1_RX\n");
	    break;
	case 3:
	    inclk = INCLK_SSI2_RX;
	    printf("inclk : INCLK_SSI2_RX\n");
	    break;
	case 4:
	    inclk = INCLK_SPDIF_RX;
	    printf("inclk : INCLK_SPDIF_RX\n");
	    break;
	case 5:
	    inclk = INCLK_MLB_CLK;
	    printf("inclk : INCLK_MLB_CLK\n");
	    break;
	case 6:
	    inclk = INCLK_ESAI_TX;
	    printf("inclk : INCLK_ESAI_TX\n");
	    break;
	case 7:
	    inclk = INCLK_SSI1_TX;
	    printf("inclk : INCLK_SSI1_TX\n");
	    break;
	case 8:
	    inclk = INCLK_SSI2_TX;
	    printf("inclk : INCLK_SSI2_TX\n");
	    break;
	case 9:
	    inclk = INCLK_SPDIF_TX;
	    printf("inclk : INCLK_SPDIF_TX\n");
	    break;
	case 10:
	    inclk = INCLK_ASRCK1_CLK;
	    printf("inclk : INCLK_ASRCK1_CLK\n");
	    break;
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
	case 24:
	case 25:
	    inclk = i + 5;
	    printf("inclk : %s\n", in_clocks_name[i]);
	    break;
	case 26:
	case 27:
	case 28:
	case 29:
	case 30:
	    inclk = i + 6;
	    printf("inclk : %s\n", in_clocks_name[i]);
	    break;
	default:
	    printf("Incorrect clock source\n");
	    return 1;
	}

	switch (audio_info.outclk) {
	case 0:
	    outclk = OUTCLK_NONE;
	    printf("outclk : OUTCLK_NONE\n");
	    break;
	case 1:
	    outclk = OUTCLK_ESAI_TX;
	    printf("outclk : OUTCLK_ESAI_TX\n");
	    break;
	case 2:
	    outclk = OUTCLK_SSI1_TX;
	    printf("outclk : OUTCLK_SSI1_TX\n");
	    break;
	case 3:
	    outclk = OUTCLK_SSI2_TX;
	    printf("outclk : OUTCLK_SSI2_TX\n");
	    break;
	case 4:
	    outclk = OUTCLK_SPDIF_TX;
	    printf("outclk : OUTCLK_SPDIF_TX\n");
	    break;
	case 5:
	    outclk = OUTCLK_MLB_CLK;
	    printf("outclk : OUTCLK_MLB_CLK\n");
	    break;
	case 6:
	    outclk = OUTCLK_ESAI_RX;
	    printf("outclk : OUTCLK_ESAI_RX\n");
	    break;
	case 7:
	    outclk = OUTCLK_SSI1_RX;
	    printf("outclk : OUTCLK_SSI1_RX\n");
	    break;
	case 8:
	    outclk = OUTCLK_SSI2_RX;
	    printf("outclk : OUTCLK_SSI2_RX\n");
	    break;
	case 9:
	    outclk = OUTCLK_SPDIF_RX;
	    printf("outclk : OUTCLK_SPDIF_RX\n");
	    break;
	case 10:
	    outclk = OUTCLK_ASRCK1_CLK;
	    printf("outclk : OUTCLK_ASRCK1_CLK\n");
	    break;
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
	case 17:
	case 18:
	case 19:
	case 20:
	case 21:
	case 22:
	case 23:
	case 24:
	case 25:
	    outclk = i + 5;
	    printf("outclk : %s\n", out_clocks_name[i]);
	    break;
	case 26:
	case 27:
	case 28:
	case 29:
	case 30:
	    outclk = i + 6;
	    printf("outclk : %s\n", out_clocks_name[i]);
	    break;
	default:
	    printf("Incorrect clock source\n");
	    return 1;
	}

	if ((fd_dst = fopen(outfile, "wb+")) <= 0) {
		goto err_dst_not_found;
	}

	if ((fd_src = fopen(infile, "r")) <= 0) {
		goto err_src_not_found;
	}

	if (audio_info.pcm) {
		read_file_length(fd_src, &audio_info);
		audio_info.frame_bits = snd_pcm_format_width(audio_info.input_format);
		audio_info.blockalign = audio_info.channel * snd_pcm_format_physical_width(audio_info.input_format) / 8;

		if (audio_info.input_format == SND_PCM_FORMAT_FLOAT_LE)
			audio_info.audioformat = 3;
		else
			audio_info.audioformat = 1;

		header_default(&audio_info);
	} else {
		if ((header_parser(fd_src, &audio_info)) <= 0) {
			goto end_head_parse;
		}
	}

	err = request_asrc_channel(fd_asrc, &audio_info);
	if (err < 0)
		goto end_err;

	bitshift(fd_src, &audio_info);

	err = configure_asrc_channel(fd_asrc, &audio_info);
	if (err < 0)
		goto end_err;

	header_write(fd_dst);

	/* Config HW */
	err += play_file(fd_dst, fd_asrc, &audio_info);
	if (err < 0)
		goto end_err;

	header_update(fd_dst, &audio_info);

	fclose(fd_src);
	fclose(fd_dst);
	close(fd_asrc);

	free(input_null);
	free(input_buffer);
	printf("All tests passed with success\n");
	return 0;

end_err:
	free(input_null);
	free(input_buffer);
end_head_parse:
	fclose(fd_src);
err_src_not_found:
	fclose(fd_dst);
err_dst_not_found:
	ioctl(fd_asrc, ASRC_RELEASE_PAIR, &pair_index);
	close(fd_asrc);
	return err;
}
