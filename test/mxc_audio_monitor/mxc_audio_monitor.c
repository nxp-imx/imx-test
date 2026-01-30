/*
 * Copyright 2020,2025 NXP
 *
 * SPDX-License-Identifier: BSD-3
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

struct config {
	char *board;
	char *indevice;
	char *outdevice;
	char *monitordevice;
	char *pll;
	bool is_spdif;
	int step;
	int in_ratio;
	int out_ratio;
	int in_rate;
	int out_rate;
};

struct PID_Controller {
	float kp;           // Proportional gain
	float ki;           // Integral gain
	float kd;           // Derivative gain
	float dt;           // Sample time (seconds)

	float integral;     // Integral accumulator
	float prev_error;   // Previous error for derivative

	float output_min;   // Minimum output limit
	float output_max;   // Maximum output limit
};

// Initialize PID controller
void PID_Init(struct PID_Controller *pid, float kp, float ki, float kd,
	      float dt, float out_min, float out_max)
{
	pid->kp = kp;
	pid->ki = ki;
	pid->kd = kd;
	pid->dt = dt;

	pid->integral = 0.0f;
	pid->prev_error = 0.0f;

	pid->output_min = out_min;
	pid->output_max = out_max;
}

// Compute PID output
float PID_Compute(struct PID_Controller *pid, float setpoint,
		  float measured_value)
{
	// Calculate error
	float error = setpoint - measured_value;

	// Proportional term
	float p_term = pid->kp * error;

	// Integral term with anti-windup
	pid->integral += error * pid->dt;
	float i_term = pid->ki * pid->integral;

	// Derivative term
	float derivative = (error - pid->prev_error) / pid->dt;
	float d_term = pid->kd * derivative;

	// Calculate total output
	float output = p_term + i_term + d_term;

	// Apply output limits (clamping)
	if (output > pid->output_max) {
		output = pid->output_max;
		// Anti-windup: prevent integral from growing
		pid->integral -= error * pid->dt;
	} else if (output < pid->output_min) {
		output = pid->output_min;
		// Anti-windup: prevent integral from growing
		pid->integral -= error * pid->dt;
	}

	// Save error for next iteration
	pid->prev_error = error;

	return output;
}

// Reset PID controller
void PID_Reset(struct PID_Controller *pid)
{
	pid->integral = 0.0f;
	pid->prev_error = 0.0f;
}

void usage(void) {
	printf("\n\n**************************************************\n");
	printf("* Test aplication for adjust audio pll on imx8mm\n");
	printf("* Before running this test, please start a pipeline for input and output\n");
	printf("* For example:\n");
	printf("* 8MM: arecord -Dplughw:0 -f S32_LE -r 48000 -c 2 -traw | aplay -Dhw:3 -f S32_LE -r 48000 -c 2 -traw\n");
	printf("*      hw:0 is the spdif sound card, hw:3 is wm8524 sound card\n");
	printf("* 8MP: arecord -Diec958:1 -f S32_LE -r 48000 -c 2 -traw | aplay -Dhw:3 -f S32_LE -r 48000 -c 2 -traw\n");
	printf("*      iec958:1 is the xcvr sound card, hw:3 is wm8960 sound card\n");
	printf("\n");
	printf("* Options :\n");
	printf("-b board type, 8MM or 8MP");
	printf("-i input device,  SPDIF or SAIx\n");
	printf("-m for SPDIF input, we need to assign a free SAI monitor device");
	printf("-o output device, SAIx\n");
	printf("-p PLL instance, PLL1 or PLL2\n");
	printf("-s PLL adjust step\n");
	printf("-u input clock ratio bitclock/ratio = frameclock\n");
	printf("-v output clock ratio bitclock/ratio = frameclock\n");
	printf("-l input sample rate\n");
	printf("-k output sample rate\n");
	printf("Example: ./mxc_audio_monitor.out -b 8MM -i SPDIF -m SAI6 -o SAI3 -p PLL1 -s 10 -u 32 -v 64 -l 48000 -k 48000\n");
	printf("Example: ./mxc_audio_monitor.out -b 8MP -i XCVR -o SAI3 -p PLL1 -s 10 -u 128 -v 64 -l 48000 -k 48000\n");
	return;
}

int parse_arguments(int argc, const char *argv[], struct config *conf)
{
	/* Usage checking  */
	if( argc < 3 )
	{
		usage();
		exit(1);
	}

	int c, option_index;
	static const char short_options[] = "ho:i:m:p:s:u:v:l:k:b:";
	static const struct option long_options[] = {
		{"help", 0, 0, 'h'},
		{"outdevice", 1, 0, 'o'},
		{"indevice", 1, 0, 'i'},
		{"monitordevice", 1, 0, 'm'},
		{"pll", 1, 0, 'p'},
		{"step", 1, 0, 's'},
		{"iratio", 1, 0, 'u'},
		{"oratio", 1, 0, 'v'},
		{"irate", 1, 0, 'l'},
		{"orate", 1, 0, 'k'},
		{"board", 1, 0, 'b'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, (char * const*)argv, short_options, long_options, &option_index)) != -1) {
		switch (c) {
		case 'b':
			conf->board = optarg;
			if (strlen(conf->board) != 3) {
				printf("Unsupported board type %s\n", conf->board);
				exit(1);
			}
			break;
		case 'o':
			conf->outdevice = optarg;
			if (strlen(conf->outdevice) != 4) {
				printf("Unsupported out device %s\n", conf->outdevice);
				exit(1);
			}
			break;
		case 'i':
			conf->indevice = optarg;
			if (strlen(conf->indevice) != 5 && strlen(conf->indevice) != 4) {
				printf("Unsupported in device %s\n", conf->indevice);
				exit(1);
			}
			break;
		case 'm':
			conf->monitordevice = optarg;
			if (strlen(conf->monitordevice) != 4) {
				printf("Unsupported monitor device %s\n", conf->monitordevice);
				exit(1);
			}
			break;
		case 'p':
			conf->pll = optarg;
			if (strlen(conf->pll) != 4) {
				printf("Unsupported pll %s\n", conf->pll);
				exit(1);
			}
			break;
		case 's':
			conf->step = strtol(optarg, NULL, 0);
			if (conf->step <= 0) {
				printf("wrong step %d\n", conf->step);
				exit(1);
			}
			break;
		case 'u':
			conf->in_ratio = strtol(optarg, NULL, 0);
			if (conf->in_ratio <= 0) {
				printf("wrong input ratio %d\n", conf->in_ratio);
				exit(1);
			}
			break;
		case 'v':
			conf->out_ratio = strtol(optarg, NULL, 0);
			if (conf->out_ratio <= 0) {
				printf("wrong output ratio %d\n", conf->out_ratio);
				exit(1);
			}
			break;
		case 'l':
			conf->in_rate = strtol(optarg, NULL, 0);
			if (conf->in_rate <= 0) {
				printf("wrong input rate %d\n", conf->in_rate);
				exit(1);
			}
			break;
		case 'k':
			conf->out_rate = strtol(optarg, NULL, 0);
			if (conf->out_rate <= 0) {
				printf("wrong output rate %d\n", conf->out_rate);
				exit(1);
			}
			break;
		case 'h':
			usage();
			exit(1);
		default:
			printf("Unknown Command  -%c \n", c);
			exit(1);
		}
	}

	return 0;
}

int main(int argc, const char **argv)
{
	char monitor_id = 0;
	char outdev_id = 0;
	int pll_id = 0;

	struct config conf;
	char *base_path_in;
	char *base_path_out;
	char *saipath_rx_monitor_spdif;

	char *saipath_rx_bitcnt;
	char *saipath_rx_bitcnt_latched_timestamp;
	char *saipath_rx_bitcnt_reset;
	char *saipath_rx_timestamp;
	char *saipath_rx_timestamp_enable;
	char *saipath_rx_timestamp_increment;
	char *saipath_rx_timestamp_reset;

	char *saipath_tx_bitcnt;
	char *saipath_tx_bitcnt_latched_timestamp;
	char *saipath_tx_bitcnt_reset;
	char *saipath_tx_timestamp;
	char *saipath_tx_timestamp_enable;
	char *saipath_tx_timestamp_increment;
	char *saipath_tx_timestamp_reset;

	char *saipath_runtimestatus;
	char *spdifpath_runtimestatus;
	char *xcvrpath_runtimestatus;

	char *xcvrpath_rx_bitcnt;
	char *xcvrpath_rx_bitcnt_latched_timestamp;
	char *xcvrpath_rx_bitcnt_reset;
	char *xcvrpath_rx_timestamp;
	char *xcvrpath_rx_timestamp_enable;
	char *xcvrpath_rx_timestamp_increment;
	char *xcvrpath_rx_timestamp_reset;

	char *pll1_k_path;
	char *pll1_param;

	char *pll2_k_path;
	char *pll2_param;

	int fd_in_runtime;
	int fd_in_monitor_spdif = 0;
	int fd_out_runtime;
	int fd_in[7];
	int fd_out[7];
	int fd_pll_k;
	int fd_pll_param;

	char *pll_k_path;
	char *pll_param;
	char *in_path[7];
	char *out_path[7];
	char *inpath_runtimestatus;
	char *outpath_runtimestatus;

	char filename[200];
	char buf[100];
	char id[2];
	int len;
	int i, count;
	unsigned int in_bitcount_prev, in_timestampcount_prev;
	unsigned int in_bitcount, in_timestampcount;
	unsigned int out_bitcount_prev, out_timestampcount_prev;
	unsigned int out_bitcount, out_timestampcount;

	long in_bitcount_err, in_timestampcount_err;
	long in_bitcount_sum, in_timestampcount_sum;
	long out_bitcount_err, out_timestampcount_err;
	long out_bitcount_sum, out_timestampcount_sum;
	long in_ipg_clock, out_ipg_clock;

	double in_clock, out_clock;

	double clock_diff;

	char *p_mdiv, *p_pdiv, *p_sdiv, *p_kdiv;
	int kdiv, kdiv_new, delta_kdiv;
	int mdiv, pdiv, sdiv;
	int stable_count;

	conf.step = 1;
	conf.in_ratio = 32;
	conf.out_ratio = 64;
	conf.is_spdif = 0;

	if (parse_arguments(argc, argv, &conf) != 0)
		return -1;

	if (!strcmp(conf.board, "8MM")) {
		base_path_in = "/sys/devices/platform/soc@0/30000000.bus/30000000.spba-bus/300";
		base_path_out = "/sys/devices/platform/soc@0/30000000.bus/30000000.spba-bus/300";
		saipath_rx_monitor_spdif = "0000.sai/rx_monitor_spdif";

		saipath_rx_bitcnt = "0000.sai/rx_bitcnt";
		saipath_rx_bitcnt_latched_timestamp = "0000.sai/rx_bitcnt_latched_timestamp";
		saipath_rx_bitcnt_reset = "0000.sai/rx_bitcnt_reset";
		saipath_rx_timestamp = "0000.sai/rx_timestamp";
		saipath_rx_timestamp_enable = "0000.sai/rx_timestamp_enable";
		saipath_rx_timestamp_increment = "0000.sai/rx_timestamp_increment";
		saipath_rx_timestamp_reset = "0000.sai/rx_timestamp_reset";

		saipath_tx_bitcnt = "0000.sai/tx_bitcnt";
		saipath_tx_bitcnt_latched_timestamp = "0000.sai/tx_bitcnt_latched_timestamp";
		saipath_tx_bitcnt_reset = "0000.sai/tx_bitcnt_reset";
		saipath_tx_timestamp = "0000.sai/tx_timestamp";
		saipath_tx_timestamp_enable = "0000.sai/tx_timestamp_enable";
		saipath_tx_timestamp_increment = "0000.sai/tx_timestamp_increment";
		saipath_tx_timestamp_reset = "0000.sai/tx_timestamp_reset";

		saipath_runtimestatus="0000.sai/power/runtime_status";
		spdifpath_runtimestatus="90000.spdif/power/runtime_status";

		pll1_k_path="/sys/kernel/debug/audio_pll_monitor/audio_pll1/delta_k";
		pll1_param = "/sys/kernel/debug/audio_pll_monitor/audio_pll1/pll_parameter";

		pll2_k_path="/sys/kernel/debug/audio_pll_monitor/audio_pll2/delta_k";
		pll2_param = "/sys/kernel/debug/audio_pll_monitor/audio_pll2/pll_parameter";

		inpath_runtimestatus = spdifpath_runtimestatus;
		in_path[0] = saipath_rx_bitcnt;
		in_path[1] = saipath_rx_bitcnt_latched_timestamp;
		in_path[2] = saipath_rx_timestamp;
		in_path[3] = saipath_rx_bitcnt_reset;
		in_path[4] = saipath_rx_timestamp_enable;
		in_path[5] = saipath_rx_timestamp_increment;
		in_path[6] = saipath_rx_timestamp_reset;

		outpath_runtimestatus = saipath_runtimestatus;
		out_path[0] = saipath_tx_bitcnt;
		out_path[1] = saipath_tx_bitcnt_latched_timestamp;
		out_path[2] = saipath_tx_timestamp;
		out_path[3] = saipath_tx_bitcnt_reset;
		out_path[4] = saipath_tx_timestamp_enable;
		out_path[5] = saipath_tx_timestamp_increment;
		out_path[6] = saipath_tx_timestamp_reset;

		in_ipg_clock = 400000000;
		out_ipg_clock = 400000000;

	} else { /*8MP*/

		base_path_in = "/sys/devices/platform/soc@0/30c00000.bus/30c00000.spba-bus/30c";
		base_path_out = "/sys/devices/platform/soc@0/30c00000.bus/30c00000.spba-bus/30c";
		saipath_rx_monitor_spdif = NULL;

		xcvrpath_rx_bitcnt = "0000.xcvr/counters/rx_bitcnt";
		xcvrpath_rx_bitcnt_latched_timestamp = "0000.xcvr/counters/rx_bitcnt_latched_timestamp";
		xcvrpath_rx_bitcnt_reset = "0000.xcvr/counters/rx_bitcnt_reset";
		xcvrpath_rx_timestamp = "0000.xcvr/counters/rx_timestamp";
		xcvrpath_rx_timestamp_enable = "0000.xcvr/counters/rx_timestamp_enable";
		xcvrpath_rx_timestamp_increment = "0000.xcvr/counters/rx_timestamp_increment";
		xcvrpath_rx_timestamp_reset = "0000.xcvr/counters/rx_timestamp_reset";

		saipath_tx_bitcnt = "0000.sai/tx_bitcnt";
		saipath_tx_bitcnt_latched_timestamp = "0000.sai/tx_bitcnt_latched_timestamp";
		saipath_tx_bitcnt_reset = "0000.sai/tx_bitcnt_reset";
		saipath_tx_timestamp = "0000.sai/tx_timestamp";
		saipath_tx_timestamp_enable = "0000.sai/tx_timestamp_enable";
		saipath_tx_timestamp_increment = "0000.sai/tx_timestamp_increment";
		saipath_tx_timestamp_reset = "0000.sai/tx_timestamp_reset";

		saipath_runtimestatus = "0000.sai/power/runtime_status";
		spdifpath_runtimestatus = NULL;
		xcvrpath_runtimestatus = "0000.xcvr/power/runtime_status";

		pll1_k_path="/sys/kernel/debug/audio_pll_monitor/audio_pll1/delta_k";
		pll1_param = "/sys/kernel/debug/audio_pll_monitor/audio_pll1/pll_parameter";

		pll2_k_path="/sys/kernel/debug/audio_pll_monitor/audio_pll2/delta_k";
		pll2_param = "/sys/kernel/debug/audio_pll_monitor/audio_pll2/pll_parameter";

		inpath_runtimestatus = xcvrpath_runtimestatus;
		in_path[0] = xcvrpath_rx_bitcnt;
		in_path[1] = xcvrpath_rx_bitcnt_latched_timestamp;
		in_path[2] = xcvrpath_rx_timestamp;
		in_path[3] = xcvrpath_rx_bitcnt_reset;
		in_path[4] = xcvrpath_rx_timestamp_enable;
		in_path[5] = xcvrpath_rx_timestamp_increment;
		in_path[6] = xcvrpath_rx_timestamp_reset;

		outpath_runtimestatus = saipath_runtimestatus;
		out_path[0] = saipath_tx_bitcnt;
		out_path[1] = saipath_tx_bitcnt_latched_timestamp;
		out_path[2] = saipath_tx_timestamp;
		out_path[3] = saipath_tx_bitcnt_reset;
		out_path[4] = saipath_tx_timestamp_enable;
		out_path[5] = saipath_tx_timestamp_increment;
		out_path[6] = saipath_tx_timestamp_reset;

		in_ipg_clock = 400000000;
		out_ipg_clock = 400000000;
	}

	if (strlen(conf.indevice) == 5) {
		if (!strncmp("SPDIF", conf.indevice, 5)) {
			conf.is_spdif = true;
			if (!strncmp("SAI", conf.monitordevice, 3))
				monitor_id = conf.monitordevice[3];
		} else {
			printf("Unsupported in device %s\n", conf.indevice);
		}
	} else {
		if (!strncmp("SAI", conf.indevice, 3)) {
			monitor_id = conf.indevice[3];
		} else if (!strncmp("XCVR", conf.indevice, 4)) {
			monitor_id = 'c';
		} else {
			printf("Unsupported in device %s\n", conf.indevice);
		}
	}

	if (!strncmp("SAI", conf.outdevice, 3)) {
		outdev_id = conf.outdevice[3];
	} else {
		printf("Unsupported out device %s\n", conf.outdevice);
	}

	if (!strncmp("PLL", conf.pll, 3)) {
		pll_id = atoi(&conf.pll[3]);
		if (pll_id != 1 && pll_id != 2) {
			printf("Unsupported pll device %s\n", conf.pll);
			return -1;
		}
	} else {
		printf("Unsupported pll device %s\n", conf.pll);
	}

	if (!strcmp(conf.board, "8MM"))
		printf("monitor device SAI%c, out device SAI%d, PLL%d\n", monitor_id, outdev_id, pll_id);
	else
		printf("monitor device XCVR, out device SAI%c, PLL%d\n", outdev_id, pll_id);

	if (pll_id == 1) {
		pll_k_path = pll1_k_path;
		pll_param = pll1_param;
	} else {
		pll_k_path = pll2_k_path;
		pll_param = pll2_param;
	}

	if (conf.is_spdif) {
		memset(filename, 0, sizeof(filename));
		strcpy(filename, base_path_in);
		strcat(filename, inpath_runtimestatus);

		fd_in_runtime = open(filename, O_RDONLY);
		if (fd_in_runtime < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

		memset(filename, 0, sizeof(filename));
		id[0] = monitor_id;
		id[1] = '\0';
		strcpy(filename, base_path_in);
		strcat(filename, id);
		strcat(filename, saipath_rx_monitor_spdif);

		fd_in_monitor_spdif = open(filename, O_WRONLY);
		if (fd_in_monitor_spdif < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

	} else {
		memset(filename, 0, sizeof(filename));
		id[0] = monitor_id;
		id[1] = '\0';
		strcpy(filename, base_path_in);
		strcat(filename, id);
		strcat(filename, inpath_runtimestatus);

		fd_in_runtime = open(filename, O_RDONLY);
		if (fd_in_runtime < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));
	}

	/* out */
	memset(filename, 0, sizeof(filename));
	id[0] = outdev_id;
	id[1] = '\0';
	strcpy(filename, base_path_out);
	strcat(filename, id);
	strcat(filename, outpath_runtimestatus);

	fd_out_runtime = open(filename, O_RDONLY);
	if (fd_out_runtime < 0)
		fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

	memset(filename, 0, sizeof(filename));
	strcpy(filename, pll_k_path);
	fd_pll_k = open(filename, O_WRONLY);
	if (fd_pll_k < 0)
		fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

	memset(filename, 0, sizeof(filename));
	strcpy(filename, pll_param);
	fd_pll_param = open(filename, O_RDONLY);
	if (fd_pll_param < 0)
		fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

	for (i = 0; i < 3; i++) {
		memset(filename, 0, sizeof(filename));
		id[0] = monitor_id;
		id[1] = '\0';
		strcpy(filename, base_path_in);
		strcat(filename, id);
		strcat(filename, in_path[i]);

		fd_in[i] = open(filename, O_RDONLY);
		if (fd_in[i] < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

		memset(filename, 0, sizeof(filename));
		id[0] = outdev_id;
		id[1] = '\0';
		strcpy(filename, base_path_out);
		strcat(filename, id);
		strcat(filename, out_path[i]);

		fd_out[i] = open(filename, O_RDONLY);
		if (fd_out[i] < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));
	}

	for (i = 3; i < 7; i++) {
		memset(filename, 0, sizeof(filename));
		id[0] = monitor_id;
		id[1] = '\0';
		strcpy(filename, base_path_in);
		strcat(filename, id);
		strcat(filename, in_path[i]);

		fd_in[i] = open(filename, O_WRONLY);
		if (fd_in[i] < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));

		memset(filename, 0, sizeof(filename));
		id[0] = outdev_id;
		id[1] = '\0';
		strcpy(filename, base_path_out);
		strcat(filename, id);
		strcat(filename, out_path[i]);

		fd_out[i] = open(filename, O_WRONLY);
		if (fd_out[i] < 0)
			fprintf(stderr, "open %s: %s\n", filename, strerror(errno));
	}

	count = 0;
	stable_count = 0;
	in_bitcount_prev = 0;
	in_timestampcount_prev = 0;

	in_bitcount_sum = 0;
	in_timestampcount_sum = 0;
	out_bitcount_sum = 0;
	out_timestampcount_sum = 0;

	len = read(fd_pll_param, buf, sizeof(buf)-1);
	p_kdiv = strstr(buf, "Kdiv");
	kdiv = strtol(p_kdiv + 8, NULL, 16);

	p_mdiv = strstr(buf, "Mdiv");
	mdiv = strtol(p_mdiv + 8, NULL, 16);

	p_pdiv = strstr(buf, "Pdiv");
	pdiv = strtol(p_pdiv + 8, NULL, 16);

	p_sdiv = strstr(buf, "Sdiv");
	sdiv = strtol(p_sdiv + 8, NULL, 16);

	p_kdiv = strstr(buf, "Kdiv");
	kdiv = strtol(p_kdiv + 8, NULL, 16);

	float fout_pll = 24000000 * (mdiv * 65536 + (float)kdiv) / (pdiv * 65536) / (1 << sdiv);
	int pll_div_sample_rate = fout_pll / conf.out_rate;

	struct PID_Controller pid;

	PID_Init(&pid, 2.0f, 0.5f, 0.1f, 0.1f, -100.0f, 100.0f);

	while (count < 1000 && stable_count < 10) {
		memset(buf, 0, sizeof(buf));
		lseek(fd_in_runtime, 0, SEEK_SET);
		len = read(fd_in_runtime, buf, sizeof(buf)-1);
		if (len < 0)
			fprintf(stderr, "read %s: %s", filename, strerror(errno));

		if (strncmp(buf, "active", 6)) {
			printf("in device is not active\n");
			goto fail;
		}

		lseek(fd_out_runtime, 0, SEEK_SET);
		len = read(fd_out_runtime, buf, sizeof(buf)-1);
		if (len < 0)
			fprintf(stderr, "read %s: %s", filename, strerror(errno));

		if (strncmp(buf, "active", 6)) {
			printf("out device is not active\n");
			goto fail;
		}

		lseek(fd_in[0], 0, SEEK_SET);
		lseek(fd_in[1], 0, SEEK_SET);
		lseek(fd_in[2], 0, SEEK_SET);

		if (conf.is_spdif) {
			memset(buf, 0, sizeof(buf));
			buf[0] = '1';
			len = write(fd_in_monitor_spdif, buf, sizeof(buf)-1);
		}

		lseek(fd_in[3], 0, SEEK_SET);
		lseek(fd_in[4], 0, SEEK_SET);
		lseek(fd_in[5], 0, SEEK_SET);
		lseek(fd_in[6], 0, SEEK_SET);

		memset(buf, 0, sizeof(buf));
		buf[0] = '0';
		len = write(fd_in[5], buf, sizeof(buf)-1);
		len = write(fd_in[4], buf, sizeof(buf)-1);

		lseek(fd_in[4], 0, SEEK_SET);
		buf[0] = '1';
		len = write(fd_in[4], buf, sizeof(buf)-1);

		memset(buf, 0, sizeof(buf));
		buf[0] = '1';
		len = write(fd_in[3], buf, sizeof(buf)-1);
		len = write(fd_in[6], buf, sizeof(buf)-1);
		buf[0] = '0';
		len = write(fd_in[3], buf, sizeof(buf)-1);
		len = write(fd_in[6], buf, sizeof(buf)-1);

		/* Read counter and timestamp */
		len = read(fd_in[0], buf, sizeof(buf)-1);
		in_bitcount_prev = strtol(buf, NULL, 0);

		len = read(fd_in[1], buf, sizeof(buf)-1);
		in_timestampcount_prev = strtol(buf, NULL, 0);

		usleep(10000);

		lseek(fd_in[0], 0, SEEK_SET);
		lseek(fd_in[1], 0, SEEK_SET);
		lseek(fd_in[2], 0, SEEK_SET);
		/* Read counter and timestamp */
		len = read(fd_in[0], buf, sizeof(buf)-1);
		in_bitcount = strtol(buf, NULL, 0);

		len = read(fd_in[1], buf, sizeof(buf)-1);
		in_timestampcount = strtol(buf, NULL, 0);

		in_bitcount_err = (long)in_bitcount - (long)in_bitcount_prev;
		in_timestampcount_err = (long)in_timestampcount - (long)in_timestampcount_prev;

		if (in_bitcount_err < 0)
			in_bitcount_err += 0x100000000L;
		if (in_timestampcount_err < 0)
			in_timestampcount_err += 0x100000000L;

		in_bitcount_sum += in_bitcount_err;
		in_timestampcount_sum += in_timestampcount_err;

		in_clock = (double)in_bitcount_sum * in_ipg_clock / in_timestampcount_sum;
		in_clock /= conf.in_ratio;

		lseek(fd_out[0], 0, SEEK_SET);
		lseek(fd_out[1], 0, SEEK_SET);
		lseek(fd_out[2], 0, SEEK_SET);

		lseek(fd_out[3], 0, SEEK_SET);
		lseek(fd_out[4], 0, SEEK_SET);
		lseek(fd_out[5], 0, SEEK_SET);
		lseek(fd_out[6], 0, SEEK_SET);

		memset(buf, 0, sizeof(buf));
		buf[0] = '0';
		len = write(fd_out[5], buf, sizeof(buf)-1);
		len = write(fd_out[4], buf, sizeof(buf)-1);

		lseek(fd_out[4], 0, SEEK_SET);
		buf[0] = '1';
		len = write(fd_out[4], buf, sizeof(buf)-1);

		memset(buf, 0, sizeof(buf));
		buf[0] = '1';
		len = write(fd_out[3], buf, sizeof(buf)-1);
		len = write(fd_out[6], buf, sizeof(buf)-1);
		buf[0] = '0';
		len = write(fd_out[3], buf, sizeof(buf)-1);
		len = write(fd_out[6], buf, sizeof(buf)-1);

		/* Read counter and timestamp */
		len = read(fd_out[0], buf, sizeof(buf)-1);
		out_bitcount_prev = strtol(buf, NULL, 0);

		len = read(fd_out[1], buf, sizeof(buf)-1);
		out_timestampcount_prev = strtol(buf, NULL, 0);

		usleep(10000);

		lseek(fd_out[0], 0, SEEK_SET);
		lseek(fd_out[1], 0, SEEK_SET);
		lseek(fd_out[2], 0, SEEK_SET);
		/* Read counter and timestamp */
		len = read(fd_out[0], buf, sizeof(buf)-1);
		out_bitcount = strtol(buf, NULL, 0);

		len = read(fd_out[1], buf, sizeof(buf)-1);
		out_timestampcount = strtol(buf, NULL, 0);

		out_bitcount_err = (long)out_bitcount - (long)out_bitcount_prev;
		out_timestampcount_err = (long)out_timestampcount - (long)out_timestampcount_prev;

		if (out_bitcount_err < 0)
			out_bitcount_err += 0x100000000L;
		if (out_timestampcount_err < 0)
			out_timestampcount_err += 0x100000000L;

		out_bitcount_sum += out_bitcount_err;
		out_timestampcount_sum += out_timestampcount_err;

		out_clock = (double)out_bitcount_sum * out_ipg_clock / out_timestampcount_sum;
		out_clock /= conf.out_ratio;

		clock_diff = (double)out_clock  - (double)(in_clock * conf.out_rate / conf.in_rate);
		if (clock_diff > 1000) {
			printf("there may be wrong setting for rate & ratio\n");
			goto fail;
		}

		float pid_out = PID_Compute(&pid, in_clock, out_clock);
		int delta;

		memset(buf, 0, sizeof(buf));
		lseek(fd_pll_param, 0, SEEK_SET);
		len = read(fd_pll_param, buf, sizeof(buf)-1);

		char *p_k;
		int k_t;

		p_k = strstr(buf, "Kdiv");
		k_t = strtol(p_k + 8, NULL, 16);

		/* 8192 is got from (pll rate / sample rate) */
		float fout = out_clock * pll_div_sample_rate;
		float foutnew = fout + pid_out * pll_div_sample_rate;
		float k_new = (foutnew * (1 << sdiv) * (pdiv * 65536) / 24000000) - mdiv * 65536;

		delta = (int)(k_new - k_t);
		printf("info %f, %f, %f, %d, -- %f, %f, %f, %d,\n", pid_out,
		       in_clock, out_clock, pll_div_sample_rate, fout, foutnew, k_new, delta);

		memset(buf, 0, sizeof(buf));
		lseek(fd_pll_k, 0, SEEK_SET);
		snprintf(buf, sizeof(buf)-1, "%d", delta);
		len = write(fd_pll_k, buf, sizeof(buf)-1);

		usleep(100000);
		count++;
	}

fail:
	memset(buf, 0, sizeof(buf));
	lseek(fd_pll_param, 0, SEEK_SET);
	len = read(fd_pll_param, buf, sizeof(buf)-1);
	p_kdiv = strstr(buf, "Kdiv");
	kdiv_new = strtol(p_kdiv + 8, NULL, 16);
	delta_kdiv = -(kdiv_new - kdiv);

	memset(buf, 0, sizeof(buf));
	lseek(fd_pll_k, 0, SEEK_SET);
	snprintf(buf, sizeof(buf)-1, "%d", delta_kdiv);
	len = write(fd_pll_k, buf, sizeof(buf)-1);

	if (conf.is_spdif) {
		memset(buf, 0, sizeof(buf));
		buf[0] = '0';
		len = write(fd_in_monitor_spdif, buf, sizeof(buf)-1);
	}

	close(fd_pll_k);
	close(fd_pll_param);
	close(fd_in_runtime);
	close(fd_out_runtime);

	for (i = 0; i < 7; i++) {
		close(fd_in[i]);
		close(fd_out[i]);
	}

	return 0;
}
