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

void fb_test_bpp(int fd, unsigned short * fb)
{
        int i;
        __u32 screensize;
        int retval;
        struct fb_var_screeninfo screen_info;

        retval = ioctl(fd, FBIOGET_VSCREENINFO, &screen_info);
        if (retval < 0)
        {
                return;
        }

        printf("Set the background to 32-bpp\n");
        screen_info.bits_per_pixel = 32;
        retval = ioctl(fd, FBIOPUT_VSCREENINFO, &screen_info);
        if (retval < 0)
        {
                return;
        }
        printf("fb_test: xres_virtual = %d\n", screen_info.xres_virtual);
        screensize = screen_info.xres * screen_info.yres * screen_info.bits_per_pixel / 8;

        printf("Fill the BG in red, size = 0x%08X\n", screensize);
        for (i = 0; i < screensize/4; i++)
                ((__u32*)fb)[i] = 0x00FF0000;
        sleep(3);

        printf("Set the BG to 24-bpp\n");
        screen_info.bits_per_pixel = 24;
        retval = ioctl(fd, FBIOPUT_VSCREENINFO, &screen_info);
        if (retval < 0)
        {
                return;
        }
        printf("fb_test: xres_virtual = %d\n", screen_info.xres_virtual);
        screensize = screen_info.xres * screen_info.yres * screen_info.bits_per_pixel / 8;

        printf("Fill the BG in blue, size = 0x%08X\n", screensize);
        for (i = 0; i < screensize; ) {
                ((__u8*)fb)[i++] = 0xFF;       // Blue
                ((__u8*)fb)[i++] = 0x00;       // Green
                ((__u8*)fb)[i++] = 0x00;       // Red
        }
        sleep(3);

        printf("Set the background to 16-bpp\n");
        screen_info.bits_per_pixel = 16;
        retval = ioctl(fd, FBIOPUT_VSCREENINFO, &screen_info);
        if (retval < 0)
        {
                return;
        }
        screensize = screen_info.xres * screen_info.yres * screen_info.bits_per_pixel / 8;

        printf("Fill the BG in green, size = 0x%08X\n", screensize);
        for (i = 0; i < screensize/2; i++)
                fb[i] = 0x07E0;
        sleep(3);

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
	if ((int)fb->fb <= 0) {
		fprintf(stderr, "Error: failed mapping framebuffer %s to memory!\n", fb->name);
		return TFAIL;
	}

	return retval;
}


void cleanup_fb(struct fb_info *fb)
{
	if ((int)fb->fb > 0 && fb->size > 0)
		munmap(fb->fb, fb->size);
	if (fb->fd > 0)
		close(fb->fd);
	memset(fb, 0, sizeof(struct fb_info));
}

int
main(int argc, char **argv)
{
	int retval = TPASS;
	struct mxcfb_gbl_alpha gbl_alpha;

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

	// Leave the screen black before exiting the test
	memset(fb0.fb, 0, fb0.size);
exit:
	cleanup_fb(&fb0);
	cleanup_fb(&fb1);

	return retval;
}

