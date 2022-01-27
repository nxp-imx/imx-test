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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <sys/param.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define WIDTH   (8192)
#define HEIGHT  (8192)
#define X_START (4)
#define Y_START (2)
#include "bitmap.h"

#ifdef DEBUG
#define PRINTD(format, args...) fprintf(stderr, format, args)
#define PRINTD2(level, format, args...) if (level > 1 ) \
fprintf(stderr, format, args)
#define PRINTD3(level, format, args...) if (level > 2 ) \
fprintf(stderr, format, args)
#define PRINTD4(level, format, args...) if (level > 3 ) \
fprintf(stderr, format, args)
#define PRINTD5(level, format, args...) if (level > 4 ) \
fprintf(stderr, format, args)
#else
#define PRINTD(format, args...) do {} while(0)
#define PRINTD2(level, format, args...) do {} while(0)
#define PRINTD3(level, format, args...) do {} while(0)
#define PRINTD4(level, format, args...) do {} while(0)
#define PRINTD5(level, format, args...) do {} while(0)
#endif


bitmap_color_t g_colors[] =
{
	/*            aarrggbb                           */
	{ { .word = 0x00ff0000, }, .name =  "red"        },
	{ { .word = 0x0000ff00, }, .name =  "green"      },
	{ { .word = 0x000000ff, }, .name =  "blue"       },
	{ { .word = 0x00ffff00, }, .name =  "yellow"     },
	{ { .word = 0x00ff00ff, }, .name =  "magenta"    },
	{ { .word = 0x0000ffff, }, .name =  "cyan"       },
	{ { .word = 0x00ffffff, }, .name =  "white"      },
	{ { .word = 0x00000000, }, .name =  "black"      },
	{ { .word = 0x00808080, }, .name =  "gray"       },
	{ { .word = 0x00202020, }, .name =  "dark_gray"  },
};

uint32_t bitmap_get_color(bitmap_t *bm, char *name, uint32_t *color)
{
	int i;

	for (i = 0; i < (sizeof(g_colors) / sizeof(g_colors[0])); i++) {
		PRINTD3(bm->debug, "%s %s\n",  g_colors[i].name, name);
		if (strcmp((const char *)&g_colors[i].name[0], name) == 0) {
			*color = g_colors[i].color.word;
			PRINTD3(bm->debug,
				"word 0x%08x r 0x%08x g 0x%08x b 0x%08x a 0x%08x\n",
				g_colors[i].color.word,
				g_colors[i].color.rgb.r,
				g_colors[i].color.rgb.g,
				g_colors[i].color.rgb.b,
				g_colors[i].color.rgb.a);
			return 0;
		}
	}
	return 1;
}

static int in_bounds(double v, double l, double u)
{
	if (v >= l && v <= u)
		return 1;
	return 0;
}

static int hsv_is_valid(double h, double s, double v)
{
	if ((in_bounds(h, HUE_LOWER_LIMIT, HUE_UPPER_LIMIT) == 0) ||
	    (in_bounds(s, PER_LOWER_LIMIT, PER_UPPER_LIMIT) == 0) ||
	    (in_bounds(v, PER_LOWER_LIMIT, PER_UPPER_LIMIT) == 0)) {
		return 0;
	}
	return 1;
}

static uint32_t double2rgb(double r, double b, double g)
{
	return BGRA_PIXEL((uint8_t)(round(r * MAX_RGBA)),
			  (uint8_t)(round(g * MAX_RGBA)),
			  (uint8_t)(round(b * MAX_RGBA)),
			  0);
}

static uint32_t bitmap_hsv_to_rgba(bitmap_t *bm, double h, double s, double v)
{
	double c = 0.0, m = 0.0, x = 0.0;
	uint32_t color = 0;

	if (h < 0.0) {
		h = 360.0 + h;
	}

	if (hsv_is_valid(h, s, v) == 1) {
		c = v * s;
		x = c * (1.0 - fabs(fmod(h / 60.0, 2) - 1.0));
		m = v - c;
		if (h >= 0.0 && h < 60.0)
			color = double2rgb(c + m, x + m, m);
		else if (h >= 60.0 && h < 120.0)
			color = double2rgb(x + m, c + m, m);
		else if (h >= 120.0 && h < 180.0)
			color = double2rgb(m, c + m, x + m);
		else if (h >= 180.0 && h < 240.0)
			color = double2rgb(m, x + m, c + m);
		else if (h >= 240.0 && h < 300.0)
			color = double2rgb(x + m, m, c + m);
		else if (h >= 300.0 && h < 360.0)
			color = double2rgb(c + m, m, x + m);
		else
			color = double2rgb(m, m, m);
	}
	PRINTD5(bm->debug,
		"h %6.2f s %6.2f v %6.2f c %6.2f x %6.2f m %6.2f v 0x%08x\n",
		h, s, v, c, x, m, color);
	return color;
}

#if 0
static uint32_t bitmap_y8_to_rgba(uint32_t color, uint8_t y){
	double i = y / 255.0;
	uint8_t r, g, b, a;

	r = round(i * BGRA_RED(color));
	g = round(i * BGRA_GREEN(color));
	b = round(i * BGRA_BLUE(color));
	a = BGRA_ALPHA(color);

	return BGRA_PIXEL(r, g, b, a);
}
#endif

static uint16_t bitmap_bgra_to_bgr565(uint32_t pixel)
{
	uint8_t r, g, b;
#if 0
	/* alternate conversions */
	r = BGRA_RED(pixel) >> 3;
	g = BGRA_GREEN(pixel) >> 2;
	b = BGRA_BLUE(pixel) >> 3;
#else
	r = round((double)BGRA_RED(pixel) *
		  (double)MAX_RB5 / (double)MAX_RGBA);
	g = round((double)BGRA_GREEN(pixel) *
		  (double)MAX_G6 / (double)MAX_RGBA);
	b = round((double)BGRA_BLUE(pixel) *
		  (double)MAX_RB5 / (double)MAX_RGBA);
#endif
	return BGR565_PIXEL(r, g, b);
}

static uint32_t bitmap_bgra_to_bpc(uint32_t pixel, uint32_t bpc)
{
	uint8_t r, g, b, a;
	uint32_t mask = (0xffffffff << (8-bpc)) ;

	r = BGRA_RED(pixel) & mask;
	g = BGRA_GREEN(pixel) & mask;
	b = BGRA_BLUE(pixel) & mask;
	a = BGRA_ALPHA(pixel);

#if 0
	/* fixup lower bits */
	if (bpc == 6) {
		if (BGRA_RED(pixel) & 0x4)
			r |= 0x1;
		if (BGRA_GREEN(pixel) & 0x4)
			g |= 0x1;
		if (BGRA_BLUE(pixel) & 0x4)
			b |= 0x1;
		if (BGRA_RED(pixel) & 0x8)
			r |= 0x2;
		if (BGRA_GREEN(pixel) & 0x8)
			g |= 0x2;
		if (BGRA_BLUE(pixel) & 0x8)
			b |= 0x2;
	}
#endif
	return BGRA_PIXEL(r, g, b, a);
}

static  uint8_t clip_uint8(int a)
{
	if (a > 0xFF)
		return 0xFF;
	if (a < 0)
		return 0;
	else
		return a;
}

static int bitmap_bgra_to_yuva(bitmap_t *bm, uint32_t pixel,
			       uint8_t *y, uint8_t *u,
			       uint8_t *v, uint8_t *a)
{
	int r, g, b, yt, ut, vt;
	const int coef[3][3] = {
		{  2153984,  16519*256, 821248 },
		{ -1245440, -9528*256, 3684352 },
		{  3684352, -12061*256,-596992 },
	};

	r = (int)BGRA_RED(pixel);
	g = (int)BGRA_GREEN(pixel);
	b = (int)BGRA_BLUE(pixel);

	*a = BGRA_ALPHA(pixel);

	yt = (((coef[0][0] * r) + (coef[0][1] * g) + (coef[0][2] * b) +  134283264) >> 17 ) << 1;

	ut = (((coef[1][0] * r) + (coef[1][1] * g) + (coef[1][2] * b) + 1073807360) >> 17) << 1;

	vt = (((coef[2][0] * r) + (coef[2][1] * g) + (coef[2][2] * b) + 1073807360) >> 17) << 1;

	if (bm->color_range_full == 0) {
		*y = (yt + 64) >> 7;
		*u = (ut + 64) >> 7;
		*v = (vt + 64) >> 7;
	} else {
		yt = (MIN(yt, 30189) * 19077 - 39057361) >> (14);
		ut = (MIN(ut, 30775) * 4663 - 9289992) >> (12);
		vt = (MIN(vt, 30775) * 4663 - 9289992) >> (12);
		*y = clip_uint8((yt + 64) >> 7);
		*u = clip_uint8((ut + 64) >> 7);
		*v = clip_uint8((vt + 64) >> 7);
	}

	return  0;
}

uint32_t bitmap_set_intensity(bitmap_t *bm, uint32_t color, double intensity)
{
	uint8_t r, g, b, a;

	if (intensity < 0.0) {
		fprintf(stderr, "%s(): intensity is below 0%%\n", __func__);
		intensity = 0.0;
	}
	if (intensity > 100.0) {
		fprintf(stderr, "%s(): intensity is above 100%%\n", __func__);
		intensity = 100.0;
	}
	r = round(intensity / 100.0 * BGRA_RED(color));
	g = round(intensity / 100.0 * BGRA_GREEN(color));
	b = round(intensity / 100.0 * BGRA_BLUE(color));
	a = BGRA_ALPHA(color);

	bm->alpha = intensity; /* save last alpha */

	return BGRA_PIXEL(r, g, b, a);
}

void bitmap_set_debug(bitmap_t *bm, int level)
{
	bm->debug = level;
}

void bitmap_set_stuckbits(bitmap_t *bm, uint32_t zero, uint32_t one)
{
	bm->zero = zero;
	bm->one = one;
}

void bitmap_set_color_range(bitmap_t *bm, uint32_t range)
{
	bm->color_range_full = range;
}

void bitmap_set_color_space(bitmap_t *bm, uint32_t space)
{
	bm->color_space = space;
}

uint32_t bitmap_set_alpha(bitmap_t *bm, uint32_t color, double alpha)
{
	uint8_t r, g, b, a;

	if (alpha < 0.0) {
		fprintf(stderr, "%s(): alpha is below 0%%\n", __func__);
		alpha = 0.0;
	} else if (alpha > 100.0) {
		fprintf(stderr, "%s(): alpha is above 100%%\n", __func__);
		alpha = 100.0;
	}
	r = BGRA_RED(color);
	g = BGRA_GREEN(color);
	b = BGRA_BLUE(color);
	a = round(alpha / 100.0 * MAX_RGBA);

	bm->alpha = alpha; /* save last alpha */

	return BGRA_PIXEL(r, g, b, a);
}

int bitmap_create(bitmap_t *bm, int w, int h, int stride,
		  int rotation, unsigned int format, unsigned int bpc)
{
	bm->w =  w;
	bm->h =  h;
	bm->stride = stride;
	bm->rotation = rotation;
	bm->format = format;
	bm->bpp = RGB_BUFFER_BYTES;
	bm->bpc = bpc;

	if (bm->w > bm->stride) {
		bm->stride = bm->w;
	}

	bm->buffer = (uint32_t *)malloc(bm->h * bm->stride *
					RGB_BUFFER_BYTES + 1024);
	if (bm->buffer == NULL) {
		return 1;
	}

	bm->buffer_luma = (uint8_t *)malloc(bm->h * bm->stride *
					    LUMA_BUFFER_BYTES + 1024);
	if (bm->buffer_luma == NULL) {
		free(bm->buffer);
		return 1;
	}

	bm->buffer_chroma = (uint8_t *)malloc(bm->h * bm->stride *
					      CHROMA_BUFFER_BPP + 1024);
	if (bm->buffer_chroma == NULL) {
		free(bm->buffer);
		free(bm->buffer_luma);
		return 1;
	}

	bm->buffer_alpha = (uint8_t *)malloc(bm->h * bm->stride *
					     ALPHA_BUFFER_BPP + 1024);
	if (bm->buffer_alpha == NULL) {
		free(bm->buffer);
		free(bm->buffer_luma);
		free(bm->buffer_chroma);
		return 1;
	}

	return 0;
}

int bitmap_destroy(bitmap_t *bm)
{
	free(bm->buffer);
	free(bm->buffer_luma);
	free(bm->buffer_chroma);
	free(bm->buffer_alpha);
	memset(bm, 0, sizeof(bitmap_t));
	return 0;
}

void bitmap_dump(bitmap_t *bm)
{
	long i;
	fprintf(stderr, "Dumping bitmap at %p (%d)",
		&bm->buffer[0], bm->w * bm->stride);
	for (i = 0; i < (bm->w * bm->stride); i++) {
		if ((i) % 8 == 0)
			fprintf(stderr, " 0x%08lx:", i);
		fprintf(stderr, " 0x%08x", bm->buffer[i]);
		if ((i + i) % 8 == 0)
			fprintf(stderr, "\n");
	}
}

int bitmap_copy_line(bitmap_t *bm, int y, uint32_t *line)
{
	PRINTD5(bm->debug, "%s(): y %d\n", __func__, y);
	if (y >= bm->h) {
		fprintf(stderr, "%s(): line out of range y %d\n",
			__func__, y);
		abort();
	}
	memcpy(&bm->buffer[y * bm->stride], line, bm->w * bm->bpp);
	return 0;
}

int bitmap_copy_line_segment(bitmap_t *bm, int x0, int y0,
			     int size, uint32_t *line)
{
	PRINTD5(bm->debug, "%s(): x0 %d y0 %d\n", __func__, x0, y0);
	if ((x0 < 0) || (y0 < 0) || (y0 >= bm->h) || ((x0 + size) > bm->w)) {
		fprintf(stderr, "%s(): line out of range x0 %d y0 %d size %d\n",
			__func__, x0, y0, size);
		abort();
	}
	memcpy(&bm->buffer[y0 * bm->stride + x0], line, size * bm->bpp);
	return 0;
}

int bitmap_draw_pixel(bitmap_t *bm, int x, int y, uint32_t v)
{
	PRINTD5(bm->debug, "%s(): x0 %d y0 %d v %u\n", __func__, x, y, v);
	if ((y >= bm->h) || (x >= bm->w) || (y < 0) || (x < 0)) {
		fprintf(stderr, "%s(): pixel out of range x %d y %d\n",
			__func__, x, y);
		abort();
	}
	bm->buffer[y * bm->stride + x] = v;
	return 0;
}

int bitmap_blend_pixel(bitmap_t *bm, int x, int y, uint32_t v, int alpha)
{
	uint8_t r, g, b, local;
	uint32_t dest = bm->buffer[y * bm->stride + x];

	double a = (double)(alpha & 0xff) / (double)MAX_RGBA;

	PRINTD4(bm->debug, "%s(): x0 %d y0 %d v 0x%x a %f\n",
		__func__, x, y, v, a * 100);
	if ((y >= bm->h) || (x >= bm->w) || (y < 0) || (x < 0)) {
		fprintf(stderr, "%s(): pixel out of range x %d y %d\n",
			__func__, x, y);
		abort();
	}

	r = round(((double)BGRA_RED(v) * a) +
		  ((double)BGRA_RED(dest) * (1.0 - a)));
	g = round(((double)BGRA_GREEN(v) * a) +
		  ((double)BGRA_GREEN(dest) * (1.0 - a)));
	b = round(((double)BGRA_BLUE(v) * a) +
		  ((double)BGRA_BLUE(dest) * (1.0 - a)));

	local = BGRA_ALPHA(dest); /* don't change the alpha */

	bm->buffer[y * bm->stride + x] = BGRA_PIXEL(r, g, b, local);
	return 0;
}

int bitmap_draw_line(bitmap_t *bm,
		     int x0, int y0,
		     int x1, int y1,
		     uint32_t pixel)
{
	uint32_t temp;
	double x, y, x_step = 0.1;

	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d pixel 0x%08x\n",
		__func__, x0, y0, x1, y1, pixel);

	if (x0 > x1) {
		temp  = x0;
		x0 = x1;
		x1 = temp;
		temp  = y0;
		y0 = y1;
		y1 = temp;
	}

	for (x = x0; x < x1; x += x_step) {
		double m = (double)(y1 - y0) / (double)(x1 - x0);
		y = round(m * (x - x0) + y0);
		bitmap_draw_pixel(bm, x, y, pixel);
	}

	return 0;
}

int bitmap_draw_line2(bitmap_t *bm,
		      int x0, int y0,
		      int x1, int y1,
		      int width,
		      uint32_t pixel)
{
	uint32_t temp;
	double  f, x, y, m, step = 0.5;

	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d pixel 0x%08x\n",
		__func__, x0, y0, x1, y1, pixel);

	m = (double)(y1 - y0) / (double)(x1 - x0);

	if (fabs(m) < 1.0) {
		if (x0 > x1) {
			temp  = x0;
			x0 = x1;
			x1 = temp;
			temp  = y0;
			y0 = y1;
			y1 = temp;
		}

		for (x = x0; x < x1; x += step) {
			//y = round(m * (x - x0) + y0);
			f = m * (x - x0) + y0;
			y = floor(f);
			f = f - y ;
			PRINTD4(bm->debug, "%s(): x %f y %f m %f f %f \n",
				__func__, x, y, m, f);

			bitmap_blend_pixel(bm, x, y - 1, pixel,
					   (1.0 - f) * 128);
			bitmap_blend_pixel(bm, x, y + 1,
					   pixel, (f) * 128);
			bitmap_draw_pixel(bm, x, y, pixel);
		}
	} else if (fabs(m) > 1.0) {
		if (y0 > y1) {
			temp  = x0;
			x0 = x1;
			x1 = temp;
			temp  = y0;
			y0 = y1;
			y1 = temp;
		}

		for (y = y0; y < y1; y += step) {
			//y = round(m * (x - x0) + y0);
			f =  ((y - y0) / m) + x0;
			x = floor(f);
			f = f - x;

			PRINTD4(bm->debug, "%s(): x %f y %f m %f f %f \n",
				__func__, x, y, m, f);
			bitmap_blend_pixel(bm, x - 1, y, pixel,
					   (1.0 - f) * 128);
			bitmap_blend_pixel(bm, x + 1, y, pixel,
					   (f) * 128);
			bitmap_draw_pixel(bm, x, y, pixel);
		}
	} else {
		if (x0 > x1) {
			temp  = x0;
			x0 = x1;
			x1 = temp;
			temp  = y0;
			y0 = y1;
			y1 = temp;
		}

		step = 1.0;
		for (x = x0; x < x1; x += step) {
			//y = round(m * (x - x0) + y0);
			f = m * (x - x0) + y0;
			y = floor(f);
			//= f - y ;
			PRINTD4(bm->debug, "%s(): x %f y %f m %f f %f \n",
				__func__, x, y, m, f);

			bitmap_blend_pixel(bm, x, y - 1, pixel, 64);
			bitmap_blend_pixel(bm, x, y + 1, pixel, 64);
			bitmap_draw_pixel(bm, x, y, pixel);
		}
	}
	return 0;
}

int bitmap_draw_circle(bitmap_t *bm, int x0, int y0,
		       int r0, uint32_t v)
{
	double t, x, y, r, x_temp, x_step = 0.0005;
	r = r0;

	PRINTD3(bm->debug, "%s(): x0 %d y0 %d r0 %d v %u\n",
		__func__, x0, y0, r0, v);

	for (x = -r; x <=  r; x += x_step) {
		/*y = +- sqrt( r^2 - x^2  )*/
		x_temp = x;
		t = (r * r) - (x_temp * x_temp);
		t = sqrt(t);
		y = y0 + t;
		bitmap_draw_pixel(bm, round(x) + x0, round(y), v);
		y = y0 - t;
		bitmap_draw_pixel(bm, round(x) + x0, round(y), v);
		PRINTD5(bm->debug, " x %f y %f r %f t %f\n", x, y, r, t);
	}

	return 0;
}

double calculate_radius(bitmap_t *bm, int x0, int y0, int x, int y)
{
	double r;
	r = (double)(x - x0) * (double)(x - x0);
	r += (double)(y - y0) * (double)(y - y0);
	return sqrt(r);
}

int bitmap_fill_circle(bitmap_t *bm, int x0, int y0,
		       int r0, uint32_t v)
{
	double t, x, y, r, x_temp, x_step = 0.0005;
	r = r0;

	PRINTD3(bm->debug, "%s(): x0 %d y0 %d r0 %d v %u\n",
		__func__, x0, y0, r0, v);

	for (x = -r; x <=  r; x += x_step) {
		/*y = +- sqrt( r^2 - x^2  )*/
		x_temp = x;
		t = (r * r) - (x_temp * x_temp);
		t = sqrt(t);

		PRINTD5(bm->debug, " x %f y %f r %f t %f\n", x, y, r, t);
		for (y = round(y0 - t); y < round(y0 + t); y++) {
			bitmap_draw_pixel(bm, round(x) + x0, y, v);
		}
	}

	return 0;
}

int bitmap_fill_circle2(bitmap_t *bm, int x0, int y0,
		       int r0, int r1, uint32_t v)
{
	double r = r0;

	int x, y, xmin, xmax, ymin, ymax;

	PRINTD3(bm->debug, "%s(): x0 %d y0 %d r0 %d v %u\n",
		__func__, x0, y0, r0, v);

	xmin = x0 - r1;
	xmax = x0 + r1;
	ymin = y0 - r1;
	ymax = y0 + r1;

	for (y = ymin; y <=  ymax; y++) {
		for (x = xmin; x <=  xmax; x++) {
			r = calculate_radius(bm, x0, y0, x, y);
			if ((r < r1)  && (r > r0)) {
				bitmap_draw_pixel(bm, x, y, v);
			} else if ( ((r - r1) < 1.00) && (r >  r0)) {
				bitmap_blend_pixel(bm, x, y, v,
						   (1 - (r - r1)) * 255 );
			} else if ( ((r0 - r) < 1.00) && (r <  r1)) {
				bitmap_blend_pixel(bm, x, y, v,
						   (1- (r0- r)) * 255 );
			}
		}
	}

	return 0;
}
#if 0
int bitmap_fill_circle(bitmap_t *bm, int x0, int y0,
		       int r0, uint32_t v)
{
	double t, x, y, r, x_temp, x_step = 0.0005;
	r = r0;

	PRINTD3(bm->debug, "%s(): x0 %d y0 %d r0 %d v %u\n",
		__func__, x0, y0, r0, v);

	for (x = -r; x <=  r; x += x_step) {
		/*y = +- sqrt( r^2 - x^2  )*/
		x_temp = x;
		t = (r * r) - (x_temp * x_temp);
		t = sqrt(t);

		PRINTD5(bm->debug, " x %f y %f r %f t %f\n", x, y, r, t);
		for (y = round(y0 - t); y < round(y0 + t); y++) {
			bitmap_draw_pixel(bm, round(x) + x0, y, v);
		}
	}

	return 0;
}
#endif

int bitmap_draw_arc(bitmap_t *bm, int x0, int y0,
		     int r0, int r1,
		     double theata0, double theata1,
		     uint32_t v)
{
	double r = r0, angle;

	int x, y, xmin, xmax, ymin, ymax;

	PRINTD3(bm->debug, "%s(): x0 %d y0 %d r0 %d v %u\n",
		__func__, x0, y0, r0, v);

	xmin = x0 - r1;
	xmax = x0 + r1;
	ymin = y0 - r1;
	ymax = y0 + r1;

	for (y = ymin; y <=  ymax; y++) {
		for (x = xmin; x <=  xmax; x++) {
			angle = atan2(-(y-y0), x-x0);
			if (( theata0 < angle) &&
			    ( theata1 > angle)){
				r = calculate_radius(bm, x0, y0, x, y);
				if ((r < r1)  && (r > r0)) {
					bitmap_draw_pixel(bm, x, y, v);
				} else if ( ((r - r1) < 1.00) && (r >  r0)) {
					bitmap_blend_pixel(bm, x, y, v,
							   (1 - (r - r1))
							   * 255);
				} else if ( ((r0 - r) < 1.00) && (r <  r1)) {
					bitmap_blend_pixel(bm, x, y, v,
							   (1- (r0- r))
							   * 255);
				}
			}
		}
	}

	return 0;
}

/*
   Return the angle between two vectors on a plane
   The angle is from vector 1 to vector 2, positive anticlockwise
   The result is between -pi -> pi
*/
/*              0-------------1                */
/*              |FFFFFFFFFFFFF|                */
/*              |FFFFFFFFFFFFF|                */
/*              |FFFFFFFFFFFFF|                */
/*              |FFFFFFFFFFFFF|                */
/*              |FFFFFFFFFFFFF|                */
/*              |FFFFFFFFFFFFF|                */
/*              2-------------3                */
/*                     0                       */
/*                    / \                      */
/*                   /   \                     */
/*                  /     \                    */
/*                 /       1                   */
/*                2       /                    */
/*                 \     /                     */
/*                  \   /                      */
/*                   \ /                       */
/*                    3                        */

double calculate_angle(double x1, double y1, double x2, double y2)
{
	double dtheta, theta1, theta2;

	theta1 = atan2(y1, x1);
	theta2 = atan2(y2, x2);
	dtheta = theta2 - theta1;
	while (dtheta > PI) dtheta -= TWOPI;
	while (dtheta < -PI) dtheta += TWOPI;

	return (dtheta);
}

int is_inside(bitmap_t *bm, point *polygon, int n, point p)
{
	int i;
	double angle = 0;
	point p1, p2;

	for (i = 0; i < n; i++) {
		p1.x = polygon[i].x - p.x;
		p1.y = polygon[i].y - p.y;
		p2.x = polygon[(i + 1) % n].x - p.x;
		p2.y = polygon[(i + 1) % n].y - p.y;
		angle += calculate_angle(p1.x, p1.y, p2.x, p2.y);
	}
	if (fabs(angle) < PI) {
		return 0;
	}
	return 1;
}



int find_xmin(point vertexes[], int n)
{
	int t = WIDTH;
	for (int i = 0; i < n; i++) {
		if (vertexes[i].x < t)
			t = vertexes[i].x;
	}
	return t;
}

int find_xmax(point vertexes[], int n)
{
	int t = 0;
	for (int i = 0; i < n; i++) {
		if (vertexes[i].x > t)
			t = vertexes[i].x;
	}
	return t;
}

int find_ymin(point vertexes[], int n)
{
	int t = HEIGHT;
	for (int i = 0; i < n; i++) {
		if (vertexes[i].y < t)
			t = vertexes[i].y;
	}
	return t;
}

int find_ymax(point vertexes[], int n)
{
	int t = 0;
	for (int i = 0; i < n; i++) {
		if (vertexes[i].y > t)
			t = vertexes[i].y;
	}
	return t;
}

int bitmap_fill_rectangle(bitmap_t *bm,
			  int x0, int y0,
			  int x1, int y1,
			  uint32_t pixel)
{
	uint32_t y, x;
	uint32_t lines[MAX_WIDTH];

	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d pixel 0x%08x\n",
		__func__, x0, y0, x1, y1, pixel);

	/* fill a line segment */
	for (x = 0; x < (x1 - x0); x++) {
		lines[x] = pixel;
	}

	for (y = y0; y < y1; y++) {
		bitmap_copy_line_segment(bm, x0, y, x1 - x0, lines);
	}

	return 0;
}

int bitmap_fill_quadrangle(bitmap_t *bm,
			   int x0, int y0,
			   int x1, int y1,
			   int x2, int y2,
			   int x3, int y3,
			   uint32_t pixel)
{
	uint32_t y, x, l, t, r, b;
	point p;
	point vertexes[4] = {
		{ x0,  y0 },
		{ x1,  y1 },
		{ x2,  y2 },
		{ x3,  y3 },
	};

	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d pixel 0x%08x\n",
		__func__, x0, y0, x1, y1, pixel);
	PRINTD2(bm->debug, "%s(): x2 %d y2 %d x3 %d y3 %d pixel 0x%08x\n",
		__func__, x2, y2, x3, y3, pixel);

	bitmap_draw_line(bm,
			 x0, y0,
			 x1, y1,
			 pixel);

	bitmap_draw_line(bm,
			 x0, y0,
			 x2, y2,
			 pixel);

	bitmap_draw_line(bm,
			 x1, y1,
			 x3, y3,
			 pixel);

	bitmap_draw_line(bm,
			 x2, y2,
			 x3, y3,
			 pixel);

	l = find_xmin(vertexes, 4);
	r = find_xmax(vertexes, 4);
	t = find_ymin(vertexes, 4);
	b = find_ymax(vertexes, 4);

	PRINTD2(bm->debug, "%s(): l %d r %d t %d b %d pixel 0x%08x\n",
		__func__, l, r, t, b, pixel);

	for (y = t; y <= b; y++) {
		for (x = l; x <= r; x++) {
			p.x = x;
			p.y = y;
			PRINTD5(bm->debug, "%s(): x %d y %d pixel 0x%08x\n",
				__func__, x, y, pixel);
			if (is_inside(bm, vertexes, 4, p))
				bitmap_draw_pixel(bm, x, y, pixel);
		}
	}
	return 0;
}

int bitmap_fill_polygon(bitmap_t *bm, point *polygon, int n, uint32_t pixel)
{
	uint32_t y, x, l, t, r, b;
	point p;

	l = find_xmin(polygon, n)-1;
	r = find_xmax(polygon, n)+1;
	t = find_ymin(polygon, n)-1;
	b = find_ymax(polygon, n)+1;

	for (y = t; y <= b; y++) {
		for (x = l; x <= r; x++) {
			p.x = x;
			p.y = y;
			PRINTD5(bm->debug, "%s(): x %d y %d pixel 0x%08x\n",
				__func__, x, y, pixel);
			if (is_inside(bm, polygon, n, p))
				bitmap_draw_pixel(bm, x, y, pixel);
		}
	}

	return 0;
}

int bitmap_is_yuv(unsigned int f)
{
	switch (f) {
	case FORMAT_YUV444P:
	case FORMAT_YUV420:
	case FORMAT_NV12:
	case FORMAT_YUV444:
	case FORMAT_YUVA444:
	case FORMAT_YUYV422:
		return TRUE;
	default:
		return FALSE;
	}
}

static int get_vertical_sub(unsigned int f)
{
	switch (f) {
	case FORMAT_YUV444:
	case FORMAT_YUVA444:
	case FORMAT_YUYV422:
	case FORMAT_YUV444P:
		return 1;
	case FORMAT_YUV420:
	case FORMAT_NV12:
		return 2;
	default:
		return 1;
	}
}

static int get_horizontal_sub(unsigned int f)
{
	switch (f) {
	case FORMAT_YUV444:
	case FORMAT_YUVA444:
	case FORMAT_YUV444P:
		return 1;
	case FORMAT_YUV420:
	case FORMAT_NV12:
	case FORMAT_YUYV422:
		return 2;
	default:
		return 1;
	}
}

static int get_planes(unsigned int f)
{
	switch (f) {
	case FORMAT_YUV444P:
	case FORMAT_YUV420:
		return 3;
	case FORMAT_NV12:
		return 2;
	case FORMAT_YUV444:
	case FORMAT_YUVA444:
	case FORMAT_YUYV422:
		return 1;
	default:
		return 1;
	}
}

static int is_yuv_subsampled(unsigned int f)
{
	switch (f) {
	case FORMAT_YUYV422:
	case FORMAT_YUV420:
	case FORMAT_NV12:
		return TRUE;
	default:
		return FALSE;
	}
}

static int bitmap_rotate_buffer(bitmap_t *bm)
{
	int x, y, w, h, i = 0, j = 0, s;
	w = MAX(bm->stride, bm->w);
	h = bm->h;
	s = MAX(h, w) * MAX(h, w) * RGB_BUFFER_BYTES + 1024;

	uint32_t *temp_buffer;

	fprintf(stderr, "Rotating buffer to %d degrees\n", bm->rotation);

	if (bm->rotation == 0)
		return 0;

	temp_buffer = (uint32_t *)malloc(s);
	if (temp_buffer == NULL) {
		fprintf(stderr, "Failed to allocate rotation buffer\n");
		return 1;
	}

	for (y = 0;  y < h; y++) {
		for (x = 0;  x < w; x++) {
			if (bm->rotation == 90) {
				i = ((x)*h) + ((h-1) - y);
				j = (y * w) + x;
			} else if (bm->rotation == 180) {
				i = (((h-1) - y) * w) + ((w-1) - x);
				j = (y * w) + x;
			} else if (bm->rotation == 270) {
				i = (((w-1) - x) * h) + (y);
				j = (y * w) + x;
			} else {
				/* default is rotation 0 degrees*/
				i = (y * w) + x;
				j = (y * w) + x;
			}

			temp_buffer[i] = bm->buffer[j];
			PRINTD5(bm->debug, "tb %x b %x x %d y %d i %d j %d\n",
				temp_buffer[i], bm->buffer[j], x, y, i, j);
		}
	}

	free(bm->buffer);
	bm->buffer = temp_buffer;
	if ((bm->rotation == 90) || (bm->rotation == 270)) {
		w = bm->w;
		bm->stride = bm->h;
		bm->w = bm->h;
		bm->h = w;
	}
	return 0;
}

static int bitmap_convert_buffer(bitmap_t *bm)
{
	int i, j, k, l, s;

	s = bm->h * bm->stride;

	/* force stuck ones and zeros */
	if (bm->zero || bm->one) {
		fprintf(stderr, "Forcing stuck bits!\n");
		uint32_t *out =  (uint32_t *)bm->buffer;
		for (i = 0;  i < s; i++) {
			if (bm->zero) {
				out[i] &= ~bm->zero;
			}
			if (bm->one) {
				out[i] |= bm->one;
			}
		}
	}

	if (bm->format == FORMAT_BGR565) {
		uint16_t *buffer_out =  (uint16_t *)bm->buffer;
		fprintf(stderr,
			"Converting to output FORMAT_BGR565 (rgb565le)\n");
		for (i = 0;  i < s; i++) {
			buffer_out[i] = bitmap_bgra_to_bgr565(bm->buffer[i]);
		}
		bm->bpp = 2;
		bm->planes = 1;
	} else if (bm->format == FORMAT_YUYV422) {
		uint32_t *buffer_out =  (uint32_t *)bm->buffer;
		fprintf(stderr,
			"Converting to output FORMAT_YUV422 (yuv422)\n");
		for (i = 0;  i < s; i += 2) {
			uint8_t y, u, v, a;
			uint8_t y0, u0, y1, v0;

			bitmap_bgra_to_yuva(bm, bm->buffer[i], &y, &u, &v, &a);
			y0 = y;
			u0 = u;
			v0 = v;

			/* todo: handle odd widths */
			bitmap_bgra_to_yuva(bm, bm->buffer[i + 1], &y, &u, &v, &a);
			y1 = y;

			buffer_out[i / 2] = YUYV_PIXEL(y0, u0, y1, v0);
		}
		bm->bpp = 2;
		bm->planes = 1;
	} else if ((bm->format == FORMAT_YUV444) ||
		   (bm->format == FORMAT_YUVA444)) {
		uint8_t *buffer_out =  (uint8_t *)bm->buffer;
		int j;

		for (i = 0, j = 0;  i < s; i++) {
			uint8_t y, u, v, a;

			bitmap_bgra_to_yuva(bm, bm->buffer[i], &y, &u, &v, &a);

			buffer_out[j] = y;
			buffer_out[j + 1] = u;
			buffer_out[j + 2] = v;

			if (bm->format == FORMAT_YUVA444) {
				buffer_out[j + 3] = a;
				j += 4;
			} else
				j += 3;
		}
		if (bm->format == FORMAT_YUV444) {
			fprintf(stderr,
				"Converting to output FORMAT_YUV444 (yuv444)\n");
			bm->bpp = 3;
		} else {
			fprintf(stderr,
				"Converting to output FORMAT_YUVA444 (yuva444)\n");
			bm->bpp = 4;
		}
		bm->planes = 1;
	} else if (bitmap_is_yuv(bm->format)) {
		uint8_t *ybuf = &bm->buffer_luma[0];
		uint8_t *ubuf = &bm->buffer_chroma[0];
		uint8_t *vbuf = &bm->buffer_chroma[s];
		uint8_t *abuf = &bm->buffer_alpha[0];

		bm->planes = get_planes(bm->format);
		bm->h_sub = get_vertical_sub(bm->format);
		bm->w_sub = get_horizontal_sub(bm->format);

		fprintf(stderr,
			"Converting to output FORMAT_YUV444 (yuva444p)\n");
		for (i = 0;  i < s; i++) {
			uint8_t y, u, v, a;
			bitmap_bgra_to_yuva(bm, bm->buffer[i], &y, &u, &v, &a);
			ybuf[i] = y;
			ubuf[i] = u;
			vbuf[i] = v;
			abuf[i] = a;
		}

		/* YUV 444 we are done*/
		if (is_yuv_subsampled(bm->format)) {
			uint8_t *uvbuf = (uint8_t *)&bm->buffer_chroma[0];
			if (bm->format == FORMAT_YUV420) {
				k = 0;
				fprintf(stderr,
					"Converting to output FORMAT_YUV420 (yuv420p)\n");
				/* now sub sample chroma */
				for (l = 0;  l < bm->h; l += bm->h_sub) {
					for (i = 0;  i < bm->stride;
					     i += bm->w_sub) {
						j = bm->stride * l + i;
						uvbuf[k++] = ubuf[j];
					}
				}
				for (l = 0;  l < bm->h; l += bm->h_sub) {
					for (i = 0;  i < bm->stride;
					     i += bm->w_sub) {
						j = bm->stride * l + i;
						uvbuf[k++] = vbuf[j];
					}
				}
			} else {
				k = 0;
				fprintf(stderr,
					"Converting to output FORMAT_NV12 (nv12)\n");
				/* now sub sample chroma first pass */
				for (i = 0;  i < bm->h; i += bm->h_sub) {
					for (j = 0;  j < bm->stride;
					     j += bm->w_sub) {
						l = bm->stride * i + j;
						uvbuf[k++] = ubuf[l];
						uvbuf[k++] = vbuf[l];
					}
				}
			}
		}
	} else if (bm->format ==  FORMAT_BGRA8888) {
		bm->planes = 1;
		if (bm->bpc < 8) {
			uint32_t *out =  (uint32_t *)bm->buffer;
			fprintf(stderr,
				"Converting to output FORMAT_BGRA8888 (bgra) %d pits per pixel\n",
				bm->bpc);
			for (i = 0;  i < s; i++) {
				out[i] = bitmap_bgra_to_bpc(bm->buffer[i], bm->bpc);
			}
		} else
		/* do nothing */
			fprintf(stderr,
				"Converting to output FORMAT_BGRA8888 (bgra)\n");
	} else {
		fprintf(stderr, "Unsupported output format %c%c%c%c\n",
			(unsigned char)bm->format >> 24,
			(unsigned char)bm->format >> 16,
			(unsigned char)bm->format >> 8,
			(unsigned char)bm->format >> 0);
	}
	return 0;
}

int bitmap_write_file(bitmap_t *bm, char *out)
{
	FILE *fout;
	int s, e;

	/* bitmap_dump(bm);*/

	bitmap_rotate_buffer(bm);
	bitmap_convert_buffer(bm);

	fprintf(stderr, "Opening outfile %s\n", out);

	if (strnlen(out, MAX_PATH) > 0) {
		fout = fopen(out, "w+");
		if (fout == NULL) {
			fprintf(stderr, "fopen returned an error\n");
			return 1;
		}
	} else {
		fprintf(stderr, "Missing output filename parameter\n");
		return 1;
	}

	if (bm->format == FORMAT_YUV444P) {
		s = bm->h * bm->stride;
		e = 1;
		fprintf(stderr, "Writing %d luma pixels (bytes)\n", s);
		fwrite(&bm->buffer_luma[0], s, e, fout);

		/*For YUV444   u, u, u, ... v, v, v, ... */
		s =  (bm->h * bm->stride) / (bm->h_sub * bm->w_sub) * 2;
		e = 1;
		fprintf(stderr, "Writing %d chroma pixels (bytes)\n", s);
		fwrite(&bm->buffer_chroma[0], s, e, fout);

	}  else if ((bm->format == FORMAT_YUV420) ||
		    (bm->format == FORMAT_NV12)) {
		/* luma */
		s = bm->h * bm->stride;
		e = 1;
		fprintf(stderr, "Writing %d luma pixels (bytes)\n", s);
		fwrite(&bm->buffer_luma[0], s, 1, fout);

		/* For YUV420   u, u, u, ... v, v, v, ... */
		/* For NV12     uv, uv, uv ...*/
		s =   (bm->h * bm->stride) / (bm->h_sub * bm->w_sub) * 2;
		e = 1;
		fprintf(stderr, "Writing %d chroma pixels (bytes)\n", s);
		fwrite(bm->buffer_chroma, s, e, fout);
	} else {
		s = bm->h * bm->stride;
		e = bm->bpp;
		fprintf(stderr, "Writing %d rgb pixels ( %d bytes)\n",
			s, s * e);
		fwrite(bm->buffer, bm->h * bm->stride, bm->bpp, fout);
	}

	fclose(fout);

	return 0;
}

int bitmap_hbars(bitmap_t *bm,
		 int x0, int y0,
		 int x1, int y1,
		 uint32_t *colors, int num_colors)
{
	int i, l, t, r, b, w;


	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d colors %u\n",
		__func__, x0, y0, x1, y1, num_colors);

	t = y0;
	b = y1;
	w = x1 - x0;

	for (i = 0; i < num_colors; i++) {
		l = ((i * w) / num_colors) + x0;
		r =  (((i + 1) * w) / num_colors) + x0;

		bitmap_fill_rectangle(bm, l, t, r, b,
				      colors[i]);

	}

	return 0;
}

int bitmap_checker(bitmap_t *bm,
		   int x0, int y0,
		   int x1, int y1,
		   uint32_t *colors, /* size is two element */
		   int checker_size)
{
	uint32_t x, y, c;

	PRINTD2(bm->debug,
		"%s(): x0 %d y0 %d x1 %d y1 %d colors 0x%x 0x%x size %d\n",
		__func__, x0, y0, x1, y1, colors[0], colors[1], checker_size);

	for (y = y0; y < y1; y++) {
		/* fill a line segment */
		for (x = x0; x < x1; x++) {
			if (y / checker_size % 2) {
				c = colors[x / checker_size % 2];
			} else {
				c = colors[(x + checker_size) /
					   checker_size % 2];
			}
			bitmap_draw_pixel(bm, x, y, c);
		}
	}
	return 0;
}

int bitmap_lines(bitmap_t *bm,
		 int x0, int y0,
		 int x1, int y1,
		 uint32_t *colors, /* size is two element */
		 int size,
		 int orientation)
{
	uint32_t x, y, c, h, w;

	h = y1 - y0;
	w = x1 - x0;

	for (y = 0; y < h; y++) {
		/* fill a line segment */
		for (x = 0; x < w; x++) {
			if (orientation)
				c = colors[y / size % 2];
			else
				c = colors[x / size % 2];
			bitmap_draw_pixel(bm, x + x0, y + y0, c);
		}
	}
	return 0;
}

int bitmap_gradient(bitmap_t *bm,
		    int x0, int y0,
		    int x1, int y1,
		    int r0, int g0, int b0,
		    int r1, int g1, int b1,
		    int orientation)
{
	int x, y = 0;
	uint8_t r, g, b;
	uint32_t lines[MAX_WIDTH];

	float r_slope, g_slope, b_slope;

	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d\n",
		__func__, x0, y0, x1, y1);

	if (orientation) {
		/* vertical */
		r_slope = ((float)(r1 - r0) * 1.0) / ((float)(y1 - y0) * 1.0);
		g_slope = ((float)(g1 - g0) * 1.0) / ((float)(y1 - y0) * 1.0);
		b_slope = ((float)(b1 - b0) * 1.0) / ((float)(y1 - y0) * 1.0);

		for (y = y0; y <  y1; y++) {
			r = ((y - y0) * r_slope) + r0;
			g = ((y - y0) * g_slope) + g0;
			b = ((y - y0) * b_slope) + b0;
			for (x = x0; x < x1; x++) {
				lines[x] = r << 16 | g << 8 | b;
			}
			bitmap_copy_line_segment(bm, x0, y, x1 - x0,
						 &lines[x0]);
		}
	} else {
		/* horizontal */
		r_slope = ((float)(r1 - r0) * 1.0) / ((float)(x1 - x0) * 1.0);
		g_slope = ((float)(g1 - g0) * 1.0) / ((float)(x1 - x0) * 1.0);
		b_slope = ((float)(b1 - b0) * 1.0) / ((float)(x1 - x0) * 1.0);

		PRINTD3(bm->debug, "slope r g b %f %f %f\n",
			r_slope, g_slope, b_slope);

		for (x = x0; x < x1; x++) {
			r = ((x - x0) * r_slope) + r0;
			g = ((x - x0) * g_slope) + g0;
			b = ((x - x0) * b_slope) + b0;

			PRINTD5(bm->debug, "x y r g b %d %d %u %u %u\n",
				x, y, r, g, b);
			lines[x] = r << 16 | g << 8 | b;
		}

		for (y = y0; y <  y1; y++) {
			//bitmap_write_line(&param->bm, y, lines);
			bitmap_copy_line_segment(bm, x0, y, x1 - x0,
						 &lines[x0]);
		}
	}

	return 0;
}

static double adjust_angle(double a)
{
	double v = a;
	if (a < 0)
		while (v < 0) v += 360.0;
	else
		while (v > 360.0) v -= 360.0;
	return v;
}

int bitmap_hsv_circle(bitmap_t *bm,
		      int x0, int y0,
		      int x1, int y1,
		      double val)
{
	int x, y;
	uint32_t c;
	int h, w, xc, yc;
	double radius, size, angle, opp, adj, deg;

	h = y1 - y0;
	w = x1 - x0;

	xc = w / 2;
	yc = h / 2;
	size = MIN(h, w) / 2.0;

	PRINTD2(bm->debug, "%s(): x0 %4d y0 %4d x1 %4d y1 %4d h %4d w %4d\n",
		__func__, x0, y0, x1, y1, h, w);
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			opp = y - yc;
			adj = x - xc;
			if ((x - xc) != 0) {
				angle = atan2(opp, adj);
			} else if (opp > 0.0) {
				angle = DEG2RAD(90.0);
			} else
				angle = DEG2RAD(-90.0);

			angle += DEG2RAD(90.0);
			deg = adjust_angle(RAD2DEG(-angle);
					   radius = sqrt(pow(opp, 2.0) +
							 pow(adj, 2.0));

					   if (radius <= size) {
				c = bitmap_hsv_to_rgba(bm, deg,
						       radius / size,
						       val / 100.0);
				PRINTD5(bm->debug,
					"x %4d y %4d h %f s %f v 0x%08x\n",
					x, y, deg, radius / size, c);

				c = bitmap_set_alpha(bm, c, bm->alpha);

				bitmap_draw_pixel(bm, x + x0, y + y0, c);
			}
		}
	}
	return 0;
}

int bitmap_hsv_rectangle(bitmap_t *bm, int x0, int y0, int x1, int y1,
			 int margin, double val, double degrees)
{
	int x, y;
	uint32_t c;
	int h, w;
	double slope = 0.0;
	h = (y1 - y0) - (margin * 2);
	w = (x1 - x0);

	slope = (double)degrees / (double)w;
	PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d\n", __func__,
		x0, y0, x1, y1);
	for (y = 0; y < margin; y++) {
		for (x = 0; x < w; x++) {
			c = bitmap_hsv_to_rgba(bm, 360.0 - (double)x * slope,
					       1.0, val / 100.0);
			c = bitmap_set_alpha(bm, c, bm->alpha);
			bitmap_draw_pixel(bm, x + x0, y + y0, c);
		}
	}

	if (margin >= h)
		return 0;
	for (y = 0; y < h + margin; y++) {
		for (x = 0; x < w; x++) {
			c = bitmap_hsv_to_rgba(bm, 360.0 - (double)x * slope,
					       1.0 - (double)y / (double)h,
					       val / 100.0);
			c = bitmap_set_alpha(bm, c, bm->alpha);
			bitmap_draw_pixel(bm, x + x0, y + y0 + margin, c);
		}
	}
	if ((margin * 2) >= h)
		return 0;
	for (y = 0; y < margin; y++) {
		for (x = 0; x < w; x++) {
			c = bitmap_hsv_to_rgba(bm, 360.0 - (double)x * slope, 0,
					       val / 100.0);
			c = bitmap_set_alpha(bm, c, bm->alpha);
			bitmap_draw_pixel(bm, x + x0, y + y0 + margin + h, c);
		}
	}

	return 0;
}

int bitmap_16m_colors(bitmap_t *bm,
		      int x0, int y0,
		      int x1, int y1)
{
	uint32_t y, x, c=0, size;
	const uint32_t min_size = 256*256*256;

	//PRINTD2(bm->debug, "%s(): x0 %d y0 %d x1 %d y1 %d pixel 0x%08x\n",
	//	__func__, x0, y0, x1, y1, pixel);
	size = (x1 - x0) * (y1 - y0);

	if (size < min_size) {
		fprintf(stderr,
			"%s() bitmap is too small %u. Needs to be %u pixels or larger (e.g. 4096x4096)\n",
			__func__, size, min_size);
	}
	for (y = y0; y < y1; y++) {
		for (x = 0; x < (x1 - x0); x++) {
			bitmap_draw_pixel(bm, x + x0, y + y0, c);
			c++;
			if (c == size) {
				return 0;
			}

			if (c == min_size) {
				c = 0;
			}
		}
	}
	if ( c < min_size) {
		fprintf(stderr,
			"%s() bitmap is too small %u. Needs to be %u pixels or larger\n",
			__func__, size, min_size);
	}
	return 0;
}

int bitmap_corners(bitmap_t *bm, int margin)
{
	uint32_t black = 0, white = 0xffffff;

	bitmap_get_color(bm, "black", &black);
	bitmap_get_color(bm, "white", &white);

	PRINTD2(bm->debug, "%s(): margin is %d\n", __func__, margin);

	/* fill corners*/
	/* top left */
	bitmap_fill_rectangle(bm,
			      0, 0,
			      margin, margin,
			      black);

	bitmap_fill_rectangle(bm,
			      0, 0,
			      margin / 2, margin / 2,
			      white);

	bitmap_fill_rectangle(bm,
			      0, 0,
			      margin / 4, margin / 4,
			      black);

	bitmap_draw_pixel(bm, 0, 0, white);

	/* bottom right */
	bitmap_fill_rectangle(bm,
			      bm->w - margin, bm->h - margin,
			      bm->w, bm->h,
			      black);

	bitmap_fill_rectangle(bm,
			      bm->w - margin / 2, bm->h - margin / 2,
			      bm->w, bm->h,
			      white);

	bitmap_fill_rectangle(bm,
			      bm->w - margin / 4, bm->h - margin / 4,
			      bm->w, bm->h,
			      black);

	bitmap_draw_pixel(bm, bm->w - 1, bm->h - 1, white);

	/* bottom left */
	bitmap_fill_rectangle(bm,
			      0, bm->h - margin,
			      margin, bm->h,
			      black);
	bitmap_fill_rectangle(bm,
			      0, bm->h - margin / 2,
			      margin / 2, bm->h,
			      white);
	bitmap_fill_rectangle(bm,
			      0, bm->h - margin / 4,
			      margin / 4, bm->h,
			      black);
	bitmap_draw_pixel(bm, 0, bm->h - 1, white);

	bitmap_fill_rectangle(bm,
			      bm->w - margin, 0,
			      bm->w, margin,
			      black);
	bitmap_fill_rectangle(bm,
			      bm->w - margin / 2, 0,
			      bm->w, margin / 2,
			      white);
	bitmap_fill_rectangle(bm,
			      bm->w - margin / 4, 0,
			      bm->w, margin / 4,
			      black);
	bitmap_draw_pixel(bm, bm->w - 1, 0, white);

	return 0;
}

/* origin is the upper left corner */
static uint8_t image[HEIGHT][WIDTH];

/* Replace this function with something useful. */
static void draw_bitmap(bitmap_t *bm,
			FT_Bitmap *bitmap,
			FT_Int      x,
			FT_Int      y,
			uint32_t color)
{
	FT_Int  i, j, p, q;
	FT_Int  x_max = x + bitmap->width;
	FT_Int  y_max = y + bitmap->rows;

	/* for simplicity, we assume that `bitmap->pixel_mode' */
	/* is `FT_PIXEL_MODE_GRAY' (i.e., not a bitmap font)   */

	for (i = x, p = 0; i < x_max; i++, p++) {
		for (j = y, q = 0; j < y_max; j++, q++) {
			if (i < 0      || j < 0       ||
			    i >= WIDTH || j >= HEIGHT)
				continue;
			image[j][i] |= bitmap->buffer[q * bitmap->width + p];

			PRINTD5(bm->debug,
				"x %d y %d image 0x%08x\n",
				i, j, image[j][i]);
		}
	}
}

#ifdef DEBUG_FONT_RENDER
static void show_image(void)
{
	int  i, j;

	for (i = 0; i < HEIGHT; i++) {
		for (j = 0; j < WIDTH; j++) putchar(image[i][j] == 0 ? ' '
						    : image[i][j] < 128 ? '+'
						    : '*');
		putchar('\n');
	}
}
#endif

static int bitmap_copy_font(bitmap_t *bm,
			    uint32_t x0,
			    uint32_t y0,
			    uint32_t color)
{
	int  x, y;
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) {
			if (image[y][x] != 0) {
				bitmap_blend_pixel(bm, x + x0, y + y0,
						   color, image[y][x]);
				PRINTD5(bm->debug,
					"x %d y %d image 0x%08x\n", x, y,
					image[y][x]);
			}
		}
	}
	return 0;
}

int bitmap_render_font(bitmap_t *bm,
		       char *filename,
		       char *text,
		       int size,
		       uint32_t x0,
		       uint32_t y0,
		       uint32_t color)
{
	FT_Library    library;
	FT_Face       face;

	FT_GlyphSlot  slot;
	FT_Matrix     matrix;                 /* transformation matrix */
	FT_Vector     pen;                    /* untransformed origin  */
	FT_Error      error;

	double        angle;
	int           target_height;
	int           n, num_chars;

	num_chars     = strlen(text);
	angle         = DEG2RAD(0.0);
	target_height = HEIGHT;

	memset(image, 0, sizeof(image));

	error = FT_Init_FreeType(&library); /* initialize library */
	if (error) {
		fprintf(stderr,
			"FT_Init_FreeType: returned an error occurred during library initialization\n");
	}

	/* create face object */
	error = FT_New_Face(library, filename, 0, &face);
	if (error == FT_Err_Unknown_File_Format) {
		fprintf(stderr,
			"FT_New_Face: returned FT_Err_Unknown_File_Format error\n");
	} else if (error) {
		fprintf(stderr,
			"FT_New_Face: returned an error %d\n", error);
	}

	/* set character size */
	/* use 64pt at 64dpi  one point per pixel */
	error = FT_Set_Char_Size(face, size * 64, 0,
				 64, 0);
	if (error) {
		fprintf(stderr,
			"FT_Set_Char_Size: returned an error %d\n", error);
	}

	/* cmap selection omitted;                                        */
	/* for simplicity we assume that the font contains a Unicode cmap */

	slot = face->glyph;

	/* set up matrix */
	matrix.xx = (FT_Fixed)(cos(angle) * 0x10000L);
	matrix.xy = (FT_Fixed)(-sin(angle) * 0x10000L);
	matrix.yx = (FT_Fixed)(sin(angle) * 0x10000L);
	matrix.yy = (FT_Fixed)(cos(angle) * 0x10000L);

	pen.x = X_START * 64;
	pen.y = (target_height - (size + Y_START)) * 64;

	for (n = 0; n < num_chars; n++) {
		FT_Set_Transform(face, &matrix, &pen);

		error = FT_Load_Char(face, text[n], FT_LOAD_RENDER);
		if (error) {
			fprintf(stderr, "FT_Load_Char: returned an error %d\n",
				error);
			continue;                 /* ignore errors */
		}

		draw_bitmap(bm,
			    &slot->bitmap,
			    slot->bitmap_left,
			    target_height - slot->bitmap_top,
			    color);

		/* increment pen position */
		pen.x += slot->advance.x;
		pen.y += slot->advance.y;

		PRINTD5(bm->debug,
			"pen.x %ld pen.y %ld slot->advance.x %ld slot->advance.y %ld\n",
			pen.x, pen.y, slot->advance.x, slot->advance.y);
		PRINTD5(bm->debug,
			"pen.x %ld pen.y %ld slot->advance.x %ld slot->advance.y %ld\n",
			pen.x / 64, target_height - (pen.y / 64),
			slot->advance.x / 64, slot->advance.y / 64);
	}
#ifdef DEBUG_FONT_RENDER
	show_image();
#endif
	bitmap_copy_font(bm, x0 - (pen.x / 64) / 2,
			 y0 - (size / 2  + Y_START * 3), color);

	FT_Done_Face(face);
	FT_Done_FreeType(library);

	return 0;
}

