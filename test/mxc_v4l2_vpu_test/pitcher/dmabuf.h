/*
 * Copyright(c) 2021 NXP. All rights reserved.
 *
 */
/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef _PITCHER_DMABUF_H
#define _PITCHER_DMABUF_H
#ifdef __cplusplus
extern "C"
{
#endif

int pitcher_construct_dma_buf_from_fd(struct pitcher_buf_ref *buf);
int pitcher_alloc_dma_buf(struct pitcher_buf_ref *buf);
void pitcher_free_dma_buf(struct pitcher_buf_ref *buf);
int pitcher_start_cpu_access_dma_buf(struct pitcher_buf_ref *buf, int read, int write);
int pitcher_end_cpu_access_dma_buf(struct pitcher_buf_ref *buf, int read, int write);
int pitcher_start_cpu_access(struct pitcher_buffer *buffer, int read, int write);
int pitcher_end_cpu_access(struct pitcher_buffer *buffer, int read, int write);

struct pitcher_buffer *pitcher_new_dma_buffer(struct pix_fmt_info *format,
		int (*recycle)(struct pitcher_buffer *buffer, void *arg, int *del),
		void *arg);
int pitcher_buffer_is_dma_buf(struct pitcher_buffer *buffer);

int ion_alloc_dma_buf(size_t size);
int cma_heap_alloc_dma_buf(size_t size);
int cma_heap_uncached_alloc_dma_buf(size_t size);

#ifdef __cplusplus
}
#endif
#endif
