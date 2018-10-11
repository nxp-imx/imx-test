/*
 * Copyright 2018 NXP
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

struct encoder_args {
	char *video_device;
	char *test_file;
	int width;
	int height;
	char *fmt;
	int fourcc;
};

struct pix_fmt_data {
	char name[10];
	char descr[80];
	int fourcc;
};

static struct pix_fmt_data fmt_data[] = {
	{
		.name	= "yuv420",
		.descr	= "2-planes, Y and UV-interleaved, same as NV12",
		.fourcc	= V4L2_PIX_FMT_NV12
	},
	{
		.name	= "yuv422",
		.descr	= "packed YUYV",
		.fourcc	= V4L2_PIX_FMT_YUYV
	},
	{
		.name	= "rgb24",
		.descr	= "packed RGB",
		.fourcc	= V4L2_PIX_FMT_RGB24
	},
	{
		.name	= "yuv444",
		.descr	= "packed YUV",
		.fourcc	= V4L2_PIX_FMT_YUV32
	},
	{
		.name	= "gray",
		.descr	= "Y8 or Y12 or Single Component",
		.fourcc	= V4L2_PIX_FMT_GREY
	},
	{
		.name	= "argb",
		.descr	= "packed ARGB",
		.fourcc	= V4L2_PIX_FMT_ARGB32
	},
};


void print_usage(char *str)
{
	int i;

	printf("Usage: %s -d </dev/videoX> -f <FILENAME.rgb> ", str);
	printf("-w <width> -h <height> ");
	printf("-p <pixel_format>\n");
	printf("Supported formats:\n");
	for (int i = 0; i < sizeof(fmt_data) / sizeof(*fmt_data); i++)
		printf("\t%8s: %s\n",
		       fmt_data[i].name,
		       fmt_data[i].descr);
}


int get_fourcc(char *fmt)
{
	int i;

	for (int i = 0; i < sizeof(fmt_data) / sizeof(*fmt_data); i++)
		if (strcmp(fmt_data[i].name, fmt) == 0)
			return fmt_data[i].fourcc;

	return 0;
}

int parse_args(int argc, char **argv, struct encoder_args *ea)
{
	int c;

	memset(ea, 0, sizeof(struct encoder_args));
	opterr = 0;
	while ((c = getopt(argc, argv, "+d:f:w:h:p:")) != -1)
		switch (c) {
		case 'd':
			ea->video_device = optarg;
			break;
		case 'f':
			ea->test_file = optarg;
			break;
		case 'w':
			ea->width = strtol(optarg, 0, 0);
			break;
		case 'h':
			ea->height = strtol(optarg, 0, 0);
			break;
		case 'p':
			ea->fourcc = get_fourcc(optarg);
			if (ea->fourcc == 0) {
				printf("Unsupported pixel format %s\n", optarg);
				print_usage(argv[0]);
				exit(1);
			}
			ea->fmt = optarg;
			break;
		case '?':
			if (optopt == 'c')
				fprintf(stderr,
					"Missing argument for option  -%c\n",
					optopt);
			else if (isprint(optopt))
				fprintf(stderr,
					"Unknown option `-%c'.\n", optopt);
			else
				fprintf(stderr,
					"Unknown option character `\\x%x'.\n",
					optopt);
			return 1;
		default:
			exit(1);
		}

	if (ea->video_device == 0 || ea->test_file == 0 ||
		ea->width == 0 || ea->height == 0 || ea->fmt == 0) {
		print_usage(argv[0]);
		exit(1);
	}
}
