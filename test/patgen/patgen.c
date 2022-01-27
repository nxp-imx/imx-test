/*
 * Copyright 2019-2022 NXP
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include <getopt.h>
#include <sys/param.h>

#include "bitmap.h"

#define DEFAULT_PATTERN         "font"
#define DEFAULT_EXTENSION       "rgb"
#define DEFAULT_OUTPUT_FORMAT   "bgra"
#define DEFAULT_HEIGHT           0
#define DEFAULT_WIDTH            0
#define DEFAULT_SIZE            "640x480"
#define DEFAULT_STRIDE           DEFAULT_WIDTH
#define DEFAULT_CHECKER          1
#define DEFAULT_ROTATION         0
#define DEFAULT_BORDER           0
#define DEFAULT_HEADER           0
#define DEFAULT_FOOTER           0
#define DEFAULT_DEBUG            0
#define DEFAULT_BODY_FONT_NAME   "body-font.ttf"
#define DEFAULT_HEADER_FONT_NAME "header-font.ttf"

#define DEFAULT_SEPARATOR_STRING \
"----------------------------------------------------------------\n"

#define MAX_PREFIX 128

typedef struct param_s {
	long w;
	long h;
	long stride;
	long checker_size;
	long steps;
	long rotation;

	double intensity;
	double min_intensity;
	double max_intensity;
	double alpha;

	int verbose;
	int debug;
	int header;
	int footer;
	int grid;
	int border;

	unsigned long color;
	unsigned long zero;
	unsigned long one;
	uint32_t pixel;

	char size[MAX_PREFIX];
	char pattern[MAX_PREFIX];
	char prefix[MAX_PREFIX];
	char out[MAX_PATH];
	char fb[MAX_PATH];
	char extension[16];
	char outformat[MAX_PREFIX];
	unsigned int o_fourcc;
	unsigned int bpc;
	unsigned int color_range;
	unsigned int color_space;

	/* char in[MAX_PATH]; */
	/* FILE *fin;*/
	FILE *fout;
	bitmap_t bm;
} param_t;

static const char help[] =

"Patgen Description\n\n"

"patgen is a simple pattern generator designed to build and run on Linux\n"
"platforms and embedded Linux platforms. Requires standard c library. To run\n"
"the default font needs to be a symbolically linked to the run directory.\n"
"GPL font, K1FS-1.2, is included as the default. \"DejaVuSansMono.ttf\" has\n"
"also been tested. The DejaVu font can be found in /usr/share/fonts/truetype\n"
"on yocto systems or in /usr/share/fonts/truetype/dejavu on ubuntu systems.\n\n"

"patgen is not intended to replace other GPU and VPU media tests. It is\n"
"intended to provide tools to isolate and debug display and camera issues.\n\n"

	"%s [-0 stuck zero 0xaarrbbgg][-1 stuck one 0xaarrbbgg]\n"
	"[-a alpha percent ] [-border] [-b, -bpc 1-8 bit per color]\n"
	"[-c xRGB pixel value 0xrrggbb] [-d] [-fb frame buffer device file]\n"
	"[-footer] [-grid] [-header] [-h, -help] [-i, -intensity percent]\n"
	"[-max_i max_intensity percent] [-min_i min_intensity percent]\n"
	"[-o outname name prefix] [-p pattern] [-pix_fmt pixel format]\n"
	"[-range range] [-steps steps for colorbar/graybar]\n"
	"[-r rotation 0,90,180,or 270 degrees] [-stride stride in pixels]\n"
	"[-v verbose mode] [-vs -vsize widthxheight pixels]\n"
	"\nusage:\n"
	"\t-0 -zero Force stuck zero [1-hot stuck zero pattern 0xaarrbbgg]\n\n"
	"\t-1 -one Force stuck one [1-hot stuck one patter 0xaarrbbgg]\n\n"
	"\t-a -alpha [alpha (%%)]\n\t\tSets the alpha value. Default is 100.0%%\n\n"
	"\t-border\n\t\tAdds a border to the pattern\n\n"
	"\t-b -bpc bits per color [bits per color 1-8]\n\n"
	"\t-c -color Default color[RGB color in hex 0xaarrbbgg]\n"
	"\t\tSets the color default for the fill and graybars\n\n"
	"\t-d -debug [level]\n\t\tSets the debug level\n\n"
	"\t-fb [frame buffer device] Name of the framebuffer device file\n\n"
	"\t-footer\n\t\tAdds footer text to the pattern border\n\n"
	"\t-grid Enables 10%%//5%% overlay grid\n\n"
	"\t-header\n\t\tAdds header text to the pattern border\n\n"
	"\t-h -help\n\t\tShows usage\n\n"
	"\t-i -intensity [intensity (%%)]\n"
	"\t\tSets the color intensity for the colorbar, fill, graybars,\n"
	"\t\tand hsv patterns. Default is 100.0%%\n\n"
	"\t-max_i[maximum intensity (%%)]\n"
	"\t\tSets the maximum intensity for the colorbar/graybar pattern.\n"
	"\t\t\tDefault is 100.0%%\n\n"
	"\t-min_i [minimum intensity (%%)] \n"
	"\t\tSets the minimum intensity for the colorbar/graybar pattern.\n"
	"\t\t\tDefault is 0.0%%\n\n"
	"\t-o -outname [prefix string]\n"
	"\t\tAdds a prefix to the default pattern name\n\n"
	"\t-p -pattern [pattern]\n\t\tSelect the type pattern to generate:\n"
	"\t\tchecker\t\tcreates a checkerboard pattern using -size\n\n"
	"\t\tcircle\t\tDraws circle with a background fill\n"
	"\t\tcolorbar\tVertical color bars\n"
	"\t\tcolorbar2\tAlternate colors bars\n"
	"\t\tcolorcheck\t24 color checker\n"
	"\t\tfill\t\tFill the pattern with -color at -intensity).\n"
	"\t\tfont\t\tPattern with some sample text from the fonts in use.\n"
	"\t\tgraybar\t\t11 shaded bars, the default is white bars from\n"
	"\t\t\t\t100%% to 0%%. The -color, -steps, -min_i, and max_i\n"
	"\t\t\t\tparameters can be used with this pattern.\n"
	"\t\tgradient\tCreates eight horizontal gradient bars: magenta, yellow,\n"
	"\t\t\t\tcyan, white, red, green, blue and white.\n"
	"\t\tvgradient\tCreates eight four vertical gradient bars: magenta, yellow,\n"
	"\t\t\t\tcyan, white, red, green, blue, and white.\n"
	"\t\thsv\t\tCreates an HSV color transition gradient. The\n"
	"\t\t\t\t-i sets the V (value) for HSV.\n"
	"\t\tshapes\t\tDraw test shapes with a background fill\n"
	"\t\tlogo\t\tDraw an NXP logo\n"
	"\t\ttest\t\tCreates a testcard like pattern.\n"
	"\t\twheel\t\tCreates an HSV color wheel. The -i sets the V\n"
	"\t\t\t\t(value) for HSV.\n\n"
	"\t\t16m_colors\tCreates an 4096x4096 color burst of 16\n"
	"\t\t\t\tmillion colors.\n\n"
	"\t-pix_fmt\n"
	"\t\tSupported formats are:\n"
	"\t\tbgra     32 bits per pixel RGB\n"
	"\t\trgb565le 16 bits per pixel RGB\n"
	"\t\tyuv420p  12 bits per pixel YUV (3 planes Y, U and V)\n"
	"\t\tyuv444p  24 bits per pixel YUV (3 planes Y, U and V)\n"
	"\t\tyuva444  32 bits per pixel YUV (1 plane  Y, U, V, and A YUVA)\n"
	"\t\tyuv444   24 bits per pixel YUV (1 plane  Y, U and V YUV)\n"
	"\t\tyuvj444p 24 bits per pixel YUV full color range (1 plane  Y, U and V YUV)\n"
	"\t\tyuvy422  16 bits per pixel YUV (1 plane  Y, U, Y, and V YUYV)\n"
	"\t\tnv12     12 bits per pixel YUV (2 planes Y and UV)\n\n"
	"\t-range - YUV color range\n"
	"\t\tSets the color range 0/1 = limited/full\n\n"
	"\t-r -rotation [rotation (degrees)] rotates the final image to 0, 90, 180, or 270\n\n"
	"\t-size  -checker_size [size (pixels)]\n\n"
	"\t-space - YUV color space\n"
	"\t\t 0 is bt.601\n\n"
	"\t-steps [steps]\n"
	"\t\tSets the number steps in the graybar pattern\n\n"
	"\t-stride [stride (pixels)] Sets the stride if it is larger the width\n\n"
	"\t\tSets the size of the checker board squares in pixels\n\n"
	"\t-v -verbose\n\t\tEchos the command parameters\n\n"
	"\t-vs -vsize [WxH (pixelsXlines)] Sets the width and height of the output.\n"
	"\t\tDefault is 640x480\n\n"
;

static void command_usage(char *argv0)
{
	fprintf(stderr, help, argv0);
}

enum {
	OPT_STRIDE = 0x1000,
	OPT_PIX_FMT,
	OPT_VSIZE,
	OPT_STEPS,
	OPT_MIN_I,
	OPT_MAX_I,
	OPT_HEADER,
	OPT_BORDER,
	OPT_FOOTER,
	OPT_SIZE,
	OPT_GRID,
	OPT_FB,
	OPT_COLOR_RANGE,
	OPT_COLOR_SPACE,
};

static struct option long_options[] =
{
	{ "alpha",   required_argument,       0, 'a' },
	{ "bpc",     required_argument,       0, 'b' },
	{ "color",   required_argument,       0, 'c' },
	{ "debug",   required_argument,       0, 'd' },
	{ "help",    no_argument,             0, 'h' },
	{ "intensity", required_argument,     0, 'i' },
	{ "outname", required_argument,       0, 'o' },
	{ "pattern", required_argument,       0, 'p' },
	{ "rotation", required_argument,      0, 'r' },
	{ "verbose", no_argument,             0, 'v' },
	{ "one",     required_argument,       0, '1' },
	{ "zero",    required_argument,       0, '0' },
	{ "border",  no_argument,             0, OPT_BORDER },
	{ "footer",  no_argument,             0, OPT_FOOTER },
	{ "grid",    no_argument,             0, OPT_GRID },
	{ "header",  no_argument,             0, OPT_HEADER },
	{ "min_i",   required_argument,       0, OPT_MIN_I },
	{ "max_i",   required_argument,       0, OPT_MAX_I },
	{ "steps",   required_argument,       0, OPT_STEPS },
	{ "size",    required_argument,       0, OPT_SIZE },
	{ "vsize",   required_argument,       0, OPT_VSIZE },
	{ "vs",      required_argument,       0, OPT_VSIZE },
	{ "stride",  required_argument,       0, OPT_STRIDE /*'s'*/ },
	{ "pix_fmt", required_argument,       0, OPT_PIX_FMT },
	{ "fb",      required_argument,       0, OPT_FB },
	{ "range",   required_argument,       0, OPT_COLOR_RANGE },
	{ "space",   required_argument,       0, OPT_COLOR_SPACE },
	{ 0, 0, 0, 0 }
};

static int command_parse(int argc, char **argv, param_t *p)
{
	int index;
	int c;

	opterr = 0;

	if  (argc < 2) {
		command_usage(argv[0]);
		/*exit(1);*/
	}

	while (1) {
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long_only(argc, argv, "a:b:c:d:hi:o:p:r:v1:0:",
				     long_options, &option_index);
		if (p->verbose)
			fprintf(stderr, "c %d, option_index %d\n",
				c, option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c) {
		case 'a':
			p->alpha = strtod(optarg, NULL);
			if (p->verbose)
				fprintf(stderr, "-a alpha =     %-3.3f\n",
					p->alpha);
			break;
		case 'b':
			p->bpc = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-b bits per color = %d\n",
					p->bpc);
			break;
		case OPT_BORDER:
			p->border = 1;
			if (p->verbose)
				fprintf(stderr, "-b -border =    %d\n",
					p->border);
			break;
		case 'c':
			p->color = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-c -color =     0x%08lx\n",
					p->color);
			break;
		case OPT_COLOR_RANGE:
			p->color_range = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-range =     %d\n",
					p->color_range);
			break;
		case OPT_COLOR_SPACE:
			p->color_space = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-space =     %d\n",
					p->color_range);
			break;
		case '0':
			p->zero = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-0 -zero =     0x%08lx\n",
					p->zero);
			break;
		case '1':
			p->color = strtoul(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-1 -one =     0x%08lx\n",
					p->one);
			break;
		case 'd':
#ifndef DEBUG
			fprintf(stderr,
				"\n\nWarning: not built for debug configuraiton.\n"
				"debug option has not effect.\n\n\n");
#endif
			if (optarg)
				p->debug = strtol(optarg, NULL, 0);
			else
				p->debug = 1;
			if (p->verbose)
				fprintf(stderr, "-d -debug =     %d\n",
					p->debug);
			break;
		case OPT_FOOTER:
			p->footer = 1;
			if (p->verbose)
				fprintf(stderr, "-f -footer =    %d\n",
					p->footer);
			break;
		case OPT_GRID:
			p->grid = 1;
			if (p->verbose)
				fprintf(stderr, "-grid =        %d\n",
					p->grid);
			break;
		case 'i':
			p->intensity = strtod(optarg, NULL);
			if (p->verbose)
				fprintf(stderr, "-i -intensity = %-3.3f\n",
					p->intensity);
			break;
		case OPT_SIZE:
			p->checker_size = strtol(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-size = %lu\n",
					p->checker_size);
			break;
		case  OPT_MIN_I:
			p->min_intensity = strtod(optarg, NULL);
			if (p->verbose)
				fprintf(stderr, "min_i = %-3.3f\n",
					p->min_intensity);
			break;
		case  OPT_MAX_I:
			p->max_intensity = strtod(optarg, NULL);
			if (p->verbose)
				fprintf(stderr, "-max_i = %-3.3f\n",
					p->max_intensity);
			break;
		case OPT_STEPS:
			p->steps = strtol(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr,
					"-steps = %lu\n", p->steps);
			break;
		case 'o':
			snprintf(p->prefix, sizeof(p->prefix), "%s", optarg);
			if (p->verbose)
				fprintf(stderr, "-o =   %s\n",
					p->prefix);
			break;
		case OPT_FB:
			snprintf(p->fb, sizeof(p->fb), "%s", optarg);
			if (p->verbose)
				fprintf(stderr, "-fb =   %s\n", p->fb);
			break;
		case 'p':
			snprintf(p->pattern, sizeof(p->pattern), "%s", optarg);
			if (p->verbose)
				fprintf(stderr, "-p -pattern =   %s\n",
					p->pattern);
			break;
		case OPT_VSIZE:
			snprintf(p->size, sizeof(p->size), "%s", optarg);
			if (p->verbose)
				fprintf(stderr, "-vs -video_size =   %s\n",
					p->size);
			break;
		case OPT_STRIDE:
			p->stride = strtol(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-stride =   %lu\n",
					p->stride);
			break;
		case 'r':
			p->rotation = strtol(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-r -rotation =   %lu\n",
					p->rotation);
			break;
		case OPT_HEADER:
			p->header = 1;
			if (p->verbose)
				fprintf(stderr, "-t --header =    %d\n",
					p->header);
			break;
		case 'h':
			if (p->verbose)
				fprintf(stderr, "-h -help is set\n");
			command_usage(argv[0]);
			exit(0);
			break;
		case 'v':
			p->verbose = 1;
			fprintf(stderr, "command line args:\n");
			fprintf(stderr, "-v -verbose is set\n");
			break;
		case 'w':
			p->w = strtol(optarg, NULL, 0);
			if (p->verbose)
				fprintf(stderr, "-w --width =     %lu\n",
					p->w);
			break;
		case OPT_PIX_FMT:
			snprintf(p->outformat, sizeof(p->outformat), "%s", optarg);
			if (p->verbose)
				fprintf(stderr, "-pix_fmt =      %s\n",
					p->outformat);
			break;
		case '?':
			break;
		default:
			fprintf(stderr, "Default case:\n");
			command_usage(argv[0]);
			return 1;
		}
	}

	for (index = optind; index < argc; index++) {
		command_usage(argv[0]);
		fprintf(stderr, "Non-option argument %s\n", argv[index]);
	}

	return 0;
}

static char* basename(char const *path)
{
	char *s = strrchr(path, '/');
	if (!s)
		return strdup(path);
	else
		return strdup(s + 1);
}

static void set_file_name(param_t *p)
{
	long h, w;
	if (p->rotation == 90 || p->rotation == 270) {
		h = p->w;
		w = p->h;
	} else {
		w = p->w;
		h = p->h;
	}
	if (strnlen(p->fb, MAX_PATH) > 0)
		strncpy(p->out, p->fb, MAX_PATH);
	else if (strnlen(p->prefix, MAX_PREFIX) > 0)
		snprintf(p->out, MAX_PATH,
			 "%s-%s-%ldx%ld-%s.%s",
			 p->prefix, p->pattern, w, h,
			 p->outformat, p->extension);
	else
		snprintf(p->out, MAX_PATH,
			 "%s-%ldx%ld-%s.%s",
			 p->pattern, w, h,
			 p->outformat, p->extension);
}

static void set_params(param_t *p)
{
	uint32_t c;

	bitmap_get_color(&p->bm, "white", &c);
	p->color = c;
	p->color_space = 0;
	p->color_range = 0;
	p->zero = 0;
	p->one = 0;
	p->pixel = p->color;
	p->w = DEFAULT_WIDTH;
	p->h = DEFAULT_HEIGHT;
	p->stride = DEFAULT_STRIDE;
	p->checker_size = DEFAULT_CHECKER;
	p->rotation = DEFAULT_ROTATION;
	p->steps = -1;
	p->prefix[0] = 0;
	p->debug = DEFAULT_DEBUG;
	p->verbose = 0;
	p->header = DEFAULT_HEADER;
	p->footer = DEFAULT_FOOTER;
	p->grid = 0;
	p->border = DEFAULT_BORDER;
	p->intensity = 100.0;
	p->min_intensity = 0.0;
	p->max_intensity = 100.0;
	p->alpha = 100.0;
	p->bpc = 8;
	p->o_fourcc = FORMAT_BGRA8888; /* bgra */
	strcpy(p->pattern, DEFAULT_PATTERN);
	strcpy(p->outformat, DEFAULT_OUTPUT_FORMAT);
	strcpy(p->extension, DEFAULT_EXTENSION);
	strcpy(p->size, DEFAULT_SIZE);
	strcpy(p->fb, "");
}

static void update_params(param_t *p)
{
	int n;

	fprintf(stderr, "Updating parameters from command line\n");
	p->pixel = p->color & 0xffffff;
	p->pixel = bitmap_set_intensity(&p->bm, p->pixel, p->intensity);
	p->pixel = bitmap_set_alpha(&p->bm, p->pixel, p->alpha);

	if (strlen(p->size) > 0) {
		n = sscanf(p->size, "%lux%lu", &p->w, &p->h);
		if (n < 2) {
			fprintf(stderr, "\nFailed to parse video_size");
			exit(1);
		}
	}
	if ((p->w == 0) || (p->h == 0)) {
		fprintf(stderr, "\nHeight or Width cannot be zero!");
		exit(1);
	}
	if ((p->w > MAX_WIDTH)) {
		fprintf(stderr, "\nWidth cannot be greater than %u ",
			MAX_WIDTH);
		exit(1);
	}
	if (p->h > MAX_HEIGHT) {
		fprintf(stderr, "\nHeight cannot be greater than %u ",
			MAX_HEIGHT);
		exit(1);
	}

	if (p->stride < p->w)
		p->stride = p->w;

	bitmap_set_color_range(&p->bm, p->color_range);
	bitmap_set_color_space(&p->bm, p->color_space);

	if (strncmp("rgb565le", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_BGR565;
	} else if (strncmp("yuv444p", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUV444P;
		strcpy(p->extension, "yuv");
	} else if (strncmp("yuva444", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUVA444;
		strcpy(p->extension, "yuv");
	} else if (strncmp("yuv444", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUV444;
		strcpy(p->extension, "yuv");
	} else if (strncmp("yuvj444p", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUV444P;
		bitmap_set_color_range(&p->bm, TRUE);
		strcpy(p->extension, "yuv");
	} else if (strncmp("yuyv422", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUYV422;
		strcpy(p->extension, "yuv");
	} else if (strncmp("nv12", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_NV12;
		strcpy(p->extension, "yuv");
	} else if (strncmp("yuv420p", p->outformat, 8) == 0) {
		p->o_fourcc = FORMAT_YUV420;
		strcpy(p->extension, "yuv");
	} else if (strncmp("bgra", p->outformat, 4) == 0) {
		p->o_fourcc = FORMAT_BGRA8888;
	} else {
		fprintf(stderr,
			"\nUnsupported format. Use one of the following:\n"
			"\tbgra - default\n"
			"\trgb565le\n"
			"\tyuv420p\n"
			"\tyuv444p\n"
			"\tyuvj444p\n"
			"\tyuva444\n"
			"\tyuv444\n"
			"\tyuyv422\n"
			"\tnv12\n\n"
			);
		exit(0);
	}
	/* default is bgra */
	set_file_name(p);
}

static void show_params(param_t *p)
{
	uint32_t temp_alpha;

	fprintf(stderr, DEFAULT_SEPARATOR_STRING);
	fprintf(stderr, "intensity:               %8.3f\n", p->intensity);
	fprintf(stderr, "min_intensity:           %8.3f\n", p->min_intensity);
	fprintf(stderr, "max_intensity:           %8.3f\n", p->max_intensity);

	temp_alpha =  (uint32_t)round(p->alpha * (double)MAX_RGBA / 100.0);

	fprintf(stderr, "alpha:                   %8.3f (%u)\n",
		p->alpha, temp_alpha);
	fprintf(stderr, "fill color:            0x%08lx\n", p->color);
	fprintf(stderr, "shaded color w/ alpha: 0x%08x\n", p->pixel);
	fprintf(stderr, "width:                   %8ld\n", p->w);
	fprintf(stderr, "height:                  %8ld\n", p->h);
	fprintf(stderr, "stride:                  %8ld\n", p->stride);
	fprintf(stderr, "rotation:                %8ld\n", p->rotation);
	fprintf(stderr, "checker_size:            %8ld\n", p->checker_size);
	fprintf(stderr, "steps:                   %8ld\n", p->steps);
	fprintf(stderr, "verbose:                 %8d\n", p->verbose);
	fprintf(stderr, "debug:                   %8d\n", p->debug);
	fprintf(stderr, "header:                  %8d\n", p->header);
	fprintf(stderr, "footer:                  %8d\n", p->footer);
	fprintf(stderr, "border:                  %8d\n", p->border);
	fprintf(stderr, "color_space:             %8d\n", p->color_space);
	fprintf(stderr, "color_range:             %8d\n", p->color_range);
	fprintf(stderr, "grid:                    %8d\n", p->grid);
	fprintf(stderr, "size:                           %s\n", p->size);
	fprintf(stderr, "pattern:                        %s\n", p->pattern);
	fprintf(stderr, "pattern file:                   %s\n", p->out);
	fprintf(stderr, "frame buffer device:            %s\n", p->fb);
	fprintf(stderr, "format:                         %s\n", p->outformat);
}

static int get_margin_size(param_t *param)
{
	int s = MIN(param->w, param->h);
	if (s <= 480)
		return 16;
	if (s < 1080)
		return 32;
	if (s < 4096)
		return 64;
	/* if s >= 4096 */
	return 256;
}

static int generate_borders(param_t *param, int *margin, char *title)
{
	int t, b, l, m, r;
	uint32_t color;
	uint32_t bw[2];
	char text[96];

	fprintf(stderr, "Generating borders\n");

	bitmap_get_color(&param->bm, "dark_gray", &color);
	if (param->border == 0) {
		*margin = 0;
		/* fill center */
		bitmap_fill_rectangle(&param->bm, 0, 0,
				      param->w - 1, param->h - 1, color);
		return 0;
	}

	m = get_margin_size(param);
	*margin = m;

	m = *margin;
	t = m;
	l = m;
	r = param->w - m;
	b = param->h - m;

	bitmap_get_color(&param->bm, "black", &bw[0]);
	bitmap_get_color(&param->bm, "white", &bw[1]);

	/* fill with checker board */
	bitmap_checker(&param->bm, 0, 0, param->w, param->h, bw, 1);

	/* fill center */
	bitmap_fill_rectangle(&param->bm, l - m / 4, t - m / 4,
			      r + m / 4, b + m / 4, color);

	bitmap_corners(&param->bm, m);

	if (param->header != 0) {
		if (title != NULL && (strncmp("none", title, 4) != 0)) {
			fprintf(stderr, "Generating header\n");

			if (strncmp("default", title, 7) == 0)
				snprintf(text, sizeof(text), "%ld x %ld",
					 param->w, param->h);
			else
				strncpy(text, title, sizeof(text));
			t = m / 8;
			l = param->w / 4;
			r = param->w * 3 / 4;
			b = m - m / 4 - 2;
			bitmap_fill_rectangle(&param->bm, l, t, r, b, color);

			bitmap_render_font(&param->bm,
					   DEFAULT_HEADER_FONT_NAME,
					   text,
					   m / 3,
					   param->w / 2,
					   m / 2,
					   bw[1]);
		}
	}

	if (param->footer != 0) {
		int ret;
		fprintf(stderr, "Generating footer\n");
		if (param->w <= 640)
			ret = snprintf(text, sizeof(text), "%s",
				       param->pattern);
		else
			ret = snprintf(text, sizeof(text),
				       "pattern: %s | file: %s",
				       param->pattern, basename(param->out));

		/* needed to prevent compiler warning */
		if (ret < 0)
			fprintf(stderr, "Warning footer string was truncated!");

		t =  param->h - m * 3 / 4 + 2;
		l = param->w / 8;
		r = param->w * 7 / 8;
		b = param->h - 2;

		bitmap_fill_rectangle(&param->bm, l, t, r, b, color);

		bitmap_render_font(&param->bm,
				   DEFAULT_HEADER_FONT_NAME,
				   text,
				   m / 2,
				   param->w / 2,
				   b - m / 2 + 6,
				   bw[1]);
	}
	return 0;
}

int generate_test_lines(param_t *param,
			int x0, int y0,
			int x1, int y1,
			uint32_t *colors) /* size is two element */
{
	int i, t, b, l, r, w;
	const int num_blocks = 8;
	int loop_data[8][2] =  {
		{ 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 },
		{ 4, 1 }, { 3, 1 }, { 2, 1 }, { 1, 1 },
	};
	fprintf(stderr, "Generating test lines\n");

	t = y0;
	b = y1;
	w = (x1 - x0) / 8;
	l = x0;
	r = x0 + w;

	for (i =  0; i < num_blocks; i++,  l += w, r += w) {
		bitmap_lines(&param->bm, l, t, r, b, colors,
			     loop_data[i][0],
			     loop_data[i][1]);
	}
	return 0;
}

static int generate_test_circles(param_t *param, int x, int y, int r)
{
	uint32_t gw[2];

	fprintf(stderr, "Generating test circles\n");

	bitmap_get_color(&param->bm, "dark_gray", &gw[0]);
	bitmap_get_color(&param->bm, "white", &gw[1]);

	/* todo use loop here */
	if (r < 4096) {
		bitmap_fill_circle2(&param->bm, x, y, r+2, r+3, gw[0]);
		bitmap_fill_circle2(&param->bm, x, y, r-3, r-2, gw[0]);
		bitmap_fill_circle2(&param->bm, x, y, r-2, r+2, gw[1]);
	} else {
		bitmap_fill_circle2(&param->bm, x, y, r+5, r+7, gw[0]);
		bitmap_fill_circle2(&param->bm, x, y, r-7, r+5, gw[0]);
		bitmap_fill_circle2(&param->bm, x, y, r-5, r+5, gw[1]);
	}
	return 0;
}

static int generate_test_center(param_t *param,
				int x0, int y0,
				int x1, int y1)
{
	int t, b, l, m, r, s;
	char text[64];
	float ftemp;
	uint32_t bw[2];

	fprintf(stderr, "Generating test center\n");

	t = y0;
	b = y1;
	l = x0;
	r = x1;

	ftemp = 0.02 * MIN(param->w,param->h);
	m = round(ftemp);

	bitmap_get_color(&param->bm, "black", &bw[0]);
	bitmap_get_color(&param->bm, "white", &bw[1]);

	/* fill center */
	bitmap_fill_rectangle(&param->bm, l, t, r, b, bw[1]);
	bitmap_fill_rectangle(&param->bm, l + m, t + m, r - m, b - m, bw[0]);

	snprintf(text, sizeof(text), "%ld x %ld",  param->w, param->h);
	s = round(MIN(param->w, param->h) * 0.1); /* font size */

	bitmap_render_font(&param->bm,
			   DEFAULT_BODY_FONT_NAME,
			   text,
			   s,
			   param->w / 2,
			   (param->h / 2) - (s / 8),
			   bw[1]);

	return 0;
}

static int generate_colorbar(param_t *param, int m)
{
	int i = 0, num_colors;
	uint32_t colors[16];

	bitmap_get_color(&param->bm, "white",   &colors[i++]);
	bitmap_get_color(&param->bm, "yellow",  &colors[i++]);
	bitmap_get_color(&param->bm, "cyan",    &colors[i++]);
	bitmap_get_color(&param->bm, "green",   &colors[i++]);
	bitmap_get_color(&param->bm, "magenta", &colors[i++]);
	bitmap_get_color(&param->bm, "red",     &colors[i++]);
	bitmap_get_color(&param->bm, "blue",    &colors[i++]);
	bitmap_get_color(&param->bm, "black",   &colors[i++]);
	num_colors = i;

	for (i = 0; i <  num_colors; i++) {
		colors[i] = bitmap_set_intensity(&param->bm, colors[i],
						 param->intensity);
		colors[i] = bitmap_set_alpha(&param->bm, colors[i],
					     param->alpha);
	}

	bitmap_hbars(&param->bm, m, m, param->w - m, param->h - m,
		     colors, num_colors);

	return 0;
}

static int generate_colorbar2(param_t *param, int m)
{
	int s, l, r, t, b, i = 0, num_colors;
	uint32_t colors[16];

	bitmap_get_color(&param->bm, "white",   &colors[i++]);
	bitmap_get_color(&param->bm, "yellow",  &colors[i++]);
	bitmap_get_color(&param->bm, "cyan",    &colors[i++]);
	bitmap_get_color(&param->bm, "green",   &colors[i++]);
	bitmap_get_color(&param->bm, "magenta", &colors[i++]);
	bitmap_get_color(&param->bm, "red",     &colors[i++]);
	bitmap_get_color(&param->bm, "blue",    &colors[i++]);

	num_colors = i;

	for (i = 0; i <  num_colors; i++) {
		colors[i] = bitmap_set_intensity(&param->bm, colors[i],
						 param->intensity);
		colors[i] = bitmap_set_alpha(&param->bm, colors[i],
					     param->alpha);
	}

	s =  (param->h - 2 * m) * 2 / 3;
	t = m;
	l = m;
	b = t + s;
	r = param->w - m;

	bitmap_hbars(&param->bm, l, t, r, b, colors, num_colors);

	i = 0;
	bitmap_get_color(&param->bm, "blue",    &colors[i++]);
	bitmap_get_color(&param->bm, "magenta", &colors[i++]);
	bitmap_get_color(&param->bm, "yellow",  &colors[i++]);
	bitmap_get_color(&param->bm, "red",     &colors[i++]);
	bitmap_get_color(&param->bm, "cyan",    &colors[i++]);
	bitmap_get_color(&param->bm, "black",   &colors[i++]);
	bitmap_get_color(&param->bm, "white",   &colors[i++]);
	num_colors = i;

	/* row of gradients */
	s =  (param->h - 2 * m) * 90 / 1080;
	t = b;
	l = m;
	b = t + s;
	r = param->w - m;
	bitmap_hbars(&param->bm, l, t, r, b, colors, num_colors);

	s =  (param->h - 2 * m) * 125 / 1000;
	t = b;
	l = m;
	b = t + s;
	r = (param->w - m) * 1150 / 1920;

	bitmap_gradient(&param->bm, l, t, r, b,
			255,
			255,
			255,
			0,
			0,
			0,
			0);

	l = r;
	r = (param->w - m);
	bitmap_hsv_rectangle(&param->bm, l, t, r, b, b - t, 100.0, 300.0);

	/* bottom row  */
	s =  (param->h - 2 * m) * 125 / 1000;
	t = b;
	l = m;
	b = param->h - m;
	r = (param->w - 2 * m) * 4 / 7;
	num_colors = 11;

	for (i = 0; i <  num_colors; i++) {
		colors[i] = bitmap_set_intensity(&param->bm, param->color,
						 i * param->intensity / 10.0);
		colors[i] = bitmap_set_alpha(&param->bm, colors[i],
					     param->alpha);
	}

	bitmap_hbars(&param->bm, l, t, r, b, colors, num_colors);

	l = r;
	r = param->w - m;

	bitmap_get_color(&param->bm, "black", &colors[0]);
	bitmap_fill_rectangle(&param->bm, l, t, r, b, colors[0]);

	return 0;
}

static int generate_graybar(param_t *param, int m)
{
	int i = 0, num_colors = 11;
	uint32_t colors[MAX_RGBA + 1];

	double brightness;

	/* todo: clamp the range here and warn */
	if ((param->steps > 0) &&  (param->steps  <= MAX_RGBA)) {
		num_colors = param->steps;
	}

	for (i = 0; i <  num_colors; i++) {
		brightness =
			param->min_intensity +  (double)i *
			(param->max_intensity - param->min_intensity) /
			(double)(num_colors - 1);
		colors[i] = bitmap_set_intensity(&param->bm, param->color,
						 brightness);
		colors[i] = bitmap_set_alpha(&param->bm, colors[i],
					     param->alpha);
		fprintf(stderr,
			"%s(): min %f max %f brightness %f pixel 0x%08x\n",
			__func__, param->min_intensity, param->max_intensity,
			brightness, colors[i]);
	}

	bitmap_hbars(&param->bm, m, m, param->w - m, param->h - m,
		     colors, num_colors);

	return 0;
}

static int generate_fill(param_t *param, int m)
{
	bitmap_fill_rectangle(&param->bm,
			      m, m,
			      param->w - m, param->h - m,
			      param->pixel);

	return 0;
}

/* reference https://en.wikipedia.org/wiki/ColorChecker */
#define COLOR_CHECKER_ROW 4
#define COLOR_CHECKER_COLUMN 6
const uint32_t colorchecker[COLOR_CHECKER_ROW * COLOR_CHECKER_COLUMN] = {
	0x00735244, /* Dark skin     */
	0x00c29682, /* Light skin    */
	0x00627a9d, /* Blue sky      */
	0x00576c43, /* Foliage       */
	0x008580b1, /* Blue flower   */
	0x0067bdaa, /* Bluish green  */

	0x00d67e2c, /* Orange        */
	0x00505ba6, /* Purplish blue */
	0x00c15a63, /* Moderate red  */
	0x005e3c6c, /* Purple        */
	0x009dbc40, /* Yellow green  */
	0x00e0a32e, /* Orange yellow */

	0x00383d96, /* Blue          */
	0x00469449, /* Green         */
	0x00af363c, /* Red           */
	0x00e7c71f, /* Yellow        */
	0x00bb5695, /* Magenta       */
	0x000885a1, /* Cyan          */

	0x00f3f3f2, /* White         */
	0x00c8c8c8, /* Neutral       */
	0x00a0a0a0, /* Neutral       */
	0x007a7a79, /* Neutral       */
	0x00555555, /* Neutral       */
	0x00343434, /* Black         */
};

static int generate_colorcheck(param_t *param, int m)
{
	int l, b, s, t, w, h, x, y;
	uint32_t c;
	/* determine boarder and separator */
	b = MIN(param->w, param->h) / 16;
	s = b / 4;

	/* start position */
	l = m + b;
	t = m + b;

	/* calculate the width and heigth of each rectangle */
	w = (param->w - (2 * m) - (2 * b) - (5 * s)) / 6;
	h = (param->h - (2 * m) - (2 * b) - (3 * s)) / 4;

	/* fill whole bitmap surface */
	bitmap_fill_rectangle(&param->bm,
			      m, m,
			      param->w - m, param->h - m,
			      0x0);

	for (y = 0; y < COLOR_CHECKER_ROW; y++) {
		for (x = 0; x < COLOR_CHECKER_COLUMN; x++) {
			c = colorchecker[(y * COLOR_CHECKER_COLUMN) + x];
			bitmap_fill_rectangle(&param->bm,
					      l + (x * (w + s)),
					      t + (y * (h + s)),
					      l + (x * (w + s)) + w,
					      t + (y * (h + s)) + h,
					      c);
		}
	}
	return 0;
}

static int generate_circle(param_t *param, int m)
{
	int r, h, w, l, t, b, radius;

	fprintf(stderr, "Generating circle\n");

	l = m;
	t = m;
	r = param->w - m;
	b = param->h - m;
	w = r - l;
	h = b - t;

	bitmap_fill_rectangle(&param->bm, l, t, r, b, param->pixel);

	radius = round(MIN(h, w) * 0.40);

	l = w / 2 + m;
	t = h / 2 + m;
	generate_test_circles(param, l, t, radius);

	return 0;
}

static int generate_nxp_logo(param_t *param, int m)
{
	int r, l, t, b, dx, dy;
	const int min_w = 768, min_h = 512;

	if ((param->h < min_h) || (param->w < min_w)) {
		fprintf(stderr,
			"Size is too small for logo pattern. Needs to be  > %d width and  > %d height.\n",
			min_w, min_h);
		return 0;
	}

	dx = 80;
	dy = 40;

	point s1[] = {
		{dx +  65, dy + 142},
		{dx + 130, dy + 142},
		{dx + 240, dy + 270},
		{dx + 240, dy + 142},
		{dx + 304, dy + 142},//{304, 247},
		{dx + 304, dy + 353},
		{dx + 240, dy + 353},
		{dx + 130, dy + 226},
		{dx + 130, dy + 353},
		{dx + 65,  dy + 353},
	};
	point s2[] = {
		{dx + 241, dy + 142},
		{dx + 304, dy + 142},
		{dx + 304, dy + 246},
	};
	point s3[] = {
		{dx + 304, dy + 246},
		{dx + 304, dy + 353},
		{dx + 240, dy + 353},
	};

	point s4[] = {
		{dx + 304, dy + 142},
		{dx + 316, dy + 142},
		{dx + 361, dy + 212},
		{dx + 405, dy + 142},
		{dx + 416, dy + 142},//{304, 247},
		{dx + 416, dy + 353},
		{dx + 405, dy + 353},
		{dx + 361, dy + 283},
		{dx + 316, dy + 353},
		{dx + 304, dy + 353},
	};

	point s5[] = {
		{dx + 416, dy + 142},
		{dx + 585, dy + 142},
		{dx + 585, dy + 306},
		{dx + 481, dy + 306},
		{dx + 481, dy + 353},
		{dx + 416, dy + 353},

	};

	point s6[] = {
		{dx + 416, dy + 142},
		{dx + 481, dy + 142},
		{dx + 416, dy + 246},
	};

	point s7[] = {
		{dx + 416, dy + 246},
		{dx + 481, dy + 353},
		{dx + 416, dy + 353},
	};

	fprintf(stderr, "Generating NXP logo\n");

	l = m;
	t = m;
	r = param->w - m;
	b = param->h - m;

	bitmap_fill_rectangle(&param->bm, l, t, r, b, param->pixel);

	bitmap_fill_polygon(&param->bm, s1, 10, 0xf9b500);
	bitmap_fill_polygon(&param->bm, s2, 3, 0x958437);
	bitmap_fill_polygon(&param->bm, s3, 3, 0x958437);
	bitmap_fill_polygon(&param->bm, s4, 10, 0x7bb1da);

	bitmap_fill_polygon(&param->bm, s5, 6, 0xc9d200);

	bitmap_fill_circle(&param->bm, dx + 594, dy +198, 55, 0xc9d200);
	bitmap_fill_circle(&param->bm, dx + 594, dy +252, 55, 0xc9d200);
	bitmap_fill_rectangle(&param->bm, dx + 590, dy + 192, dx + 650, dy + 257,  0xc9d200);

	bitmap_fill_rectangle(&param->bm, dx + 482, dy + 195, dx + 572, dy + 258,  0xffffff);
	bitmap_fill_circle(&param->bm, dx + 572, dy + 223, 27, 0xffffff);
	bitmap_fill_circle(&param->bm, dx + 572, dy + 230, 27, 0xffffff);

	bitmap_fill_polygon(&param->bm, s6, 3, 0x739833);
	bitmap_fill_polygon(&param->bm, s7, 3, 0x739833);

	return 0;
}

static int generate_shapes(param_t *param, int m)
{
	int r, h, w, l, t, b, i = 0;
	point poly[16];
	uint32_t colors[16];
	const int min_w = 1536, min_h = 1024;

	if ((param->h < min_h) || (param->w < min_w)) {
		fprintf(stderr,
			"Size is too small for shapes pattern. Needs to be  > %d width and  > %d height.\n",
			min_w, min_h);
		return 0;
	}

	bitmap_get_color(&param->bm, "white",    &colors[i++]);
	bitmap_get_color(&param->bm, "yellow",   &colors[i++]);
	bitmap_get_color(&param->bm, "cyan",     &colors[i++]);
	bitmap_get_color(&param->bm, "green",    &colors[i++]);
	bitmap_get_color(&param->bm, "magenta",  &colors[i++]);
	bitmap_get_color(&param->bm, "red",      &colors[i++]);
	bitmap_get_color(&param->bm, "blue",     &colors[i++]);
	bitmap_get_color(&param->bm, "black",    &colors[i++]);
	//bitmap_get_color(&param->bm, "white",    &colors[i++]);
	bitmap_get_color(&param->bm, "dark_gray",&colors[i++]);

	fprintf(stderr, "Generating shapes\n");

	l = m;
	t = m;
	r = param->w - m;
	b = param->h - m;
	w = r - l;
	h = b - t;

	bitmap_fill_rectangle(&param->bm, l, t, r, b, param->pixel);

	l = w / 2 + m;
	t = h / 2 + m;

	bitmap_draw_line2(&param->bm,
			  100, 100, //int x0, int y0,
			  150, 300, //int x1, int y1,
			  3,
			  colors[1]);

	bitmap_draw_line2(&param->bm,
			  200, 100, //int x0, int y0,
			  300, 200, //int x1, int y1,
			  3,
			  colors[1]);

	bitmap_draw_line2(&param->bm,
			  100, 200, //int x0, int y0,
			  300, 220, //int x1, int y1,
			  3,
			  colors[2]);

	bitmap_fill_circle2(&param->bm, 400, 300,
			       30, 60, colors[3]);

	bitmap_fill_circle2(&param->bm, 900, 500,
			       50, 100, colors[4]);


	/* roygbiv  from color checker */
	bitmap_draw_arc(&param->bm, 1200, 300,
			 100, 120,
			 DEG2RAD(0), DEG2RAD(180),
			 0x00af363c);
	bitmap_draw_arc(&param->bm, 1200, 300,
			 120, 140,
			 DEG2RAD(0), DEG2RAD(180),
			 0x00d67e2c);
	bitmap_draw_arc(&param->bm, 1200, 300,
			 140, 160,
			 DEG2RAD(0), DEG2RAD(180),
			 0x00e7c71f);
	bitmap_draw_arc(&param->bm, 1200, 300,
			  160, 180,
			  DEG2RAD(0), DEG2RAD(180),
			  0x00469449);
	bitmap_draw_arc(&param->bm, 1200, 300,
			 180, 200,
			 DEG2RAD(0), DEG2RAD(180),
			 0x00627a9d);
	bitmap_draw_arc(&param->bm, 1200, 300,
			  200, 220,
			  DEG2RAD(0), DEG2RAD(180),
			  0x00505ba6);
	bitmap_draw_arc(&param->bm, 1200, 300,
			  220, 240,
			  DEG2RAD(0), DEG2RAD(180),
			  0x00383d96);
	poly[0].x = 100;
	poly[0].y = 800;
	poly[1].x = 150;
	poly[1].y = 800;
	poly[2].x = 100;
	poly[2].y = 900;
	bitmap_fill_polygon(&param->bm, poly, 3, colors[5]);

	poly[0].x = 200;
	poly[0].y = 800;
	poly[1].x = 300;
	poly[1].y = 800;
	poly[2].x = 300;
	poly[2].y = 900;
	poly[3].x = 200;
	poly[3].y = 900;
	bitmap_fill_polygon(&param->bm, poly, 4, colors[6]);

	poly[0].x = 400;
	poly[0].y = 800;
	poly[1].x = 450;
	poly[1].y = 750;
	poly[2].x = 500;
	poly[2].y = 800;
	poly[3].x = 500;
	poly[3].y = 900;
	poly[4].x = 400;
	poly[4].y = 900;
	bitmap_fill_polygon(&param->bm, poly, 5, colors[7]);

	poly[0].x = 600;
	poly[0].y = 800;
	poly[1].x = 650;
	poly[1].y = 750;
	poly[2].x = 700;
	poly[2].y = 800;
	poly[3].x = 700;
	poly[3].y = 900;
	poly[4].x = 650;
	poly[4].y = 950;
	poly[5].x = 600;
	poly[5].y = 900;

	bitmap_fill_polygon(&param->bm, poly, 6, colors[8]);

	poly[0].x = 800;
	poly[0].y = 800;

	poly[1].x = 825;
	poly[1].y = 700;

	poly[2].x = 850;
	poly[2].y = 800;

	poly[3].x = 950;
	poly[3].y = 825;

	poly[4].x = 850;
	poly[4].y = 850;

	poly[5].x = 825;
	poly[5].y = 950;

	poly[6].x = 800;
	poly[6].y = 850;

	poly[7].x = 700;
	poly[7].y = 825;

	bitmap_fill_polygon(&param->bm, poly, 8, colors[4]);

	return 0;
}

static int generate_checkerboard(param_t *param, int m)
{
	uint32_t bw[2];

	fprintf(stderr, "Generating checker board\n");

	bitmap_get_color(&param->bm, "black", &bw[0]);
	bitmap_get_color(&param->bm, "white", &bw[1]);

	bitmap_checker(&param->bm,
		       m, m,
		       param->w - m, param->h - m,
		       bw,
		       param->checker_size);
	return 0;
}

static int generate_color_legend(param_t *param,
				 int s,
				 int x0,
				 int y0)
{
	uint32_t colors[7];
	char text[] = "RGBCYM";
	int i = 0, j, x, y, l, num;

	bitmap_get_color(&param->bm, "red", &colors[i++]);
	bitmap_get_color(&param->bm, "green", &colors[i++]);
	bitmap_get_color(&param->bm, "blue", &colors[i++]);
	bitmap_get_color(&param->bm, "cyan", &colors[i++]);
	bitmap_get_color(&param->bm, "yellow", &colors[i++]);
	bitmap_get_color(&param->bm, "magenta", &colors[i++]);

	l = x0;
	x = l;
	y = y0 - s * 3 / 4;
	num = i;

	for (i = 0, j = 0; i < num; i++, j++) {
		char temp[2];
		temp[0] = text[i];
		temp[1] = 0;
		if (i == 3) {
			x = l;
			j = 0;
			y = y0 + s / 2;
		}
		bitmap_render_font(&param->bm,
				   DEFAULT_BODY_FONT_NAME,
				   temp,
				   s,
				   x + s * j * 3 / 4,
				   y,
				   colors[i]);
	}
	return 0;
}

static int generate_font(param_t *param, int m)
{

	uint32_t color;
	int i, x, y, s;
	char text[80];
	const int sizes[] = { 240, 480, 720, 1080, 2160, 4096, 8192};
	const int margin_sizes[] = { 16, 32, 64, 128, 256};

	fprintf(stderr, "Generating font test\n");
	generate_fill(param, m);
	if (m == 0) {
		m = get_margin_size(param);
	}

	fprintf(stderr, "Generating color legend\n");
	y = param->h * 3 / 4;
	x =  m * 2;
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		if (sizes[i] > MIN(param->h, param->w)) {
			break;
		}
		s =  round((double)(sizes[i]) * 0.08);
		generate_color_legend(param, s, x, y);
		x =  ((m * 2) + sizes[i]);
	}

	fprintf(stderr, "Generating header fonts\n");
	x = param->w / 2;
	y = m * 2;
	bitmap_get_color(&param->bm, "black", &color);
	for (i = 0; i < sizeof(margin_sizes) /
	     sizeof(margin_sizes[0]); i++) {
		if (sizes[i] > MIN(param->h, param->w)) {
			break;
		}
		y += margin_sizes[i] / 2;
		sprintf(text, "Header: %d (%dP)",
			margin_sizes[i] / 2, sizes[i]);
		bitmap_render_font(&param->bm, DEFAULT_HEADER_FONT_NAME,
				   text,
				   margin_sizes[i] / 2, x, y, color);
	}

	fprintf(stderr, "Generating body fonts\n");
	x = param->w / 2;
	y = (param->h / 4) + (m * 2);
	bitmap_get_color(&param->bm, "black", &color);
	for (i = 0; i < sizeof(sizes) /
	     sizeof(sizes[0]); i++) {
		if (sizes[i] > MIN(param->h, param->w)) {
			break;
		}
		s = round((double)sizes[i] * 0.1);
		sprintf(text, "Body :   %d   (%dP)",
			s, sizes[i]);

		bitmap_render_font(&param->bm, DEFAULT_BODY_FONT_NAME,
				   text,
				   s, x, y, color);
		y += s + s / 2;
	}

	return 0;
}

static int generate_grid(param_t *param)
{
	int x, y, r, h, w, l, t, b, x_step, y_step;
	uint32_t bw[2];

	if (param->grid == 0) {
		return 0;
	}

	fprintf(stderr, "Generating grid\n");
	bitmap_get_color(&param->bm, "magenta", &bw[0]);
	bitmap_get_color(&param->bm, "blue", &bw[1]);

	l = 0;
	t = 0;
	r = param->w - 1;
	b = param->h - 1;
	w = r - l;
	h = b - t;

	x_step =  round(w * 0.1);
	y_step =  round(h * 0.1);

	fprintf(stderr, "%s(): l %d t %d b %d r %d\n",
		__func__, l, t, r, b);

	for (y = 0; y <= b; y++) {
		for (x = 0; x <= r; x++) {
			if (y % (y_step) == 0)
				bitmap_draw_pixel(&param->bm, x, y, bw[0]);
			else if (y % (y_step / 2) == 0)
				bitmap_draw_pixel(&param->bm, x, y, bw[1]);
			else if (x % (x_step) == 0)
				bitmap_draw_pixel(&param->bm, x, y, bw[0]);
			else if (x % (x_step / 2) == 0)
				bitmap_draw_pixel(&param->bm, x, y, bw[1]);

			if ((x == r) || (y == b))
				bitmap_draw_pixel(&param->bm, x, y, bw[0]);
		}
	}

	return 0;
}

static int generate_test_gradients(param_t *param,
				   int x0, int y0,
				   int x1, int y1,
				   int *colors, int numcolors,
				   int orientation)
{
	int i, t, b, l, r, s1, s2;
	const int colors_per_gradient = 6;
	if (orientation == 0) {
		b = y1;
		l = x0;
		r = x1;
		s1 = (y1 - y0) / (numcolors / colors_per_gradient);
		t = b - s1;
		s2 = 0;
	} else {
		s1 = 0;
		b = y1;
		l = x0;
		r = x1;
		s2 = (x1 - x0) / (numcolors / colors_per_gradient);
		l = r - s2;
		t = y0;
	}

	for (i = 0; i < numcolors; i += colors_per_gradient) {
		bitmap_gradient(&param->bm,
				l, t,
				r, b,
				colors[i],
				colors[i + 1],
				colors[i + 2],
				colors[i + 3],
				colors[i + 4],
				colors[i + 5],
				orientation);
		t -= s1;
		b -= s1;
		l -= s2;
		r -= s2;
	}

	return 0;
}

#define NUM_GRADIENT_COLORS (4*6)
static int gradient_primary_colors[NUM_GRADIENT_COLORS] =
{
	255, 255, 255, 0, 0, 0,
	0, 0, 255, 0, 0, 0,
	0, 255, 0, 0, 0, 0,
	255, 0, 0, 0, 0, 0,
};

static int gradient_secondary_colors[NUM_GRADIENT_COLORS] =
{
	0, 0, 0, 255, 255, 255,
	0, 0, 0, 0, 255, 255,
	0, 0, 0, 255, 255, 0,
	0, 0, 0, 255, 0, 255,
};

static int generate_test(param_t *param, int m)
{
	int t, b, l, r, s, bw, num_colors, i = 0;
	int vert_top, center_bot, center_top, cb_bot;
	const int sep = 4;
	uint32_t colors[16];

	bitmap_get_color(&param->bm, "white",   &colors[i++]);
	bitmap_get_color(&param->bm, "yellow",  &colors[i++]);
	bitmap_get_color(&param->bm, "cyan",    &colors[i++]);
	bitmap_get_color(&param->bm, "green",   &colors[i++]);
	bitmap_get_color(&param->bm, "magenta", &colors[i++]);
	bitmap_get_color(&param->bm, "red",     &colors[i++]);
	bitmap_get_color(&param->bm, "blue",    &colors[i++]);
	bw = i;
	bitmap_get_color(&param->bm, "black",   &colors[i++]);
	num_colors = i;
	bitmap_get_color(&param->bm, "white",   &colors[i++]);
	bitmap_get_color(&param->bm, "dark_gray",   &colors[i++]);

	if (m == 0)
		s = round((double)MIN(param->w, param->h) * 0.05);
	else
		s = m;

	l = m;
	r = param->w - m;
	t = m;
	b = t + s * 2;
	cb_bot = b;

	/* add color bars */
	l = m + sep;
	r = param->w - (m + sep);
	t += sep;
	bitmap_hbars(&param->bm, l, t, r, b, colors, num_colors);

	/* adding feature from bottom up */
	t = param->h - (m + (s * 2) + sep);
	b = param->h - (m + sep);
	l = m + sep;
	r = param->w - (m + sep);
	vert_top = t;

	generate_test_lines(param, l, t, r, b, &colors[bw]);

	t = param->h / 2;
	l = param->w / 2;

	r =  round(((MIN(t, l) - m) * 0.9)); /* use %90 of min */

	generate_test_circles(param, l, t, r);

	t = round(r * 0.67);

	l = param->w / 2 - t;
	r = param->w / 2 + t;
	s = round(MIN(param->w, param->h) * 0.1);
	t = param->h / 2 - s;
	b = param->h / 2 + s;
	center_bot = b;
	center_top = t;

	generate_test_center(param, l, t, r, b);

	t = center_bot + sep;
	b = vert_top - sep;
	l = m + sep;
	r = param->w - (m + sep);

	generate_test_gradients(param, l, t, r, b,
				gradient_primary_colors,
				NUM_GRADIENT_COLORS, HORIZONTAL);

	t = cb_bot + sep;
	b = center_top - sep;
	generate_test_gradients(param, l, t, r, b,
				gradient_secondary_colors,
				NUM_GRADIENT_COLORS, HORIZONTAL);

	s =  round((double)(MIN(param->w, param->h) * 0.06));
	l = m + s / 2;
	generate_color_legend(param,
			      s,
			      l,
			      param->h / 2);
	return 0;
}

static int generate_wheel(param_t *param, int m)
{
	int t, b, l, r;

	t = m;
	l = m;
	r = param->w - m;
	b = param->h - m;

	bitmap_hsv_circle(&param->bm, l, t, r, b, param->intensity);

	return 0;
}

static int generate_hsv(param_t *param, int m)
{
	int t, b, l, r;

	t = m;
	l = m;
	r = param->w - m;
	b = param->h - m;

	bitmap_hsv_rectangle(&param->bm, l, t, r, b,
			     (b - t) / 16, param->intensity, 360.0);

	return 0;
}

static int generate_16m_colors(param_t *param, int m)
{
	int t, b, l, r;

	t = m;
	l = m;
	r = param->w - m;
	b = param->h - m;

	bitmap_hsv_rectangle(&param->bm, l, t, r, b,
			     (b - t) / 16, param->intensity, 360.0);

	bitmap_16m_colors(&param->bm, l, t, r, b);

	return 0;
}

#define NUM_GRADIENT_COLORS_ALL (8*6)
static int gradient_colors[NUM_GRADIENT_COLORS_ALL] =
{
	255, 255, 255, 0, 0, 0,
	0, 0, 255, 0, 0, 0,
	0, 255, 0, 0, 0, 0,
	255, 0, 0, 0, 0, 0,
	255, 255, 255, 0, 0, 0,
	0, 255, 255, 0, 0, 0,
	255, 255, 0, 0, 0, 0,
	255, 0, 255, 0, 0, 0,
};

static int generate_gradient(param_t *param, int orientation, int m)
{
	int t, b, l, r;

	t = m;
	l = m;
	r = param->w - m;
	b = param->h - m;

	generate_test_gradients(param,
				l, t,
				r, b,
				gradient_colors,
				NUM_GRADIENT_COLORS_ALL,
				orientation);
	return 0;
}

static param_t param;

int main(int argc, char **argv)
{
	int ret = 0, m = 0;

	set_params(&param);

	if (command_parse(argc, argv, &param))
		fprintf(stderr, "Using defaults\n");

	fprintf(stderr, DEFAULT_SEPARATOR_STRING);

	update_params(&param);

	if (param.verbose)
		show_params(&param);

	if (bitmap_create(&param.bm, param.w, param.h,
			  param.stride, param.rotation,
			  param.o_fourcc, param.bpc)) {
		fprintf(stderr, "Failed top create a bitmap\n");
		return 1;
	}

	bitmap_set_debug(&param.bm, param.debug);
	bitmap_set_stuckbits(&param.bm, param.zero, param.one);

	fprintf(stderr, DEFAULT_SEPARATOR_STRING);
	fprintf(stderr, "Generating %s pattern w %lu h %lu\n",
		param.pattern, param.w, param.h);

	generate_borders(&param, &m, "default");

	if (strncmp("colorbar2", param.pattern, 9) == 0)
		ret = generate_colorbar2(&param, m);
	else if (strncmp("colorbar", param.pattern, 8) == 0)
		ret = generate_colorbar(&param, m);
	else if (strncmp("colorcheck", param.pattern, 10) == 0)
		ret = generate_colorcheck(&param, m);
	else if (strncmp("graybar", param.pattern, 7) == 0)
		ret = generate_graybar(&param, m);
	else if (strncmp("fill", param.pattern, 4) == 0)
		ret = generate_fill(&param, m);
	else if (strncmp("circle", param.pattern, 6) == 0)
		ret = generate_circle(&param, m);
	else if (strncmp("logo", param.pattern, 4) == 0)
		ret = generate_nxp_logo(&param, m);
	else if (strncmp("shapes", param.pattern, 6) == 0)
		ret = generate_shapes(&param, m);
	else if (strncmp("test", param.pattern, 4) == 0)
		ret = generate_test(&param, m);
	else if (strncmp("checker", param.pattern, 7) == 0)
		ret = generate_checkerboard(&param, m);
	else if (strncmp("gradient", param.pattern, 8) == 0)
		ret = generate_gradient(&param, HORIZONTAL, m);
	else if (strncmp("vgradient", param.pattern, 9) == 0)
		ret = generate_gradient(&param, VERTICAL, m);
	else if (strncmp("wheel", param.pattern, 5) == 0)
		ret = generate_wheel(&param, m);
	else if (strncmp("hsv", param.pattern, 3) == 0)
		ret = generate_hsv(&param, m);
	else if (strncmp("16m_colors", param.pattern, 9) == 0)
		ret = generate_16m_colors(&param, m);
	else if (strncmp("font", param.pattern, 4) == 0)
		ret = generate_font(&param, m);
	else {
		fprintf(stderr, "Defaulting to colorbar pattern w %lu h %lu\n",
			param.w,  param.h);
		ret = generate_colorbar(&param, m);
	}

	generate_grid(&param);

	fprintf(stderr, DEFAULT_SEPARATOR_STRING);

	bitmap_write_file(&param.bm, param.out);

	bitmap_destroy(&param.bm);
	fprintf(stderr, DEFAULT_SEPARATOR_STRING);

	return ret;
}
