/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP
 *
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/* 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer 
   in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from 
   this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, 
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE 
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#ifndef _DEC_H
#define _DEC_H

#include <linux/videodev2.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <semaphore.h>
#include "mxc_ipu_hl_lib.h"
#include "vpu_lib.h"
#include "vpu_io.h"
#ifdef BUILD_FOR_ANDROID
#include "g2d.h"
#endif


#define COMMON_INIT

extern int vpu_test_dbg_level;

#define dprintf(level, fmt, arg...)     if (vpu_test_dbg_level >= level) \
        printf("[DEBUG]\t%s:%d " fmt, __FILE__, __LINE__, ## arg)

#define err_msg(fmt, arg...) do { if (vpu_test_dbg_level >= 1)		\
	printf("[ERR]\t%s:%d " fmt,  __FILE__, __LINE__, ## arg); else \
	printf("[ERR]\t" fmt, ## arg);	\
	} while (0)
#define info_msg(fmt, arg...) do { if (vpu_test_dbg_level >= 1)		\
	printf("[INFO]\t%s:%d " fmt,  __FILE__, __LINE__, ## arg); else \
	printf("[INFO]\t" fmt, ## arg);	\
	} while (0)
#define warn_msg(fmt, arg...) do { if (vpu_test_dbg_level >= 1)		\
	printf("[WARN]\t%s:%d " fmt,  __FILE__, __LINE__, ## arg); else \
	printf("[WARN]\t" fmt, ## arg);	\
	} while (0)

#undef u32
#undef u16
#undef u8
#undef s32
#undef s16
#undef s8
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef long s32;
typedef short s16;
typedef char s8;

#define SZ_4K			(4 * 1024)

#define STREAM_BUF_SIZE		0x200000
#define STREAM_FILL_SIZE	0x40000
#define STREAM_READ_SIZE	(512 * 8)
#define STREAM_END_SIZE		0
#define PS_SAVE_SIZE		0x080000
#define VP8_MB_SAVE_SIZE	0x080000
#define MPEG4_SCRATCH_SIZE	0x080000
#define MJPG_FILL_SIZE		(8 * 1024)

#define STREAM_ENC_PIC_RESET 	1

#define PATH_V4L2	0
#define PATH_FILE	1
#define PATH_NET	2
#define PATH_IPU	3
#ifdef BUILD_FOR_ANDROID
#define PATH_G2D	4
#endif

/* Test operations */
#define ENCODE		1
#define DECODE		2
#define LOOPBACK	3
#define TRANSCODE	4

#define DEFAULT_PORT		5555
#define DEFAULT_PKT_SIZE	0x28000

#define SIZE_USER_BUF            0x1000
#define USER_DATA_INFO_OFFSET    8*17

enum {
    MODE420 = 0,
    MODE422 = 1,
    MODE224 = 2,
    MODE444 = 3,
    MODE400 = 4
};

struct frame_buf {
	int addrY;
	int addrCb;
	int addrCr;
	int strideY;
	int strideC;
	int mvColBuf;
	vpu_mem_desc desc;
};

struct v4l_buf {
	void *start;
	off_t offset;
	size_t length;
};

#define MAX_BUF_NUM	32
#define QUEUE_SIZE	(MAX_BUF_NUM + 1)
struct v4l_specific_data {
	struct v4l2_buffer buf;
	struct v4l_buf *buffers[MAX_BUF_NUM];
};

#ifdef BUILD_FOR_ANDROID
struct g2d_specific_data {
	struct g2d_buf *g2d_bufs[MAX_BUF_NUM];
};
#endif

struct buf_queue {
	int list[MAX_BUF_NUM + 1];
	int head;
	int tail;
};

struct ipu_buf {
	int ipu_paddr;
	void * ipu_vaddr;
	int field;
};

struct vpu_display {
	int fd;
	int nframes;
	int ncount;
	time_t sec;
	int queued_count;
	int dequeued_count;
	suseconds_t usec;
	int frame_size;

	pthread_t disp_loop_thread;

	sem_t avaiable_decoding_frame;
	sem_t avaiable_dequeue_frame;

	struct buf_queue display_q;
	struct buf_queue released_q;
	int stopping;
	int deinterlaced;
	void *render_specific_data;

	/* ipu lib renderer */
	ipu_lib_handle_t ipu_handle;
	ipu_lib_input_param_t input;
	ipu_lib_output_param_t output;
	pthread_t ipu_disp_loop_thread;
	struct buf_queue ipu_q;
	struct ipu_buf ipu_bufs[MAX_BUF_NUM];
};

struct capture_testbuffer {
	size_t offset;
	unsigned int length;
};

struct rot {
	int rot_en;
	int ext_rot_en;
	int rot_angle;
};

#define MAX_PATH	256
struct cmd_line {
	char input[MAX_PATH];	/* Input file name */
	char output[MAX_PATH];  /* Output file name */
	int src_scheme;
	int dst_scheme;
	int video_node;
	int video_node_capture;
	int src_fd;
	int dst_fd;
	int width;
	int height;
	int enc_width;
	int enc_height;
	int loff;
	int toff;
	int format;
	int deblock_en;
	int dering_en;
	int rot_en; /* Use VPU to do rotation */
	int ext_rot_en; /* Use IPU/GPU to do rotation */
	int rot_angle;
	int mirror;
	int chromaInterleave;
	int bitrate;
	int gop;
	int save_enc_hdr;
	int count;
	int prescan;
	int bs_mode;
	char *nbuf; /* network buffer */
	int nlen; /* remaining data in network buffer */
	int noffset; /* offset into network buffer */
	int seq_no; /* seq numbering to detect skipped frames */
	u16 port; /* udp port number */
	u16 complete; /* wait for the requested buf to be filled completely */
	int iframe;
	int mp4_h264Class;
	char vdi_motion;	/* VDI motion algorithm */
	int fps;
	int mapType;
	int quantParam;
};

struct decode {
	DecHandle handle;
	PhysicalAddress phy_bsbuf_addr;
	PhysicalAddress phy_ps_buf;
	PhysicalAddress phy_slice_buf;
	PhysicalAddress phy_vp8_mbparam_buf;

	int phy_slicebuf_size;
	int phy_vp8_mbparam_size;
	u32 virt_bsbuf_addr;
	int picwidth;
	int picheight;
	int stride;
	int mjpg_fmt;
	int regfbcount;
	int minfbcount;
	int rot_buf_count;
	int extrafb;
	FrameBuffer *fb;
	struct frame_buf **pfbpool;
	struct vpu_display *disp;
	vpu_mem_desc *mvcol_memdesc;
	Rect picCropRect;
	int reorderEnable;
	int tiled2LinearEnable;
	int post_processing;

	DecReportInfo mbInfo;
	DecReportInfo mvInfo;
	DecReportInfo frameBufStat;
	DecReportInfo userData;

	struct cmd_line *cmdl;

	int decoded_field[32];
	int lastPicWidth;
	int lastPicHeight;

	int mjpgLineBufferMode;
	u32 mjpg_wr_ptr;
	u32 mjpg_rd_ptr;
	int mjpg_sc_state; /* start code FSM state */
	int mjpg_eof;
	u8 *mjpg_cached_bsbuf;
	int mjpegScaleDownRatioWidth;
	int mjpegScaleDownRatioHeight;

	struct frame_buf fbpool[MAX_BUF_NUM];
};

struct encode {
	EncHandle handle;		/* Encoder handle */
	PhysicalAddress phy_bsbuf_addr; /* Physical bitstream buffer */
	u32 virt_bsbuf_addr;		/* Virtual bitstream buffer */
	int enc_picwidth;	/* Encoded Picture width */
	int enc_picheight;	/* Encoded Picture height */
	int src_picwidth;        /* Source Picture width */
	int src_picheight;       /* Source Picture height */
	int totalfb;	/* Total number of framebuffers allocated */
	int src_fbid;	/* Index of frame buffer that contains YUV image */
	FrameBuffer *fb; /* frame buffer base given to encoder */
	struct frame_buf **pfbpool; /* allocated fb pointers are stored here */
	ExtBufCfg scratchBuf;
	int mp4_dataPartitionEnable;
	int ringBufferEnable;
	int mjpg_fmt;
	int mvc_paraset_refresh_en;
	int mvc_extension;
	int linear2TiledEnable;
	int minFrameBufferCount;
	int avc_vui_present_flag;

        EncReportInfo mbInfo;
        EncReportInfo mvInfo;
        EncReportInfo sliceInfo;

	struct cmd_line *cmdl; /* command line */
	u8 * huffTable;
	u8 * qMatTable;

	struct frame_buf fbpool[MAX_BUF_NUM];
};

int fwriten(int fd, void *vptr, size_t n);
int freadn(int fd, void *vptr, size_t n);
int vpu_read(struct cmd_line *cmd, char *buf, int n);
int vpu_write(struct cmd_line *cmd, char *buf, int n);
void get_arg(char *buf, int *argc, char *argv[]);
int open_files(struct cmd_line *cmd);
void close_files(struct cmd_line *cmd);
int check_params(struct cmd_line *cmd, int op);
char*skip_unwanted(char *ptr);
int parse_options(char *buf, struct cmd_line *cmd, int *mode);

struct vpu_display *v4l_display_open(struct decode *dec, int nframes,
					struct rot rotation, Rect rotCrop);
int v4l_get_buf(struct decode *dec);
int v4l_put_data(struct decode *dec, int index, int field, int fps);
void v4l_display_close(struct vpu_display *disp);
struct frame_buf *framebuf_alloc(struct frame_buf *fb, int stdMode, int format, int strideY, int height, int mvCol);
int tiled_framebuf_base(FrameBuffer *fb, Uint32 frame_base, int strideY, int height, int mapType);
struct frame_buf *tiled_framebuf_alloc(struct frame_buf *fb, int stdMode, int format, int strideY, int height, int mvCol, int mapType);
void framebuf_free(struct frame_buf *fb);

struct vpu_display *
ipu_display_open(struct decode *dec, int nframes, struct rot rotation, Rect cropRect);
void ipu_display_close(struct vpu_display *disp);
int ipu_put_data(struct vpu_display *disp, int index, int field, int fps);

#ifdef BUILD_FOR_ANDROID
struct vpu_display *
android_display_open(struct decode *dec, int nframes, struct rot rotation, Rect cropRect);
void android_display_close(struct vpu_display *disp);
int android_get_buf(struct decode *dec);
int android_put_data(struct vpu_display *disp, int index, int field, int fps);
#endif

int v4l_start_capturing(void);
void v4l_stop_capturing(void);
int v4l_capture_setup(struct encode *enc, int width, int height, int fps);
int v4l_get_capture_data(struct v4l2_buffer *buf);
void v4l_put_capture_data(struct v4l2_buffer *buf);


int encoder_open(struct encode *enc);
void encoder_close(struct encode *enc);
int encoder_configure(struct encode *enc);
int encoder_allocate_framebuffer(struct encode *enc);
void encoder_free_framebuffer(struct encode *enc);

int decoder_open(struct decode *dec);
void decoder_close(struct decode *dec);
int decoder_parse(struct decode *dec);
int decoder_allocate_framebuffer(struct decode *dec);
void decoder_free_framebuffer(struct decode *dec);

void SaveQpReport(Uint32 *qpReportAddr, int picWidth, int picHeight,
		  int frameIdx, char *fileName);

static inline int is_mx6x_mjpg(int fmt)
{
        if (cpu_is_mx6x() && (fmt == STD_MJPG))
                return true;
        else
                return false;
}

static __inline int queue_size(struct buf_queue * q)
{
        if (q->tail >= q->head)
                return (q->tail - q->head);
        else
                return ((q->tail + QUEUE_SIZE) - q->head);
}

static __inline int queue_buf(struct buf_queue * q, int idx)
{
        if (((q->tail + 1) % QUEUE_SIZE) == q->head)
                return -1;      /* queue full */
        q->list[q->tail] = idx;
        q->tail = (q->tail + 1) % QUEUE_SIZE;
        return 0;
}

static __inline int dequeue_buf(struct buf_queue * q)
{
        int ret;
        if (q->tail == q->head)
                return -1;      /* queue empty */
        ret = q->list[q->head];
        q->head = (q->head + 1) % QUEUE_SIZE;
        return ret;
}

static __inline int peek_next_buf(struct buf_queue * q)
{
        if (q->tail == q->head)
                return -1;      /* queue empty */
        return q->list[q->head];
}

#endif
