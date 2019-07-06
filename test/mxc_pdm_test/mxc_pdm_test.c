/*
 * Copyright 2017 NXP
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

int main(int argc, char **argv)
{
	const char *output_file = NULL;
	struct mxc_pdm_priv *priv;
	int opt, option_index;
	char *cptr;

	/* Init private struct */
	priv = malloc(sizeof(struct mxc_pdm_priv));
	if (!priv)
		return -ENOMEM;

	static struct option long_options[] = {
		{"log",     no_argument,       NULL, 'l'},
		{"device",  required_argument, NULL, 'd'},
		{"output",  required_argument, NULL, 'o'},
		{"rate",    required_argument, NULL, 'r'},
		{"seconds", required_argument, NULL, 's'},
		{NULL, 0, NULL, 0}
	};

	priv->debug_info = 0;

	while (1) {
		option_index = 0;
		opt = getopt_long_only(argc, argv, "ld:o:r:s:", long_options,
				&option_index);
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
		case 'd':
			priv->device = strdup(optarg);
			break;
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
		case ':':
			fprintf(stderr, "option %c missing arguments\n",
					optopt);
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
