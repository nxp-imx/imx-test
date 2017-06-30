/*
 * Copyright 2004-2011 Freescale Semiconductor, Inc. All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 * @file mxc_fb_test.c
 *
 * @brief Mxc framebuffer driver test application
 *
 */

#ifdef __cplusplus
extern "C"{
#endif

/*=======================================================================
                                        INCLUDE FILES
=======================================================================*/
/* Standard Include Files */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/mxcfb.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <getopt.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define TFAIL -1
#define TPASS 0

struct fb_info {
	int id;
	int fd;
	unsigned short *fb;
	int size;
	char name[16];
	int xpanstep;
	int ypanstep;
	struct fb_var_screeninfo screen_info;
};
struct fb_info fb0, fb1;

struct img_file {
	char fname[256];
	size_t xres;
	size_t yres;
	int bpp;
};

/* Maximum resolution to use in a framebuffer (X or Y) */
int max_res = 1920;
/* Use this to 1 if no user input is expected (when running this test from a script) */
int nouser = 0;

static char *options = "m:n";
char *usage = "Usage: ./mxc_fb_test.out [options]\n" \
	       "options:\n" \
	       "\t-m <max_res>\t Maximum X/Y resolution to use when searching files to display\n" \
	       "\t-n\t Expect no user input: with this option, the test can be ran from a script";

int set_screen(struct fb_info *fb, int xres, int yres, int xres_virtual, int yres_virtual, int bpp)
{
	int ret = 0;
	int new_size;
	struct fb_var_screeninfo screen_info;

	memcpy(&screen_info, &fb->screen_info, sizeof(screen_info));
	screen_info.xres = xres;
	screen_info.xres_virtual = xres_virtual;
	screen_info.yres = yres;
	screen_info.yres_virtual = yres_virtual;
	screen_info.bits_per_pixel = bpp;
	if ((ret = ioctl(fb->fd, FBIOPUT_VSCREENINFO, &screen_info)) < 0) {
		fprintf(stderr, "@%s: Failed setting screen to %dx%d (virtual: %dx%d) @%d-bpp\n", fb->name, xres, yres, xres_virtual, yres_virtual, bpp);
		return ret;
	}
	fb->screen_info.xres = xres;
	fb->screen_info.xres_virtual = xres_virtual;
	fb->screen_info.yres = yres;
	fb->screen_info.yres_virtual = yres_virtual;
	fb->screen_info.bits_per_pixel = bpp;
	printf("@%s: Succesfully changed screen to %dx%d (virtual: %dx%d) @%d-bpp\n", fb->name, xres, yres, xres_virtual, yres_virtual, bpp);
	new_size = screen_info.xres_virtual * screen_info.yres_virtual * bpp / 8;
	// If the new size requires larger memory, remap it
	if (new_size > fb->size) {
		munmap(fb->fb, fb->size);
		fb->fb = (unsigned short *)mmap(0, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
		if (fb->fb == MAP_FAILED) {
			fprintf(stderr, "@%s: Failed to remap framebuffer to memory (%d)\n", fb->name, errno);
			munmap(fb->fb, new_size);
			return errno;
		}
		fb->size = new_size;
	}
	return ret;

}

int fb_test_bpp(struct fb_info *fb)
{
        int i;
        int retval = 0;

        printf("Test bpp start\n");
        printf("@%s: Set colorspace to 32-bpp\n", fb->name);
        fb->screen_info.bits_per_pixel = 32;
        if ((retval = ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->screen_info)) < 0) {
                fprintf(stderr, "@%s: Could not set screen info!\n", fb->name);
                return retval;
        }

        printf("@%s: Fill the screen in red\n", fb->name);
        for (i = 0; i < fb->size / 4; i++) {
                ((__u32*)fb->fb)[i] = 0x00FF0000;
        }
        sleep(3);

        printf("@%s: Set colorspace to 24-bpp\n", fb->name);
        fb->screen_info.bits_per_pixel = 24;
        if ((retval = ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->screen_info)) < 0) {
                fprintf(stderr, "@%s: Could not set screen info!\n", fb->name);
                return retval;
        }

        printf("@%s: Fill the screen in blue\n", fb->name);
        for (i = 0; i < fb->size; ) {
                ((__u8*)fb->fb)[i++] = 0xFF;       // Blue
                ((__u8*)fb->fb)[i++] = 0x00;       // Green
                ((__u8*)fb->fb)[i++] = 0x00;       // Red
        }
        sleep(3);

        printf("@%s: Set colorspace to 16-bpp\n", fb->name);
        fb->screen_info.bits_per_pixel = 16;
        if ((retval = ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->screen_info)) < 0) {
                fprintf(stderr, "@%s: Could not set screen info!\n", fb->name);
                return retval;
        }

        printf("@%s: Fill the screen in green\n", fb->name);
        for (i = 0; i < fb->size / 2; i++)
                fb->fb[i] = 0x07E0;
        sleep(3);

        printf("Test bpp end\n");
        return retval;
}

void fb_test_gbl_alpha(void)
{
        int i;
        int retval;
        volatile int delay;
        struct mxcfb_gbl_alpha gbl_alpha;
        struct mxcfb_color_key key;

        printf("Testing global alpha blending...\n");

        printf("Fill the FG in black (screen is %dx%d @ %d-bpp)\n",
                       fb1.screen_info.xres,
                       fb1.screen_info.yres,
                       fb1.screen_info.bits_per_pixel);
        memset(fb1.fb, 0x0, fb1.size);
        sleep(2);

        printf("Fill the BG in white (screen is %dx%d @ %d-bpp)\n",
                       fb0.screen_info.xres,
                       fb0.screen_info.yres,
                       fb0.screen_info.bits_per_pixel);
        memset(fb0.fb, 0xFF, fb0.size);
        sleep(2);

        gbl_alpha.enable = 1;
        for (i = 0; i < 0x100; i++) {
                delay = 1000;

                gbl_alpha.alpha = i;
                retval = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);

                // Wait for VSYNC
                retval = ioctl(fb0.fd, MXCFB_WAIT_FOR_VSYNC, 0);
                if (retval < 0) {
                        printf("Error waiting on VSYNC\n");
                        break;
                }
        }

        for (i = 0xFF; i > 0; i--) {
                delay = 1000;

                gbl_alpha.alpha = i;
                retval = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);

                while (delay--) ;
        }
        printf("Alpha is 0, FG is opaque\n");
        sleep(3);

        gbl_alpha.alpha = 0xFF;
        retval = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
        printf("Alpha is 255, BG is opaque\n");
        sleep(3);

        key.enable = 1;
        key.color_key = 0x00FF0000; // Red
        retval = ioctl(fb0.fd, MXCFB_SET_CLR_KEY, &key);

        for (i = 0; i < 240*80; i++)
                fb0.fb[i] = 0xF800;

        printf("Color key enabled\n");
        sleep(3);

        key.enable = 0;
        retval = ioctl(fb0.fd, MXCFB_SET_CLR_KEY, &key);
        printf("Color key disabled\n");


        gbl_alpha.enable = 0;
        retval = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
        printf("Global alpha disabled\n");
        sleep(3);

}

void fb_test_pan(struct fb_info *fb)
{
        int x, y;
        int color = 0;

        printf("Pan test start.\n");

	if (fb->screen_info.yres_virtual == fb->screen_info.yres)
		fb->screen_info.yres_virtual += fb->screen_info.yres / 2;
        printf("@%s: Set the colorspace to 16-bpp\n", fb->name);
        fb->screen_info.bits_per_pixel = 16;
        if (ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->screen_info) < 0) {
                fprintf(stderr, "@%s: Could not set screen info!\n", fb->name);
                return;
        }

        for (y = 0; y < fb->screen_info.yres_virtual; y++) {
                for (x = 0; x < fb->screen_info.xres; x++) {
                        fb->fb[(y * fb->screen_info.xres) + x] = color;
                }
                color+=4;
        }

        for (y = 0; y <= fb->screen_info.yres; y += fb->ypanstep) {
                fb->screen_info.yoffset = y;
                if (ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->screen_info) < 0)
                        break;
        }
	fb->screen_info.yoffset = 0;
	ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->screen_info);
        printf("Pan test done.\n");
}

void get_gamma_coeff(float gamma, int constk[], int slopek[])
{
	unsigned int tk[17] = {0, 2, 4, 8, 16, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 383};
        unsigned int k;
        unsigned int rk[16], yk[17];
        float center, width, start, height, t, scurve[17], gammacurve[17];

        center = 32; /* nominal 32 - must be in {1,380} where is s-curve centered*/
        width = 32; /* nominal 32 - must be in {1,380} how narrow is s-curve */
        for(k=0;k<17;k++) {
                t = (float)tk[k];
                scurve[k] = (256.F/3.14159F)*((float) atan((t-center)/width));
                gammacurve[k] = 255.F* ((float) pow(t/384.F,1.0/gamma));
        }

        start = scurve[0];
        height = scurve[16] - start;
        for(k=0;k<17;k++) {
                scurve[k] = (256.F/height)*(scurve[k]-start);
                yk[k] = (int)(scurve[k] * gammacurve[k]/256.F + 0.5) << 1;
        }

        for(k=0;k<16;k++)
                rk[k] = yk[k+1] - yk[k];

        for(k=0;k<5;k++) {
                constk[k] = yk[k] - rk[k];
                slopek[k] = rk[k] << (4-k);
        }
        for(k=5;k<16;k++) {
                constk[k] = yk[k];
                slopek[k] = rk[k] >> 1;
        }

	for(k=0;k<16;k++) {
		constk[k] /= 2;
		constk[k] &= 0x1ff;
	}
}

void fb_test_gamma(int fd_fb)
{
	struct mxcfb_gamma gamma;
	float gamma_tc[5] = {0.8, 1.0, 1.5, 2.2, 2.4};
	int i, retval;

        printf("Gamma test start.\n");
	for (i=0;i<5;i++) {
		printf("Gamma %f\n", gamma_tc[i]);
		get_gamma_coeff(gamma_tc[i], gamma.constk, gamma.slopek);
		gamma.enable = 1;
		retval = ioctl(fd_fb, MXCFB_SET_GAMMA, &gamma);
		if (retval < 0) {
			printf("Ioctl MXCFB_SET_GAMMA fail!\n");
			break;
		}
		sleep(3);
	}

        printf("Gamma test end.\n");
}

/* fb_test_file: Uses an image file to fill a framebuffer
 * Parameters:
 * 	fb: framebuffer where the image will be displayed
 * 	img: image structure containing details of the image file used to fill
 * 	the framebuffer
 * 	first: if this is the first test ran with an image file, do some additional
 * 	tests with the framebuffer: alpha-blending and panning; these tests will be
 * 	executed only on the background framebuffer (id == 1), so it will blend with
 * 	the foreground framebuffer (id == 0)
 */
int fb_test_file(struct fb_info *fb, struct img_file* img, int first)
{
	int ret = 0;
	char c = 0;
	int fd;
	volatile int delay;
	size_t fsize = 0, rdsize = 0;
	struct mxcfb_gbl_alpha gbl_alpha;

	if (img->bpp != 16 && img->bpp != 32) {
		return ret;
	}
	if (img->xres < 64 || img->yres < 64 ||
		img->xres > max_res || img->yres > max_res) {
		printf("Invalid resolution: %zdx%zd (file: %s)\n", img->xres, img->yres, img->fname);
		return ret;
	}

	if ((fd = open(img->fname, O_RDONLY, 0)) < 0) {
		printf("Unable to open img file %s\n", img->fname);
		return ret;
	}
	fsize = lseek(fd, 0, SEEK_END);
	if (fsize == 0)
		return ret;
	lseek(fd, 0, SEEK_SET);

	// If this is the first test, use a larger virtual screen, for X/Y panning test
	if (first)
		ret = set_screen(fb, img->xres, img->yres, img->xres + img->xres / 2, img->yres + img->yres / 2, img->bpp);
	else
		ret = set_screen(fb, img->xres, img->yres, img->xres, img->yres, img->bpp);
	if (ret < 0)
		return ret;
	gbl_alpha.enable = 1;
	if (fb->id == 1)
		gbl_alpha.alpha = 0x0;
	else
		gbl_alpha.alpha = 0xFF;
	ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);

	memset(fb->fb, 0xFF, fb->size);
	if (nouser) {
		sleep(1);
	} else {
		printf("@%s: Screen should be %dx%d white. Verify the screen and press any key to continue!\n",
			fb->name, fb->screen_info.xres, fb->screen_info.yres);
		scanf("%c", &c);
	}

	if (fsize > fb->size) {
		printf("FB size:%d is smaller than file size: %zd!\n", fb->size, fsize);
		return ret;
	}
	if (fb->id == 1) {
		gbl_alpha.alpha = 0xFF;
		ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
	}

	printf("@%s: Using %s to fill the screen...\n", fb->name, img->fname);
	if (fb->screen_info.xres < fb->screen_info.xres_virtual) {
		void *fb_ptr = fb->fb;
		int y;
		// Copy the image line by line
		for (y = 0; y < fb->screen_info.yres; y++) {
			rdsize += read(fd, fb_ptr, (fb->screen_info.xres * (fb->screen_info.bits_per_pixel / 8)));
			fb_ptr += (fb->screen_info.xres_virtual * (fb->screen_info.bits_per_pixel / 8));
		}
		fprintf(stderr, "Read %zd, expected %zd\n", rdsize, fsize);
	} else {
		if ((rdsize = read(fd, fb->fb, fsize)) != fsize)
			fprintf(stderr, "Warning: Read %zd, expected %zd\n", rdsize, fsize);
	}
	if (fb->id == 1) {
		if (first) {
			int i;
			printf("@%s: Test alpha blend (image should fade in)...\n", fb->name);
			for (i = 0xFF; i > 0; i--) {
				delay = 1000;
				gbl_alpha.alpha = i;
				ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
				ioctl(fb0.fd, MXCFB_WAIT_FOR_VSYNC, 0);
				while (delay--) ;
			}
			if (nouser) {
				sleep(1);
			} else {
				printf("Verify the screen and press any key to continue!\n");
				scanf("%c", &c);
			}
			int p, ret;
			printf("@%s: Test y panning...\n", fb->name);
			for (p = 0; fb->ypanstep > 0 && p <= fb->screen_info.yres; p += fb->ypanstep) {
				fb->screen_info.yoffset = p;
				if((ret = ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->screen_info)) < 0)
					break;
			}
			fb->screen_info.yoffset = 0;
			printf("@%s: Test x panning...\n", fb->name);
			for (p = 0; fb->xpanstep > 0 && p <= fb->screen_info.xres; p += fb->xpanstep) {
				fb->screen_info.xoffset = p;
				if((ret = ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->screen_info)) < 0)
					break;
			}
			fb->screen_info.xoffset = 0;
			ioctl(fb->fd, FBIOPAN_DISPLAY, &fb->screen_info);
		} else {
			gbl_alpha.alpha = 0;
			ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
		}
	}
	if (nouser) {
		sleep(1);
	} else {
		printf("Verify the screen and press any key to continue!\n");
		scanf("%c", &c);
	}
	return ret;
}


/* is_image_file: Check if a filename has the format: <name>-<xres>x<yres>-<bpp>.rgb
 * Examples:
 * 	testcard-1920x1080-bgra.rgb
 * 	testcard-640x480-565.rgb
 *
 * If the file matches the format, it will fill the img_file with parsed params
 */
int is_image_file(char *fname, struct img_file *str)
{
	char *ptr, *ptr2;

	if ((ptr = strrchr(fname, '.')) == NULL)
		return 0;
	if (!strncmp(ptr, ".rgb", 4)) {
		// this is an image file
		strcpy(str->fname, fname);
		str->fname[strlen(fname)] = 0;
		*ptr = 0;
		if ((ptr = strrchr(fname, '-')) == NULL)
			return 0;
		ptr++;
		if (!strncmp(ptr, "565", 3)) {
			str->bpp = 16;
		} else if (!strncmp(ptr, "bgra", 4)) {
			str->bpp = 32;
		} else {
			return 0;
		}
		ptr--;
		*ptr = 0;
		if ((ptr = strrchr(fname, '-')) == NULL)
			return 0;
		ptr++;
		if ((ptr2 = strchr(ptr, 'x')) == NULL) {
			return 0;
		}
		ptr2++;
		str->yres = atoi(ptr2);
		ptr2--;
		ptr2 = NULL;
		str->xres = atoi(ptr);
		return 1;
	}
	return 0;
}

/* fb_test_images: Parses the current directory for image files and will use
 *                 them to fill BG and FG framebuffers
 */
int fb_test_images(void)
{
	DIR *dir;
	char c;
	int ret = 0;
	struct dirent *ent;
	struct img_file img, fb0_state, fb1_state;
	struct mxcfb_gbl_alpha gbl_alpha;
	int first_test = 1;

	if ((dir = opendir(".")) == NULL) {
		fprintf(stderr, "Cannot open current directory!\n");
		return ret;
	}

	// use img_file struct to save fb0 and fb1 resolution and bpp
	fb0_state.xres = fb0.screen_info.xres;
	fb0_state.yres = fb0.screen_info.yres;
	fb0_state.bpp  = fb0.screen_info.bits_per_pixel;
	fb1_state.xres = fb1.screen_info.xres;
	fb1_state.yres = fb1.screen_info.yres;
	fb1_state.bpp  = fb1.screen_info.bits_per_pixel;

	gbl_alpha.enable = 1;
	gbl_alpha.alpha = 0x0;
	if ((ret = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha)) < 0) {
		/* TODO:
		 * We failed to reset global alpha, not sure if we should abandon
		 * or just print the error and continue in this state.
		 */
		fprintf(stderr, "Failed to reset global alpha! (%d)\n", ret);
	}

	memset(fb0.fb, 0x00, fb0.size);
	ioctl(fb0.fd, MXCFB_WAIT_FOR_VSYNC, 0);
	memset(fb1.fb, 0xFF, fb1.size);
	ioctl(fb1.fd, MXCFB_WAIT_FOR_VSYNC, 0);
	if (nouser) {
		sleep(1);
	} else {
		printf("Prepared %s (black) and %s (white). Verify the screen and press any key to continue!\n",
			fb0.name, fb1.name);
		scanf("%c", &c);
	}

	while ((ent = readdir(dir)) != NULL) {
		if (is_image_file(ent->d_name, &img)) {
			if (img.xres >= fb0.screen_info.xres && img.yres >= fb0.screen_info.yres)
				ret = fb_test_file(&fb0, &img, 0);
			else
				ret = fb_test_file(&fb1, &img, first_test);
			// For the first sample, test alpha blending and X/Y panning
			first_test = 0;
			if (ret < 0)
				break;
		}
	}

	closedir(dir);

	// Restore initial states for fb0 and fb1
	if (ret < 0 || (ret = set_screen(&fb0, fb0_state.xres, fb0_state.yres, fb0_state.xres, fb0_state.yres, fb0_state.bpp)) < 0)
		return ret;
	if (ret < 0 || (ret = set_screen(&fb1, fb1_state.xres, fb1_state.yres, fb1_state.xres, fb1_state.yres, fb1_state.bpp)) < 0)
		return ret;
	// Reset the framebuffers to black
	memset(fb0.fb, 0, fb0.size);
	ioctl(fb0.fd, MXCFB_WAIT_FOR_VSYNC, 0);
	memset(fb1.fb, 0, fb1.size);
	ioctl(fb1.fd, MXCFB_WAIT_FOR_VSYNC, 0);
	sleep(2);
	return ret;
}



int setup_fb(struct fb_info *fb, int id)
{
	int retval = TPASS;
	struct fb_fix_screeninfo fb_fix;
	char path[16];

	memset(fb, 0, sizeof(struct fb_info));
	snprintf(&path[0], ARRAY_SIZE(path), "/dev/fb%d", id);
	fb->id = id;
	if ((fb->fd = open(path, O_RDWR, 0)) < 0) {
		fprintf(stderr, "Unable to open %s\n", path);
		return TFAIL;
	}
	if ((retval = ioctl(fb->fd, FBIOBLANK, FB_BLANK_UNBLANK)) < 0) {
		fprintf(stderr, "Unable to unblank %s\n", path);
		return retval;
	}
	if ((retval = ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb_fix)) < 0) {
		fprintf(stderr, "Could not get fix screen info for %s\n", path);
		return retval;
	}
	strcpy(fb->name, fb_fix.id);
	fb->xpanstep = fb_fix.xpanstep;
	fb->ypanstep = fb_fix.ypanstep;
	printf("Opened fb: %s (%s)\n", path, fb->name);


	if ((retval = ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->screen_info)) < 0) {
		fprintf(stderr, "Could not get screen info for %s\n", path);
		return retval;
	}
	printf("%s: screen info: %dx%d (virtual: %dx%d) @ %d-bpp\n\n",
			fb->name,
			fb->screen_info.xres,
			fb->screen_info.yres,
			fb->screen_info.xres_virtual,
			fb->screen_info.yres_virtual,
			fb->screen_info.bits_per_pixel);

	/* Map the device to memory*/
	fb->size = fb->screen_info.xres_virtual * fb->screen_info.yres_virtual * fb->screen_info.bits_per_pixel / 8;
	fb->fb = (unsigned short *)mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->fb == MAP_FAILED) {
		fprintf(stderr, "Error: failed mapping framebuffer %s to memory!\n", fb->name);
		return TFAIL;
	}

	return retval;
}


void cleanup_fb(struct fb_info *fb)
{
	if (fb->fb != MAP_FAILED && fb->size > 0)
		munmap(fb->fb, fb->size);
	if (fb->fd > 0)
		close(fb->fd);
	memset(fb, 0, sizeof(struct fb_info));
}

void print_usage(void)
{
	printf("%s", usage);
}

int parse_args(int argc, char **argv)
{
	int ret = 0;
	int opt;

	do {
		opt = getopt(argc, argv, options);
		switch (opt) {
		case 'm':
			max_res = atoi(optarg);
			if (max_res <= 0) {
				fprintf(stderr, "Invalid max_res: %s\n", optarg);
				ret = -1;
			}
			break;
		case 'n':
			nouser = 1;
			break;
		}
	} while ((opt != -1) && (ret == 0));

	return ret;
}

int
main(int argc, char **argv)
{
	int retval = TPASS;
	struct mxcfb_gbl_alpha gbl_alpha;

	if (parse_args(argc, argv) < 0) {
		print_usage();
		return retval;
	}

	if ((retval = setup_fb(&fb0, 0)) < 0)
		goto exit;
	if ((retval = setup_fb(&fb1, 1)) < 0)
		goto exit;

	printf("@%s: Set colorspace to 16-bpp\n", fb0.name);
	fb0.screen_info.bits_per_pixel = 16;
	fb0.screen_info.yoffset = 0;
	if ((retval = ioctl(fb0.fd, FBIOPUT_VSCREENINFO, &fb0.screen_info)) < 0) {
		fprintf(stderr, "@%s: Could not set screen info!\n", fb0.name);
		goto exit;
	}

	printf("@%s: Set colorspace to 16-bpp\n", fb1.name);
	fb1.screen_info.bits_per_pixel = 16;
	fb1.screen_info.yoffset = 0;
	if ((retval = ioctl(fb1.fd, FBIOPUT_VSCREENINFO, &fb1.screen_info)) < 0) {
		fprintf(stderr, "@%s: Could not set screen info!\n", fb1.name);
		goto exit;
	}

	fb_test_images();
	fb_test_gbl_alpha();
	fb_test_pan(&fb1);

	// Set BG to visible
	gbl_alpha.alpha = 0xFF;
	retval = ioctl(fb0.fd, MXCFB_SET_GBL_ALPHA, &gbl_alpha);
	retval = ioctl(fb0.fd, FBIOGET_VSCREENINFO, &fb0.screen_info);
	if (retval < 0) {
		fprintf(stderr, "@%s: Could not get screen info!\n", fb0.name);
		goto exit;
	}
	printf("@%s: Set colorspace to 16-bpp\n", fb0.name);
	fb0.screen_info.bits_per_pixel = 16;
	if ((retval = ioctl(fb0.fd, FBIOPUT_VSCREENINFO, &fb0.screen_info)) < 0) {
		fprintf(stderr, "@%s: Could not set screen info!\n", fb0.name);
		goto exit;
	}

	fb_test_pan(&fb0);
	fb_test_gamma(fb0.fd);
	fb_test_bpp(&fb0);

	// Leave the screen black before exiting the test
	memset(fb0.fb, 0, fb0.size);
exit:
	cleanup_fb(&fb0);
	cleanup_fb(&fb1);

	return retval;
}

