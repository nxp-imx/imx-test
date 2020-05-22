/*
 * Copyright 2020-2017 NXP
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mxc_pdm_alsa.h"

#ifdef HAS_IMX_SW_PDM
cic_t cic_decoder_type(unsigned long type)
{
	switch (type) {
	case 12: return CIC_pdmToPcmType_cic_order_5_cic_downsample_12;
	case 16: return CIC_pdmToPcmType_cic_order_5_cic_downsample_16;
	case 24: return CIC_pdmToPcmType_cic_order_5_cic_downsample_24;
	case 32: return CIC_pdmToPcmType_cic_order_5_cic_downsample_32;
	case 48: return CIC_pdmToPcmType_cic_order_5_cic_downsample_48;
	default: return CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable;
	}
}
#endif

void print_help(const char *argv[])
{
	fprintf(stderr, "Usage: %s \n\n"
		"  --help     (h): this screen\n"
		"  --device   (d): SAI capture device\n"
		"  --channels (c): number of channels\n"
		"  --block    (b): output samples per run per channel\n"
		"  --gain     (g): output multipler scale factor\n"
		"                  if not set, value is calculated\n"
		"  --log      (l): log debug information\n"
		"  --outFile  (o): output file\n"
		"  --type     (t): 5 order cic decoder type\n"
		"                  [12, 16, 24, 32, 48]\n"
		"  --rate     (r): sample rate\n"
		"  --seconds  (s): number of seconds to capture\n"
		, argv[0]);
}

int main(int argc, char **argv)
{
	const char *output_file = NULL;
	struct mxc_pdm_priv *priv;
	int opt, option_index;
	unsigned long arg;
	char *cptr;

	/* Init private struct */
	priv = malloc(sizeof(struct mxc_pdm_priv));
	if (!priv)
		return -ENOMEM;

	static struct option long_options[] = {
		{"log",      no_argument,       NULL, 'l'},
		{"block",    required_argument, NULL, 'b'},
		{"channels", required_argument, NULL, 'c'},
		{"gain",     required_argument, NULL, 'g'},
		{"help",     no_argument,       NULL, 'h'},
		{"device",   required_argument, NULL, 'd'},
		{"output",   required_argument, NULL, 'o'},
		{"rate",     required_argument, NULL, 'r'},
		{"seconds",  required_argument, NULL, 's'},
		{"type",     required_argument, NULL, 't'},
		{NULL, 0, NULL, 0}
	};

	/* Default config */
	priv->debug_info = 0;
	priv->type = CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable;

	while (1) {
		option_index = 0;
		opt = getopt_long_only(argc, argv, "hld:b:c:g:o:r:s:t:",
				long_options, &option_index);
		if (opt == -1)
			break;
		switch (opt) {
		case 0:
			fprintf(stderr, "option %s",
					long_options[option_index].name);
			if (optarg)
				fprintf(stderr, " with args %s", optarg);
			fprintf(stderr, "\n");
			break;
		case 'b':
			priv->samples_per_channel =
				(unsigned int)strtoul(optarg, &cptr, 10);
			break;
		case 'c':
			priv->channels =
				(unsigned int)strtoul(optarg, &cptr, 10);
			break;
		case 'd':
			priv->device = strdup(optarg);
			break;
		case 'g':
			priv->gain = strtof(optarg, &cptr);
			break;
		case 'h':
			print_help(argv);
			exit(1);
		case 'l':
			priv->debug_info = 1;
			break;
		case 'o':
			output_file = strdup(optarg);
			break;
		case 'r':
			priv->rate = (unsigned int)strtoul(optarg, &cptr, 10);
			break;
		case 's':
			priv->seconds =
				(unsigned int)strtoul(optarg, &cptr, 10);
			break;
		case 't':
#ifdef HAS_IMX_SW_PDM
			arg = strtoul(optarg, &cptr, 10);
			priv->type = cic_decoder_type(arg);
			if (priv->type ==
				CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable) {
				fprintf(stderr, "cic downsample not supported\n");
				fprintf(stderr, "using built in sw decimation algo\n");
			}
#else
			priv->type = CIC_pdmToPcmType_cic_order_5_cic_downsample_unavailable;
#endif
			break;
		case ':':
			fprintf(stderr, "option %c missing arguments\n", optopt);
			exit(1);
		case '?':
		default:
			fprintf(stderr, "invalid option '%c'\n", optopt);
			exit(1);
		}
	}

	if (output_file) {
		priv->fd_out = fopen(output_file, "w+");
		if (priv->fd_out < 0) {
			fprintf(stderr, "fail to open %s file\n", output_file);
			return -EINVAL;
		}
	}

	return (mxc_alsa_pdm_process(priv));
}
