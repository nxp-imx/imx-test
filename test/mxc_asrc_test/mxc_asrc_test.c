/*
 * Copyright 2008-2014 Freescale Semiconductor, Inc. All rights reserved.
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
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
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
#include <linux/mxc_asrc.h>
#include <pthread.h>

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
	enum asrc_word_width input_word_width;
	enum asrc_word_width output_word_width;
	int input_dma_buf_size;
};
struct audio_buf {
	char *start;
	unsigned int index;
	unsigned int length;
	unsigned int max_len;
};

#define WAVE_HEAD_SIZE 44 + 14 + 16
static enum asrc_pair_index pair_index;
static char header[WAVE_HEAD_SIZE];

static int *input_buffer;
static int *input_null;

static enum asrc_inclk inclk;
static enum asrc_outclk outclk;

unsigned int convert_flag;
int fd_asrc;

void *asrc_input_thread(void *info);
void *asrc_output_thread(void *info);

void help_info(int ac, char *av[])
{
	printf("\n\n**************************************************\n");
	printf("* Test aplication for ASRC\n");
	printf("* Options : \n\n");
	printf("-to <output sample rate> <origin.wav> <converted.wav>\n");
	printf("<input clock source> <output clock source>\n");
	printf("input clock source types are:\n\n");
	printf("0  --  INCLK_NONE\n");
	printf("1  --  INCLK_ESAI_RX\n");
	printf("2  --  INCLK_SSI1_RX\n");
	printf("3  --  INCLK_SSI2_RX\n");
	printf("4  --  INCLK_SPDIF_RX\n");
	printf("5  --  INCLK_MLB_CLK\n");
	printf("6  --  INCLK_ESAI_TX\n");
	printf("7  --  INCLK_SSI1_TX\n");
	printf("8  --  INCLK_SSI2_TX\n");
	printf("9  --  INCLK_SPDIF_TX\n");
	printf("10 --  INCLK_ASRCK1_CLK\n");
	printf("default option for output clock source is 0\n");
	printf("output clock source types are:\n\n");
	printf("0  --  OUTCLK_NONE\n");
	printf("1  --  OUTCLK_ESAI_TX\n");
	printf("2  --  OUTCLK_SSI1_TX\n");
	printf("3  --  OUTCLK_SSI2_TX\n");
	printf("4  --  OUTCLK_SPDIF_TX\n");
	printf("5  --  OUTCLK_MLB_CLK\n");
	printf("6  --  OUTCLK_ESAI_RX\n");
	printf("7  --  OUTCLK_SSI1_RX\n");
	printf("8  --  OUTCLK_SSI2_RX\n");
	printf("9  --  OUTCLK_SPDIF_RX\n");
	printf("10 --  OUTCLK_ASRCK1_CLK\n");
	printf("default option for output clock source is 10\n");
	printf("**************************************************\n\n");
}

int configure_asrc_channel(int fd_asrc, struct audio_info_s *info)
{
	int err = 0;
	struct asrc_req req;
	struct asrc_config config;

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

	config.pair = req.index;
	config.channel_num = req.chn_num;
	config.dma_buffer_size = info->input_dma_buf_size;
	config.input_sample_rate = info->sample_rate;
	config.output_sample_rate = info->output_sample_rate;
	config.input_word_width = info->input_word_width;
	config.output_word_width = info->output_word_width;
	config.inclk = inclk;
	config.outclk = outclk;
	pair_index = req.index;
	if ((err = ioctl(fd_asrc, ASRC_CONFIG_PAIR, &config)) < 0)
		return err;

	return 0;
}

int asrc_get_output_buffer_size(int input_buffer_size,
				int input_sample_rate, int output_sample_rate,
				int input_word_width, int output_word_width)
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

	switch (input_word_width) {
	case ASRC_WIDTH_24_BIT:
		in_word_size = 4;
		break;
	case ASRC_WIDTH_16_BIT:
		in_word_size = 2;
		break;
	case ASRC_WIDTH_8_BIT:
		in_word_size = 1;
		break;
	default:
		in_word_size = 4;
		break;
	}

	switch (output_word_width) {
	case ASRC_WIDTH_24_BIT:
		out_word_size = 4;
		break;
	case ASRC_WIDTH_16_BIT:
		out_word_size = 2;
		break;
	case ASRC_WIDTH_8_BIT:
		out_word_size = 1;
		break;
	default:
		out_word_size = 4;
		break;
	}

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
					info->input_word_width,
					info->output_word_width);
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
		char * buf = (char *) input_buffer;
		unsigned char c;
		signed char d;
		/*change data format*/
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
		info->input_word_width = ASRC_WIDTH_8_BIT;
		info->output_word_width = ASRC_WIDTH_16_BIT;

	} else if (info->frame_bits == 16) {
		short * buf = (short *) input_buffer;
		/*change data format*/
		do {
			fread(&data, 2, 1, src);
			buf[i++] = (short)(data & 0xFFFF);
		} while (--nleft);

		info->frame_bits = 16;
		info->blockalign = info->channel * 2;
		info->input_word_width = ASRC_WIDTH_16_BIT;
		info->output_word_width = ASRC_WIDTH_16_BIT;

	} else if (info->frame_bits == 24 && (slotwidth == 24)) {
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
		info->input_word_width = ASRC_WIDTH_24_BIT;
		info->output_word_width = ASRC_WIDTH_24_BIT;

	} else if (info->frame_bits == 24 && (slotwidth == 32)) {
		/*change data format*/
		do {
			fread(&data, 4, 1, src);
			input_buffer[i++] = data;
		} while (--nleft);

		info->frame_bits = 24;
		info->blockalign = info->channel * 4;
		info->input_word_width = ASRC_WIDTH_24_BIT;
		info->output_word_width = ASRC_WIDTH_24_BIT;
	} else if (info->frame_bits == 32) {
		/*change data format*/
		do {
			fread(&data, 4, 1, src);
			/*change data bit from 32bit to 24bit*/
			zero = (data >> 8) & 0x00FFFFFF;
			input_buffer[i++] = zero;
		} while (--nleft);

		info->frame_bits = 24;
		info->blockalign = info->channel * 4;
		info->input_word_width = ASRC_WIDTH_24_BIT;
		info->output_word_width = ASRC_WIDTH_24_BIT;
	}

	/*change block align*/
	update_blockalign(info);

	update_sample_bitdepth(info);
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
					ASRC_WIDTH_16_BIT,
					ASRC_WIDTH_16_BIT);

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

int main(int ac, char *av[])
{
	FILE *fd_dst = NULL;
	FILE *fd_src = NULL;
	struct audio_info_s audio_info;
	int i = 0, err = 0;

	inclk = INCLK_NONE;
	outclk = OUTCLK_ASRCK1_CLK;
	convert_flag = 0;
	pair_index = ASRC_INVALID_PAIR;
	printf("\n---- Running < %s > test ----\n\n", av[0]);

	if (ac < 5) {
		help_info(ac, av);
		return 1;
	}

	memset(&audio_info, 0, sizeof(struct audio_info_s));
	fd_asrc = open("/dev/mxc_asrc", O_RDWR);
	if (fd_asrc < 0)
		printf("Unable to open device\n");

	for (i = 1; i < ac; i++) {
		if (strcmp(av[i], "-to") == 0)
			audio_info.output_sample_rate = atoi(av[++i]);
	}

	for (i = 1; i < ac; i++) {
		if (strcmp(av[i], "-wid") == 0) {
			if (atoi(av[++i]) != 24)
				printf("Only 24bit output is support!\n");
			audio_info.output_word_width = ASRC_WIDTH_24_BIT;
		}
	}

	if (ac > 5)
	{
		i = atoi(av[5]);
		switch (i)
		{
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
			default:
			    printf("Incorrect clock source\n");
			    return 1;
		}

		i = atoi(av[6]);
		switch (i)
		{
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
			default:
			    printf("Incorrect clock source\n");
			    return 1;
		}
	}

	if ((fd_dst = fopen(av[5 - 1], "wb+")) <= 0) {
		goto err_dst_not_found;
	}

	if ((fd_src = fopen(av[5 - 2], "r")) <= 0) {
		goto err_src_not_found;
	}

	if ((header_parser(fd_src, &audio_info)) <= 0) {
		goto end_err;
	}

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
	fclose(fd_src);
      err_src_not_found:
	fclose(fd_dst);
      err_dst_not_found:
	ioctl(fd_asrc, ASRC_RELEASE_PAIR, &pair_index);
	close(fd_asrc);
	return err;
}
