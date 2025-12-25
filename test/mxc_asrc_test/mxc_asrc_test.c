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
#include <sound/compress_offload.h>
#include "alsa_api.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define DMA_BUF_SIZE 4096

struct audio_info_s {
	int sample_rate;
	int channel;
	int input_data_len;
	int output_data_len;
	int input_used;
	int output_used;
	int output_sample_rate;
	int output_dma_size;
	unsigned short in_frame_bits;
	unsigned short in_blockalign;
	unsigned short out_frame_bits;
	unsigned short out_blockalign;
	unsigned short in_slotwidth;
	unsigned short out_slotwidth;
	snd_pcm_format_t input_format;
	snd_pcm_format_t output_format;
	int input_dma_buf_size;
	int pcm;
	int in_audioformat;
	int out_audioformat;
};

#define WAVE_HEAD_SIZE 44 + 14 + 16

static char header[WAVE_HEAD_SIZE];

static int *input_buffer;
int dev_id;
char *infile;
char *outfile;

unsigned int convert_flag;
int fd_asrc;

void help_info(int ac, const char *av[])
{
	printf("\n\n**************************************************\n");
	printf("* Test aplication for ASRC\n");
	printf("* Options : \n\n");
	printf("-d card id of /dev/snd/comprCxD0\n");
	printf("-o <output sample rate>\n");
	printf("-x <origin.wav>\n");
	printf("-z <converted.wav>\n");
	printf("-m : pcm\n");
	printf("-f : input format\n");
	printf("-F : output format\n");
	printf("-r : input rate\n");
	printf("-c : channel\n");
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
	static const char short_options[] = "hd:o:x:z:f:c:r:F:m";
	static const struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"device", 1, 0, 'd'},
		{"outFreq", 1, 0, 'o'},
		{"inputFile", 1, 0, 'x'},
		{"outputFile", 1, 0, 'z'},
		{"pcm", 0, 0, 'm'},
		{"channels", 1, 0, 'c'},
		{"iformat", 1, 0, 'f'},
		{"irate", 1, 0, 'r'},
		{"oformat", 1, 0, 'F'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, (char * const*)argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'd':
			dev_id = strtol(optarg, NULL, 0);
			break;
		case 'o':
			info->output_sample_rate = strtol(optarg, NULL, 0);
			break;
		case 'x':
			infile = optarg;
			break;
		case 'z':
			outfile = optarg;
			break;
		case 'm':
			info->pcm = 1;
			break;
		case 'f':
			info->input_format = alsa_snd_pcm_format_value(optarg);
			break;
		case 'F':
			info->output_format = alsa_snd_pcm_format_value(optarg);
			break;
		case 'r':
			info->sample_rate = strtol(optarg, NULL, 0);
			break;
		case 'c':
			info->channel = strtol(optarg, NULL, 0);
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

	in_word_size = alsa_snd_pcm_format_physical_width(input_format) / 8;
	out_word_size = alsa_snd_pcm_format_physical_width(output_format) / 8;

	outbuffer_size = outbuffer_size * out_word_size / in_word_size;

	return outbuffer_size;
}

int update_datachunk_length(struct audio_info_s *info, int format_size)
{
	*(int *)&header[24 + format_size] = info->output_data_len;
	*(int *)&header[4] = info->output_data_len + 20 + format_size;
	return 0;
}

int update_blockalign(struct audio_info_s *info)
{
	*(unsigned short *)&header[32] = info->out_blockalign;
	*(unsigned short *)&header[20] = info->out_audioformat;
	return 0;
}

int update_sample_bitdepth(struct audio_info_s *info)
{
	*(unsigned short *)&header[34] = info->out_frame_bits;
	*(int *)&header[28] =
		info->output_sample_rate * (info->out_frame_bits / 8)
					* info->channel;
	return 0;
}

int read_file_length(FILE *src, struct audio_info_s *info) {

	fseek(src, 0, SEEK_END);
	info->input_data_len = ftell(src);

	fseek(src, 0, SEEK_SET);

	return 0;
}

int header_parser(FILE * src, struct audio_info_s *info)
{

	int format_size;
	char chunk_id[4];
	int chunk_size;

	/* check the "RIFF" chunk */
	fseek(src, 0, SEEK_SET);
	if (fread(chunk_id, 4, 1, src) != 1)
		return -1;

	while (strncmp(chunk_id, "RIFF", 4) != 0){
		if (fread(&chunk_size, 4, 1, src) != 1)
			return -1;

		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) != 1) {
			printf("Wrong wave file format \n");
			return -1;
		}
	}
	fseek(src, -4, SEEK_CUR);
	if (fread(&header[0], 1, 12, src) != 12)
		return -1;

	/* check the "fmt " chunk */
	if (fread(chunk_id, 4, 1, src) != 1)
		return -1;

	while (strncmp(chunk_id, "fmt ", 4) != 0){
		if (fread(&chunk_size, 4, 1, src) != 1)
			return -1;

		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) != 1) {
			printf("Wrong wave file format \n");
			return -1;
		}
	}
	/* fmt chunk size */
	if (fread(&format_size, 4, 1, src) != 1)
		return -1;

	fseek(src, -8, SEEK_CUR);
	if (fread(&header[12], 1, format_size + 8, src) != (format_size + 8))
		return -1;

	/* AudioFormat(2) */
	info->in_audioformat = *(short *)&header[12 + 8];

	/* NumChannel(2) */
	info->channel = *(short *)&header[12 + 8 + 2];

	/* SampleRate(4) */
	info->sample_rate = *(int *)&header[12 + 8 + 2 + 2];

	/* ByteRate(4) */

	/* BlockAlign(2) */
	info->in_blockalign = *(short *)&header[12 + 8 + 2 + 2 + 4 + 4];

	/* BitsPerSample(2) */
	info->in_frame_bits = *(short *)&header[12 + 8 + 2 + 2 + 4 + 4 + 2];


	/* check the "data" chunk */
	if (fread(chunk_id, 4, 1, src) != 1)
		return -1;

	while (strncmp(chunk_id, "data", 4) != 0) {
		if (fread(&chunk_size, 4, 1, src) != 1)
			return -1;
		/* seek to next chunk if it is not "data" chunk */
		fseek(src, chunk_size, SEEK_CUR);
		if(fread(chunk_id, 4, 1, src) != 1) {
		    printf("No data chunk found \nWrong wave file format \n");
		    return -1;
		}
	}
	/* wave data length */
	if (fread(&info->input_data_len, 4, 1, src) != 1)
		return -1;

	fseek(src, -8, SEEK_CUR);
	if (fread(&header[format_size + 20], 1, 8, src) != 8)
		return -1;

	*(int *)&header[24] = info->output_sample_rate;

	info->in_slotwidth = 8 * info->in_blockalign / info->channel;

	switch (info->in_frame_bits) {
	case 8:
		info->input_format = SNDRV_PCM_FORMAT_U8;
		break;
	case 16:
		info->input_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	case 20:
		if (info->in_slotwidth == 24)
			info->input_format = SNDRV_PCM_FORMAT_S20_3LE;
		else
			return -1;
		break;
	case 24:
		if (info->in_slotwidth == 24)
			info->input_format = SNDRV_PCM_FORMAT_S24_3LE;
		else if (info->in_slotwidth == 32)
			info->input_format = SNDRV_PCM_FORMAT_S24_LE;
		else
			return -1;
		break;
	case 32:
		if (info->in_audioformat == 3)
			info->input_format = SNDRV_PCM_FORMAT_FLOAT_LE;
		else if (info->in_audioformat == 1)
			info->input_format = SNDRV_PCM_FORMAT_S32_LE;
		else
			return -1;
		break;
	default:
		return -1;
	}

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
	int err = 0;
	int frame = 0;
	int i, j;
	char path[64];

	convert_flag = 0;

	memset(&audio_info, 0, sizeof(struct audio_info_s));

	if (parse_arguments(ac, av, &audio_info) != 0 )
		return -1;

	if (dev_id == 0) {
		for (i = 0; i < 10; i++) {
			memset(path, 0, 64);
			sprintf(path, "/dev/snd/comprC%uD0", i);
			if (access(path, F_OK) == 0)
				break;
		}
		if (i == 10) {
			printf("no asrc sound card found\n");
			return -1;
		}
	} else {
		memset(path, 0, 64);
		sprintf(path, "/dev/snd/comprC%uD0", dev_id);
		if (access(path, F_OK) != 0) {
			printf("%s can't be accessed\n", path);
			return -1;
		}
	}

	printf("\n---- Running < %s > test on <%s> ---- \n\n", av[0], path);

	fd_asrc = open(path, O_RDWR);
	if (fd_asrc < 0) {
		printf("Unable to open device %d\n", fd_asrc);
		return -1;
	}

	if ((fd_dst = fopen(outfile, "wb+")) <= 0) {
		printf("output file not found\n");
		goto err_dst_not_found;
	}

	if ((fd_src = fopen(infile, "r")) <= 0) {
		printf("input file not found\n");
		goto err_src_not_found;
	}

	if (audio_info.pcm) {
		read_file_length(fd_src, &audio_info);
		audio_info.in_frame_bits = alsa_snd_pcm_format_width(audio_info.input_format);
		audio_info.in_blockalign = audio_info.channel * alsa_snd_pcm_format_physical_width(audio_info.input_format) / 8;
		audio_info.in_slotwidth = alsa_snd_pcm_format_physical_width(audio_info.input_format);

		if (audio_info.input_format == SNDRV_PCM_FORMAT_FLOAT_LE)
			audio_info.in_audioformat = 3;
		else
			audio_info.in_audioformat = 1;

		header_default(&audio_info);
	} else {
		if ((header_parser(fd_src, &audio_info)) <= 0) {
			goto end_head_parse;
		}
	}

	if (audio_info.output_format == 0)
		audio_info.output_format = SNDRV_PCM_FORMAT_S16_LE;

	/* read file */
	input_buffer = (int *)malloc(audio_info.input_data_len);
	if (fread(input_buffer, audio_info.input_data_len, 1, fd_src) != 1)
		goto end_head_parse;
	audio_info.input_dma_buf_size = (DMA_BUF_SIZE / audio_info.in_blockalign) * audio_info.in_blockalign;

	audio_info.output_data_len =
		asrc_get_output_buffer_size(audio_info.input_data_len,
					audio_info.sample_rate,
					audio_info.output_sample_rate,
					audio_info.input_format,
					audio_info.output_format);
	update_datachunk_length(&audio_info, *(int *)&header[16]);

	audio_info.out_frame_bits = alsa_snd_pcm_format_width(audio_info.output_format);
	audio_info.out_blockalign = audio_info.channel * alsa_snd_pcm_format_physical_width(audio_info.output_format) / 8;
	if (audio_info.output_format == SNDRV_PCM_FORMAT_FLOAT_LE)
		audio_info.out_audioformat = 3;
	else
		audio_info.out_audioformat = 1;

	/*change block align*/
	update_blockalign(&audio_info);
	update_sample_bitdepth(&audio_info);

	struct snd_compr_caps caps;
	if ((err = ioctl(fd_asrc, SNDRV_COMPRESS_GET_CAPS, &caps)) < 0) {
		printf("get caps FAILED\n");
		goto end_err;
	}

	struct snd_compr_codec_caps codec_caps = {};
	codec_caps.codec = SND_AUDIOCODEC_PCM;
	if ((err = ioctl(fd_asrc, SNDRV_COMPRESS_GET_CODEC_CAPS, &codec_caps)) < 0) {
		printf("get codec caps FAILED\n");
		goto end_err;
	}

	for (i = 0; i < codec_caps.num_descriptors; i++) {
		if (codec_caps.descriptor[i].formats != audio_info.input_format)
			continue;

		for (j = 0; j < codec_caps.descriptor[i].num_sample_rates; j++) {
			if (codec_caps.descriptor[i].sample_rates[j] == audio_info.sample_rate)
				break;
		}

		if (j == codec_caps.descriptor[i].num_sample_rates)
			continue;

		if (audio_info.output_sample_rate >= codec_caps.descriptor[i].src.out_sample_rate_min &&
		    audio_info.output_sample_rate <= codec_caps.descriptor[i].src.out_sample_rate_max)
			break;
	}

	if ( i == codec_caps.num_descriptors) {
		printf("caps don't support\n");
		goto end_err;

	}

	struct snd_compr_params params;
	params.buffer.fragment_size = 4096;
	params.buffer.fragments = 1;
	params.codec.id = SND_AUDIOCODEC_PCM;
	params.codec.ch_in  = audio_info.channel;
	params.codec.ch_out = audio_info.channel;
	params.codec.format = audio_info.input_format;
	params.codec.sample_rate = audio_info.sample_rate;
	params.codec.pcm_format = audio_info.output_format;
	params.codec.options.src_d.out_sample_rate = audio_info.output_sample_rate;
	if ((err = ioctl(fd_asrc, SNDRV_COMPRESS_SET_PARAMS, &params)) < 0) {
		printf("set params FAILED\n");
                return err;
        }

	struct snd_compr_task task = {};
	if ((err = ioctl(fd_asrc, SNDRV_COMPRESS_TASK_CREATE, &task)) < 0) {
		printf("task create FAILED %d\n", err);
                return err;
	}

	struct snd_compr_task_status status = {};
	status.seqno = task.seqno;

	void *bufin_start;
	bufin_start = mmap(NULL,
			    512 * 1024, /* set by the driver */
			    PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    task.input_fd,
			    0);
	if (bufin_start == MAP_FAILED) {
		printf("MMAP CAP err\n");
		goto end_err;
	}
	/* empty capture buffer */
	memset(bufin_start, 0, 512 * 1024);

	void *bufout_start;
	bufout_start = mmap(NULL,
			    512 * 1024, /* set by the driver */
			    PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    task.output_fd,
			    0);
	if (bufout_start == MAP_FAILED) {
		printf("MMAP OUT err\n");
		goto end_err;
	}
	/* empty capture buffer */
	memset(bufout_start, 0, 512 * 1024);

	if (alsa_snd_pcm_format_linear(audio_info.output_format) &&
	    alsa_snd_pcm_format_signed(audio_info.output_format) > 0)
		header_write(fd_dst);

	unsigned int input_buffer_length;
	char *input_p;

	input_p = (char *)input_buffer;
	convert_flag = 1;
	audio_info.output_used = 0;

	while (convert_flag) {
		input_buffer_length =
		    (audio_info.input_data_len > audio_info.input_dma_buf_size) ?
			audio_info.input_dma_buf_size : audio_info.input_data_len;

		if (audio_info.input_data_len > 0) {
			memcpy(bufin_start, input_p, input_buffer_length);
			input_p = input_p + input_buffer_length;
			audio_info.input_data_len -= input_buffer_length;
			task.input_size = audio_info.input_dma_buf_size;
		} else {
			memset(bufin_start, 0, audio_info.input_dma_buf_size);
			task.input_size = audio_info.input_dma_buf_size;
			convert_flag = 0;
		}

		frame++;

		if (ioctl(fd_asrc, SNDRV_COMPRESS_TASK_START, &task) < 0) {
			printf("task start FAILED\n");
			goto end_err;
		}

		if (ioctl(fd_asrc, SNDRV_COMPRESS_TASK_STOP, &task.seqno) < 0) {
			printf("task stop FAILED\n");
			goto end_err;
		}

		if (ioctl(fd_asrc, SNDRV_COMPRESS_TASK_STATUS, &status) < 0) {
			printf("task status FAILED\n");
			goto end_err;
		}

		if (audio_info.output_data_len > status.output_size) {
			audio_info.output_data_len -= status.output_size;
			audio_info.output_used += status.output_size;
			fwrite(bufout_start, status.output_size, 1, fd_dst);
		} else {
			audio_info.output_used += audio_info.output_data_len;
			fwrite(bufout_start, audio_info.output_data_len, 1, fd_dst);
			audio_info.output_data_len = 0;
		}
		if (audio_info.output_data_len == 0)
			break;
	}

	if(alsa_snd_pcm_format_linear(audio_info.output_format) &&
	   alsa_snd_pcm_format_signed(audio_info.output_format) > 0)
		header_update(fd_dst, &audio_info);

	if (ioctl(fd_asrc, SNDRV_COMPRESS_TASK_FREE, &task.seqno) < 0) {
		printf("task free FAILED\n");
		goto end_err;
	}

	fclose(fd_src);
	fclose(fd_dst);
	close(fd_asrc);

	free(input_buffer);
	printf("All tests passed with success\n");
	return 0;

end_err:
	free(input_buffer);
end_head_parse:
	fclose(fd_src);
err_src_not_found:
	fclose(fd_dst);
err_dst_not_found:
	close(fd_asrc);
	return err;
}
