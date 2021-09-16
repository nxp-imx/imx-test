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
/*
 * pitcher/dmabuf.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include "pitcher.h"
#include "pitcher_def.h"
#include "dmabuf.h"

#define PAGE_ALIGN(x) ALIGN(x, 4096)

struct allocator_t {
	int (*alloc_dma_buf) (size_t size);
};

struct allocator_t allocators[] = {
	{
		cma_heap_uncached_alloc_dma_buf,
	},
	{
		ion_alloc_dma_buf,
	},
	{
		cma_heap_alloc_dma_buf,
	},
};

static size_t get_dma_buf_size(int fd)
{

	return lseek(fd, 0l, SEEK_END);
}

static int get_dma_buf_phys(int dmafd, unsigned long *phys)
{
	int ret;
	struct dma_buf_phys query;

	if (!phys)
		return -RET_E_NULL_POINTER;

	*phys = 0;
	ret = ioctl(dmafd, DMA_BUF_IOCTL_PHYS, &query);
	if (!ret)
		*phys = query.phys;

	return ret;
}

static int get_dma_buf_virt(int dmafd, size_t size, void **virt)
{
	void *addr;

	if (!virt)
		return -RET_E_NULL_POINTER;

	*virt = NULL;
	addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmafd, 0);
	if (addr == MAP_FAILED) {
		printf("mmap failed\n");
		return -1;
	}
	*virt = addr;
	return 0;
}

int pitcher_construct_dma_buf_from_fd(struct pitcher_buf_ref *buf)
{
	unsigned long phys;
	void *virt;
	size_t size;
	int ret;

	if (!buf || buf->dmafd < 0)
		return -RET_E_INVAL;
	size = get_dma_buf_size(buf->dmafd);
	if (!size)
		return -RET_E_INVAL;
	if (buf->size && size > buf->size)
		size = buf->size;

	ret = get_dma_buf_phys(buf->dmafd, &phys);
	if (ret)
		return ret;

	ret = get_dma_buf_virt(buf->dmafd, size, &virt);
	if (ret)
		return ret;

	buf->size = size;
	buf->phys = phys;
	buf->virt = virt;

	return RET_OK;
}

int pitcher_alloc_dma_buf(struct pitcher_buf_ref *buf)
{
	int dmafd = -1;
	size_t size;
	int i;
	int ret;

	if (!buf || !buf->size)
		return -RET_E_INVAL;

	size = PAGE_ALIGN(buf->size);
	for (i = 0; i < ARRAY_SIZE(allocators); i++) {
		if (!allocators[i].alloc_dma_buf)
			continue;
		dmafd = allocators[i].alloc_dma_buf(size);
		if (dmafd >= 0)
			break;
	}
	if (dmafd < 0) {
		printf("alloc dma_buf failed\n");
		return -RET_E_NO_MEMORY;
	}

	buf->dmafd = dmafd;
	ret = pitcher_construct_dma_buf_from_fd(buf);
	if (ret) {
		SAFE_CLOSE(dmafd, close);
		buf->dmafd = -1;
		return ret;
	}
	return RET_OK;
}

void pitcher_free_dma_buf(struct pitcher_buf_ref *buf)
{
	if (!buf || buf->dmafd < 0)
		return;
	if (buf->virt && buf->size)
		munmap(buf->virt, buf->size);
	SAFE_CLOSE(buf->dmafd, close);
	buf->phys = 0;
}

int pitcher_start_cpu_access_dma_buf(struct pitcher_buf_ref *buf, int read,
				     int write)
{
	struct dma_buf_sync sync;

	if (!buf || buf->dmafd < 0)
		return -1;

	sync.flags = DMA_BUF_SYNC_START;
	if (read)
		sync.flags |= DMA_BUF_SYNC_READ;
	if (write)
		sync.flags |= DMA_BUF_SYNC_WRITE;

	return ioctl(buf->dmafd, DMA_BUF_IOCTL_SYNC, &sync);
}

int pitcher_end_cpu_access_dma_buf(struct pitcher_buf_ref *buf, int read,
				   int write)
{
	struct dma_buf_sync sync;

	if (!buf || buf->dmafd < 0)
		return -1;

	sync.flags = DMA_BUF_SYNC_END;
	if (read)
		sync.flags |= DMA_BUF_SYNC_READ;
	if (write)
		sync.flags |= DMA_BUF_SYNC_WRITE;

	return ioctl(buf->dmafd, DMA_BUF_IOCTL_SYNC, &sync);
}

int pitcher_start_cpu_access(struct pitcher_buffer *buffer, int read, int write)
{
	int ret;
	uint32_t i;

	for (i = 0; i < buffer->count; i++) {
		if (buffer->planes[i].dmafd < 0)
			continue;
		ret =
		    pitcher_start_cpu_access_dma_buf(&buffer->planes[i], read,
						     write);
		if (ret)
			return ret;
	}

	return RET_OK;
}

int pitcher_end_cpu_access(struct pitcher_buffer *buffer, int read, int write)
{
	int ret;
	uint32_t i;

	for (i = 0; i < buffer->count; i++) {
		if (buffer->planes[i].dmafd < 0)
			continue;
		ret =
		    pitcher_end_cpu_access_dma_buf(&buffer->planes[i], read,
						   write);
		if (ret)
			return ret;
	}

	return RET_OK;
}

static int init_dma_buf_plane(struct pitcher_buf_ref *plane, unsigned int index,
			      void *arg)
{
	assert(plane);

	if (!plane->size)
		return -RET_E_INVAL;

	return pitcher_alloc_dma_buf(plane);
}

static int free_dma_buf_plane(struct pitcher_buf_ref *plane, unsigned int index,
			      void *arg)
{
	if (!plane)
		return RET_OK;

	pitcher_free_dma_buf(plane);
	return RET_OK;
}

struct pitcher_buffer *pitcher_new_dma_buffer(struct pix_fmt_info *format,
					      int (*recycle) (struct
							      pitcher_buffer *
							      buffer, void *arg,
							      int *del),
					      void *arg)
{
	struct pitcher_buffer_desc desc;
	uint32_t i;

	if (!format || !format->format || !format->num_planes || !format->size)
		return NULL;
	if (!recycle)
		return NULL;

	memset(&desc, 0, sizeof(desc));
	desc.init_plane = init_dma_buf_plane;
	desc.uninit_plane = free_dma_buf_plane;
	desc.plane_count = format->num_planes;
	desc.recycle = recycle;
	desc.arg = arg;
	for (i = 0; i < format->num_planes; i++)
		desc.plane_size[i] = format->planes[i].size;

	return pitcher_new_buffer(&desc);
}

int pitcher_buffer_is_dma_buf(struct pitcher_buffer *buffer)
{
	uint32_t i;

	if (!buffer)
		return -RET_E_NULL_POINTER;

	for (i = 0; i < buffer->count; i++) {
		if (buffer->planes[i].dmafd < 0)
			return -RET_E_INVAL;
	}

	return RET_OK;
}

#ifdef ENABLE_ION
#include <linux/ion.h>
static int ion_query_heap_cnt(int fd, int *cnt)
{
	int ret;
	struct ion_heap_query query;

	memset(&query, 0, sizeof(query));

	ret = ioctl(fd, ION_IOC_HEAP_QUERY, &query);
	if (ret < 0)
		return ret;

	*cnt = query.cnt;
	return ret;
}

static int ion_query_get_heaps(int fd, int cnt, void *buffers)
{
	int ret;
	struct ion_heap_query query = {
		.cnt = cnt,.heaps = (uintptr_t) buffers,
	};

	ret = ioctl(fd, ION_IOC_HEAP_QUERY, &query);
	return ret;
}

static int ion_alloc_fd(int fd, size_t len, size_t align,
			unsigned int heap_mask, unsigned int flags,
			int *handle_fd)
{
	int ret;
	struct ion_allocation_data data = {
		.len = len,
		.heap_id_mask = heap_mask,
		.flags = flags,
	};

	ret = ioctl(fd, ION_IOC_ALLOC, &data);
	if (ret < 0)
		return ret;
	*handle_fd = data.fd;

	return ret;
}

static int ion_allocator_alloc(int fd, int size)
{
	int dmafd = -1;
	int heapCnt = 0;
	int heap_mask = 0;
	size_t ionSize = size;
	struct ion_heap_data ihd[ION_HEAP_TYPE_CUSTOM + 1];

	int ret = ion_query_heap_cnt(fd, &heapCnt);
	if (ret != 0 || heapCnt == 0) {
		printf("can't query heap count");
		return -1;
	}

	memset(&ihd, 0, sizeof(ihd));
	ret = ion_query_get_heaps(fd, heapCnt, &ihd);
	if (ret != 0) {
		printf("can't get ion heaps");
		return -1;
	}
	// add heap ids from heap type.
	for (int i = 0; i < heapCnt; i++) {
		if (ihd[i].type == ION_HEAP_TYPE_DMA) {
			heap_mask |= 1 << ihd[i].heap_id;
			continue;
		}
	}

	ret = ion_alloc_fd(fd, ionSize, 8, heap_mask, 0, &dmafd);
	if (ret) {
		printf("ion_alloc_fd failed.\n");
		return -1;
	}

	return dmafd;
}

int ion_alloc_dma_buf(size_t size)
{
	const char *devnode = "/dev/ion";
	int devfd;
	int dmafd = -1;

	devfd = open(devnode, O_RDWR);
	if (devfd < 0)
		return -1;

	dmafd = ion_allocator_alloc(devfd, size);
	close(devfd);

	return dmafd;
}
#else
int ion_alloc_dma_buf(size_t size)
{
	PITCHER_ERR("ion is not supported\n");
	return -RET_E_INVAL;
}
#endif

#ifdef ENABLE_DMA_HEAP
#include <linux/dma-heap.h>
static int cma_heap_allocator_alloc(int fd, int size)
{
	struct dma_heap_allocation_data data;
	int ret;

	data.fd = 0;
	data.len = size;
	data.fd_flags = O_CLOEXEC | O_RDWR;
	data.heap_flags = 0;

	ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &data);
	if (ret)
		return -RET_E_INVAL;

	return data.fd;
}

static int dma_heap_alloc_dma_buf(const char *heap, size_t size)
{
	int devfd;
	int dmafd = -1;

	devfd = open(heap, O_RDWR);
	if (devfd < 0)
		return -1;

	dmafd = cma_heap_allocator_alloc(devfd, size);
	close(devfd);

	return dmafd;
}

int cma_heap_alloc_dma_buf(size_t size)
{
	return dma_heap_alloc_dma_buf("/dev/dma_heap/linux,cma", size);
}

int cma_heap_uncached_alloc_dma_buf(size_t size)
{
	return dma_heap_alloc_dma_buf("/dev/dma_heap/linux,cma-uncached", size);
}
#else
int cma_heap_alloc_dma_buf(size_t size)
{
	PITCHER_ERR("dma heap is not supported\n");
	return -RET_E_INVAL;
}
int cma_heap_uncached_alloc_dma_buf(size_t size)
{
	PITCHER_ERR("dma heap uncached is not supported\n");
	return -RET_E_INVAL;
}
#endif
