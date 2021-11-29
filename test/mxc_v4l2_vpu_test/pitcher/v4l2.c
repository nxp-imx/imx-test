/*
 * Copyright 2018-2021 NXP
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
 * v4l2.c
 *
 * Author Ming Qian<ming.qian@nxp.com>
 */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <time.h>
#include "pitcher_def.h"
#include "pitcher.h"
#include "pitcher_v4l2.h"
#include "platform_8x.h"
#include "dmabuf.h"

static int __is_v4l2_end(struct v4l2_component_t *component)
{
	assert(component);

	if (component->end) {
		return true;
	}

	if (component->is_end && component->is_end(component)) {
		component->end = true;
		return true;
	}
	if (pitcher_is_error(component->chnno)) {
		component->end = true;
		return true;
	}

	return false;
}

static int __set_v4l2_fmt(struct v4l2_component_t *component)
{
	struct v4l2_format format;
	int i;
	int fd;
	int ret;

	assert(component && component->fd >= 0);
	fd = component->fd;

	if (component->pixelformat != PIX_FMT_NONE) {
		ret = check_v4l2_support_fmt(component->fd,
				component->type, component->pixelformat);
		if (ret == FALSE) {
			PITCHER_ERR("set fmt %s fail, not supported \n",
					pitcher_get_format_name(component->pixelformat));
			return -RET_E_INVAL;
		}
	}

	memset(&format, 0, sizeof(format));
	format.type = component->type;
	ioctl(fd, VIDIOC_G_FMT, &format);
	if (!component->fourcc) {
		if (!V4L2_TYPE_IS_MULTIPLANAR(component->type))
			component->fourcc = format.fmt.pix.pixelformat;
		else
			component->fourcc = format.fmt.pix_mp.pixelformat;
		component->pixelformat = pitcher_get_format_by_fourcc(component->fourcc);
	}
	if (!component->width || !component->height) {
		if (!V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
			component->width = format.fmt.pix.width;
			component->height = format.fmt.pix.height;
		} else {
			component->width = format.fmt.pix_mp.width;
			component->height = format.fmt.pix_mp.height;
		}
	}

	if (!V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		format.fmt.pix.pixelformat = component->fourcc;
		format.fmt.pix.width = component->width;
		format.fmt.pix.height = component->height;
		format.fmt.pix.bytesperline = component->bytesperline;
		format.fmt.pix.sizeimage = component->sizeimage;
	} else {
		format.fmt.pix_mp.pixelformat = component->fourcc;
		format.fmt.pix_mp.width = component->width;
		format.fmt.pix_mp.height = component->height;
		format.fmt.pix_mp.num_planes = pitcher_get_format_num_planes(component->pixelformat);
		for (i = 0; i < format.fmt.pix_mp.num_planes; i++) {
			format.fmt.pix_mp.plane_fmt[i].bytesperline =
							component->bytesperline;
			format.fmt.pix_mp.plane_fmt[i].sizeimage =
							component->sizeimage;
		}
	}
	ret = ioctl(fd, VIDIOC_S_FMT, &format);
	if (ret) {
		PITCHER_ERR("VIDIOC_S_FMT fail, error : %s\n", strerror(errno));
		return -RET_E_INVAL;
	}
	ioctl(fd, VIDIOC_G_FMT, &format);
	if (!V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		component->num_planes = 1;
		component->width = format.fmt.pix.width;
		component->height = format.fmt.pix.height;
		component->fourcc = format.fmt.pix.pixelformat;
		component->sizeimage = format.fmt.pix.sizeimage;
		component->bytesperline = format.fmt.pix.bytesperline;
		component->field = format.fmt.pix.field;
	} else {
		component->num_planes = format.fmt.pix_mp.num_planes;
		component->width = format.fmt.pix_mp.width;
		component->height = format.fmt.pix_mp.height;
		component->fourcc = format.fmt.pix_mp.pixelformat;
		component->sizeimage = format.fmt.pix_mp.plane_fmt[0].sizeimage;
		component->bytesperline = format.fmt.pix_mp.plane_fmt[0].bytesperline;
		component->field = format.fmt.pix_mp.field;
	}
	component->pixelformat = pitcher_get_format_by_fourcc(component->fourcc);

	return RET_OK;
}

static int __set_v4l2_fps(struct v4l2_component_t *component)
{
	int fd;
	int ret;
	struct v4l2_streamparm parm;

	assert(component && component->fd >= 0);
	fd = component->fd;

	if (!component->framerate)
		return RET_OK;

	memset(&parm, 0, sizeof(parm));
	parm.type = component->type;
	parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = component->framerate;
	ret = ioctl(fd, VIDIOC_S_PARM, &parm);
	if (ret) {
		PITCHER_ERR("VIDIOC_S_PARM fail, error : %s\n",
				strerror(errno));
		return -RET_E_INVAL;
	}

	return RET_OK;
}

static int __get_v4l2_min_buffers(int fd, uint32_t type)
{
	uint32_t id;
	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(type))
		id = V4L2_CID_MIN_BUFFERS_FOR_OUTPUT;
	else
		id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;

	memset(&qctrl, 0, sizeof(qctrl));
	qctrl.id = id;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, &qctrl);
	if (ret < 0)
		return 0;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
	if (ret < 0)
		return 0;

	return ctrl.value;
}

static int __set_crop(struct v4l2_component_t *component)
{
	struct v4l2_selection s;
	int ret;

	assert(component && component->fd >= 0);

	if (!component->crop.width || !component->crop.height)
		return 0;

	if (!V4L2_TYPE_IS_OUTPUT(component->type))
		return -RET_E_INVAL;

	s.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	s.target = V4L2_SEL_TGT_CROP;
	s.r.left = component->crop.left;
	s.r.top = component->crop.top;
	s.r.width = component->crop.width;
	s.r.height = component->crop.height;

	ret = ioctl(component->fd, VIDIOC_S_SELECTION, &s);
	if (ret < 0) {
		PITCHER_ERR("fail to set crop (%d, %d) %d x %d\n",
					component->crop.left,
					component->crop.top,
					component->crop.width,
					component->crop.height);
		return -RET_E_INVAL;
	}

	return 0;
}

static int __req_v4l2_buffer(struct v4l2_component_t *component)
{
	int fd;
	int ret;
	struct v4l2_requestbuffers req_bufs;
	unsigned int min_buffer_count;

	assert(component && component->fd >= 0);
	fd = component->fd;

	min_buffer_count = __get_v4l2_min_buffers(component->fd,
							component->type);
	PITCHER_LOG("%s min buffers : %d\n",
		V4L2_TYPE_IS_OUTPUT(component->type) ? "output" : "capture",
		min_buffer_count);
	min_buffer_count += 3;
	if (component->buffer_count < min_buffer_count)
		component->buffer_count = min_buffer_count;
	if (component->buffer_count > MAX_BUFFER_COUNT)
		component->buffer_count = MAX_BUFFER_COUNT;

	memset(&req_bufs, 0, sizeof(req_bufs));
	req_bufs.count = component->buffer_count;
	req_bufs.type = component->type;
	req_bufs.memory = component->memory;
	ret = ioctl(fd, VIDIOC_REQBUFS, &req_bufs);
	if (ret) {
		PITCHER_ERR("VIDIOC_REQBUFS fail, error : %s\n",
				strerror(errno));
		return -RET_E_INVAL;
	}
	if (req_bufs.count > MAX_BUFFER_COUNT) {
		PITCHER_ERR("v4l2 buffer count(%d) is out of range(%d)\n",
				req_bufs.count, MAX_BUFFER_COUNT);
		req_bufs.count = 0;
		ioctl(fd, VIDIOC_REQBUFS, &req_bufs);
	}
	component->buffer_count = req_bufs.count;

	PITCHER_LOG("%s request buffers : %d, memory : %d\n",
		V4L2_TYPE_IS_OUTPUT(component->type) ? "output" : "capture",
		req_bufs.count, req_bufs.memory);

	return RET_OK;
}

static struct pitcher_buffer *__dqbuf(struct v4l2_component_t *component)
{
	struct v4l2_buffer v4lbuf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct pitcher_buffer *buffer = NULL;
	int is_ready;
	int fd;
	int i;
	int ret;

	if (!component || component->fd < 0)
		return NULL;

	if (V4L2_TYPE_IS_OUTPUT(component->type))
		is_ready = pitcher_poll(component->fd, POLLOUT, 0);
	else
		is_ready = pitcher_poll(component->fd, POLLIN, 0);
	if (!is_ready)
		return NULL;

	fd = component->fd;
	memset(&v4lbuf, 0, sizeof(v4lbuf));
	v4lbuf.type = component->type;
	v4lbuf.memory = component->memory;
	v4lbuf.index = -1;
	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		v4lbuf.m.planes = planes;
		v4lbuf.length = ARRAY_SIZE(planes);
	}
	ret = ioctl(fd, VIDIOC_DQBUF, &v4lbuf);
	if (ret) {
		if (errno == EPIPE) {
			PITCHER_LOG("dqbuf : EPIPE\n");
			component->end = true;
			return NULL;
		}
		PITCHER_ERR("dqbuf fail, error: %s, %d\n", strerror(errno), errno);
		return NULL;
	}

	buffer = component->slots[v4lbuf.index];

	if (!buffer)
		return NULL;
	component->slots[v4lbuf.index] = NULL;
	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		for (i = 0; i < v4lbuf.length; i++)
			buffer->planes[i].bytesused = v4lbuf.m.planes[i].bytesused;
	} else {
		buffer->planes[0].bytesused = v4lbuf.bytesused;
	}

	if (!V4L2_TYPE_IS_OUTPUT(component->type)) {
		if (v4lbuf.flags & V4L2_BUF_FLAG_KEYFRAME)
			SET_BUFFER_TYPE(buffer->flags, BUFFER_TYPE_KEYFRAME);
		else if (v4lbuf.flags & V4L2_BUF_FLAG_PFRAME)
			SET_BUFFER_TYPE(buffer->flags, BUFFER_TYPE_PFRAME);
		else if (v4lbuf.flags & V4L2_BUF_FLAG_BFRAME)
			SET_BUFFER_TYPE(buffer->flags, BUFFER_TYPE_BFRAME);

		if (v4lbuf.flags & V4L2_BUF_FLAG_LAST
			|| buffer->planes[0].bytesused == 0) {
			PITCHER_LOG("LAST BUFFER, flags = 0x%x, bytesused = %ld\n", v4lbuf.flags, buffer->planes[0].bytesused);
			buffer->flags |= PITCHER_BUFFER_FLAG_LAST;
		}
		if (buffer->planes[0].bytesused)
			component->frame_count++;
	}

	return pitcher_get_buffer(buffer);
}

static void __qbuf(struct v4l2_component_t *component,
			struct pitcher_buffer *buffer)
{
	struct v4l2_buffer v4lbuf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	int fd;
	int i;
	int ret;

	if (!component || component->fd < 0)
		return;
	if (!buffer)
		return;

	if (V4L2_TYPE_IS_OUTPUT(component->type) && !component->enable)
		return;

	fd = component->fd;
	memset(&v4lbuf, 0, sizeof(v4lbuf));
	v4lbuf.type = component->type;
	v4lbuf.memory = component->memory;
	v4lbuf.index = buffer->index;
	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		v4lbuf.m.planes = planes;
		v4lbuf.length = ARRAY_SIZE(planes);
	}
	ret = ioctl(fd, VIDIOC_QUERYBUF, &v4lbuf);
	if (ret) {
		PITCHER_ERR("query buf fail, error: %s\n", strerror(errno));
		return;
	}

	if (V4L2_TYPE_IS_OUTPUT(component->type)) {
		if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
			for (i = 0; i < v4lbuf.length; i++)
				v4lbuf.m.planes[i].bytesused =
					buffer->planes[i].bytesused;
		} else {
			v4lbuf.bytesused = buffer->planes[0].bytesused;
		}
	}
	if (component->memory == V4L2_MEMORY_USERPTR) {
		if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
			for (i = 0; i < v4lbuf.length; i++) {
				v4lbuf.m.planes[i].length =
					buffer->planes[i].size;
				v4lbuf.m.planes[i].m.userptr =
					(unsigned long)buffer->planes[i].virt;
			}
		} else {
			v4lbuf.length = buffer->planes[0].size;
			v4lbuf.m.userptr =
				(unsigned long)buffer->planes[0].virt;
		}
	} else if (component->memory == V4L2_MEMORY_DMABUF) {
		if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
			for (i = 0; i < v4lbuf.length; i++) {
				v4lbuf.m.planes[i].m.fd = buffer->planes[i].dmafd;
				v4lbuf.m.planes[i].length = buffer->planes[i].size;
				v4lbuf.m.planes[i].data_offset = buffer->planes[i].offset;
			}
		} else {
			v4lbuf.m.fd = buffer->planes[0].dmafd;
			v4lbuf.length = buffer->planes[0].size;
		}
	}
	v4lbuf.timestamp.tv_sec = -1;
	ret = ioctl(fd, VIDIOC_QBUF, &v4lbuf);
	if (ret) {
		PITCHER_ERR("(%s)qbuf fail, error: %s\n",
				component->desc.name, strerror(errno));
		SAFE_RELEASE(buffer->priv, pitcher_put_buffer);
		component->errors[buffer->index] = pitcher_get_buffer(buffer);
		return;
	}

	component->slots[buffer->index] = buffer;
	if (V4L2_TYPE_IS_OUTPUT(component->type))
		component->frame_count++;
}

static int __streamon_v4l2(struct v4l2_component_t *component)
{
	enum v4l2_buf_type type;
	int fd;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	fd = component->fd;
	type = component->type;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret) {
		PITCHER_LOG("streamon fail, error : %s\n", strerror(errno));
		return -RET_E_INVAL;
	}

	return RET_OK;
}

static int __streamoff_v4l2(struct v4l2_component_t *component)
{
	enum v4l2_buf_type type;
	int fd;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	fd = component->fd;
	type = component->type;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret) {
		PITCHER_LOG("streamoff fail, error : %s\n", strerror(errno));
		return -RET_E_INVAL;
	}

	return RET_OK;
}

static struct pitcher_buffer *__get_buf(struct v4l2_component_t *component)
{
	int i;
	struct pitcher_buffer *buffer;

	if (!component || component->fd < 0)
		return NULL;

	for (i = 0; i < component->buffer_count; i++) {
		buffer = component->buffers[i];
		if (buffer)
			return buffer;
	}

	return NULL;
}

static int v4l2_enum_fmt(int fd, struct v4l2_fmtdesc *fmt)
{
	return ioctl(fd, VIDIOC_ENUM_FMT, fmt);
}

static int init_v4l2_mmap_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	struct v4l2_buffer v4lbuf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct v4l2_component_t *component = arg;
	struct v4l2_exportbuffer exp;
	int fd;
	int ret;

	if (!component || component->fd < 0 || !plane)
		return -RET_E_NULL_POINTER;

	fd = component->fd;
	memset(&v4lbuf, 0, sizeof(v4lbuf));
	v4lbuf.type = component->type;
	v4lbuf.memory = component->memory;
	v4lbuf.index = component->buffer_index;
	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		v4lbuf.m.planes = planes;
		v4lbuf.length = ARRAY_SIZE(planes);
	}
	ret = ioctl(fd, VIDIOC_QUERYBUF, &v4lbuf);
	if (ret) {
		PITCHER_ERR("query buf fail, error: %s\n", strerror(errno));
		return -RET_E_INVAL;
	}

	exp.type = component->type;
	exp.index = component->buffer_index;
	exp.plane = index;
	exp.fd = -1;
	exp.flags = O_CLOEXEC | O_RDWR;
	ioctl(fd, VIDIOC_EXPBUF, &exp);
	/*printf("[%d:%d] fd = %d\n", component->buffer_index, index, exp.fd);*/
	if (exp.fd >= 0) {
		plane->dmafd = exp.fd;
		plane->size = V4L2_TYPE_IS_MULTIPLANAR(component->type) ? v4lbuf.m.planes[index].length : v4lbuf.length;
		if (!pitcher_construct_dma_buf_from_fd(plane))
			return RET_OK;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		if (index >= v4lbuf.length)
			return -RET_E_INVAL;
		plane->size = v4lbuf.m.planes[index].length;
		plane->virt = mmap(NULL,
					plane->size,
					PROT_READ | PROT_WRITE,
					MAP_SHARED,
					fd,
					v4lbuf.m.planes[index].m.mem_offset);
	} else {
		if (index >= 1)
			return -RET_E_INVAL;
		plane->size = v4lbuf.length;
		plane->virt = mmap(NULL,
					plane->size,
					PROT_READ | PROT_WRITE,
					MAP_SHARED,
					fd,
					v4lbuf.m.offset);
	}
	if (plane->virt == (void *)MAP_FAILED) {
		PITCHER_ERR("mmap v4l2 fail, error : %s\n", strerror(errno));
		return -RET_E_MMAP;
	}

	return RET_OK;
}

static int uninit_v4l2_mmap_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	if (!plane)
		return RET_OK;

	if (plane->virt && plane->size) {
		munmap(plane->virt, plane->size);
		plane->virt = NULL;
	}
	if (plane->dmafd != -1) {
		SAFE_CLOSE(plane->dmafd, close);
		plane->dmafd = -1;
	}

	return RET_OK;
}

static int init_v4l2_userptr_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	struct v4l2_buffer v4lbuf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	struct v4l2_component_t *component = arg;
	int fd;
	int ret;

	if (!component || component->fd < 0 || !plane)
		return -RET_E_NULL_POINTER;

	fd = component->fd;
	memset(&v4lbuf, 0, sizeof(v4lbuf));
	v4lbuf.type = component->type;
	v4lbuf.memory = component->memory;
	v4lbuf.index = component->buffer_index;
	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		v4lbuf.m.planes = planes;
		v4lbuf.length = ARRAY_SIZE(planes);
	}
	ret = ioctl(fd, VIDIOC_QUERYBUF, &v4lbuf);
	if (ret) {
		PITCHER_ERR("query buf fail, error: %s\n", strerror(errno));
		return -RET_E_INVAL;
	}

	if (V4L2_TYPE_IS_MULTIPLANAR(component->type)) {
		if (index >= v4lbuf.length)
			return -RET_E_INVAL;
		plane->size = v4lbuf.m.planes[index].length;
	} else {
		if (index >= 1)
			return -RET_E_INVAL;
		plane->size = v4lbuf.length;
	}

	return RET_OK;
}

static int uninit_v4l2_userptr_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int init_v4l2_dmabuf_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	/* same operation as userptr */
	return init_v4l2_userptr_plane(plane, index, arg);
}

static int uninit_v4l2_dmabuf_plane(struct pitcher_buf_ref *plane,
				unsigned int index, void *arg)
{
	return RET_OK;
}

static int __recycle_v4l2_buffer(struct pitcher_buffer *buffer,
				void *arg, int *del)
{
	struct v4l2_component_t *component = arg;
	int is_del = false;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	__qbuf(component, buffer);

	if (!component->enable)
		is_del = true;
	if (component->end)
		is_del = true;
	if (buffer->flags & PITCHER_BUFFER_FLAG_LAST)
		is_del = true;
	if (is_del)
		component->slots[buffer->index] = NULL;

	if (del)
		*del = is_del;

	return RET_OK;
}

static int __alloc_v4l2_buffer(struct v4l2_component_t *component)
{
	int i;
	struct pitcher_buffer_desc desc;
	struct pitcher_buffer *buffer;
	int supported_fmt = 0;

	assert(component && component->fd >= 0);

	switch (component->memory) {
	case V4L2_MEMORY_MMAP:
		desc.init_plane = init_v4l2_mmap_plane;
		desc.uninit_plane = uninit_v4l2_mmap_plane;
		break;
	case V4L2_MEMORY_USERPTR:
		desc.init_plane = init_v4l2_userptr_plane;
		desc.uninit_plane = uninit_v4l2_userptr_plane;
		break;
	case V4L2_MEMORY_DMABUF:
		desc.init_plane = init_v4l2_dmabuf_plane;
		desc.uninit_plane = uninit_v4l2_dmabuf_plane;
		break;
	default:
		return -RET_E_INVAL;
	}

	component->format.format = component->pixelformat;
	component->format.width = component->width;
	component->format.height = component->height;
	if (component->num_planes == 1)
		component->format.size = component->sizeimage;
	component->format.interlaced = component->field == V4L2_FIELD_INTERLACED ? 1 : 0;
	if (!pitcher_get_pix_fmt_info(&component->format, component->bytesperline))
		supported_fmt = 1;

	desc.plane_count = component->num_planes;
	desc.recycle = __recycle_v4l2_buffer;
	desc.arg = component;

	for (i = 0; i < component->buffer_count; i++) {
		component->buffer_index = i;
		buffer = pitcher_new_buffer(&desc);
		if (!buffer)
			break;
		buffer->index = i;
		component->buffers[i] = buffer;
		if (supported_fmt)
			buffer->format = &component->format;
	}
	component->buffer_count = i;
	if (!component->buffer_count)
		return -RET_E_NO_MEMORY;

	return RET_OK;
}

static void __clear_error_buffers(struct v4l2_component_t *component)
{
	int i;

	if (!component)
		return;

	for (i = 0; i < component->buffer_count; i++) {
		if (!component->errors[i])
			continue;
		component->buffers[i] =
			pitcher_get_buffer(component->errors[i]);
		SAFE_RELEASE(component->errors[i], pitcher_put_buffer);
	}
}

static int init_v4l2(void *arg)
{
	struct v4l2_component_t *component = arg;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	PITCHER_LOG("init : %s\n", component->desc.name);

	return RET_OK;
}

static int cleanup_v4l2(void *arg)
{
	struct v4l2_component_t *component = arg;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	PITCHER_LOG("cleanup : %s\n", component->desc.name);

	return RET_OK;
}

static int __init_v4l2(struct v4l2_component_t *component)
{
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	ret = __set_v4l2_fmt(component);
	if (ret < 0) {
		PITCHER_ERR("set fmt fail\n");
		return ret;
	}

	__set_crop(component);

	ret = __set_v4l2_fps(component);
	if (ret < 0) {
		PITCHER_ERR("set fps fail\n");
		return ret;
	}

	ret = __req_v4l2_buffer(component);
	if (ret < 0) {
		PITCHER_ERR("req buffer fail\n");
		return ret;
	}

	ret = __alloc_v4l2_buffer(component);
	if (ret < 0) {
		PITCHER_ERR("alloc buffer fail\n");
		return ret;
	}

	return RET_OK;
}

static int __cleanup_v4l2(struct v4l2_component_t *component)
{
	int i;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	__clear_error_buffers(component);
	for (i = 0; i < component->buffer_count; i++) {
		if (!component->buffers[i])
			continue;
		SAFE_RELEASE(component->buffers[i]->priv, pitcher_put_buffer);
		SAFE_RELEASE(component->buffers[i], pitcher_put_buffer);
	}

	return RET_OK;
}

static int get_v4l2_fourcc(struct v4l2_component_t *component)
{
	struct v4l2_fmtdesc fmt_desc;

	fmt_desc.type = component->type;
	fmt_desc.index = 0;

	while (!v4l2_enum_fmt(component->fd, &fmt_desc)) {
		if (pitcher_get_format_by_fourcc(fmt_desc.pixelformat) == component->pixelformat) {
			component->fourcc = fmt_desc.pixelformat;
			return RET_OK;
		}
		fmt_desc.index++;
	}

	return -RET_E_NOT_SUPPORT;
}

static int start_v4l2(void *arg)
{
	struct v4l2_component_t *component = arg;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	ret = get_v4l2_fourcc(component);
	if (ret) {
		PITCHER_ERR("can't find fourcc for foramt %s\n",
				pitcher_get_format_name(component->pixelformat));
		return ret;
	}
	ret = __init_v4l2(component);
	if (ret < 0)
		return ret;

	component->enable = true;
	if (!V4L2_TYPE_IS_OUTPUT(component->type)) {
		int i;

		for (i = 0; i < component->buffer_count; i++)
			SAFE_RELEASE(component->buffers[i], pitcher_put_buffer);
	}

	ret = __streamon_v4l2(component);
	if (ret < 0) {
		PITCHER_ERR("streamon fail\n");
		component->enable = false;
		return ret;
	}

	if (component->start)
		component->start(component);

	component->ts_b = pitcher_get_monotonic_raw_time();

	return RET_OK;
}

static int stop_v4l2(void *arg)
{
	struct v4l2_component_t *component = arg;
	int i;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	component->ts_e = pitcher_get_monotonic_raw_time();
	if (component->stop)
		component->stop(component);

	ret = __streamoff_v4l2(component);
	if (ret < 0) {
		PITCHER_ERR("stream on fail\n");
		return ret;
	}
	component->enable = false;
	component->end = false;

	for (i = 0; i < component->buffer_count; i++) {
		struct pitcher_buffer *buffer = component->slots[i];

		if (!buffer)
			continue;
		pitcher_get_buffer(buffer);
		SAFE_RELEASE(buffer->priv, pitcher_put_buffer);
		SAFE_RELEASE(buffer, pitcher_put_buffer);
		component->slots[i] = NULL;
	}

	__clear_error_buffers(component);

	for (i = 0; i < component->buffer_count; i++) {
		if (!component->buffers[i])
			continue;
		SAFE_RELEASE(component->buffers[i]->priv, pitcher_put_buffer);
		SAFE_RELEASE(component->buffers[i], pitcher_put_buffer);
	}

	return __cleanup_v4l2(component);
}

static void __check_v4l2_events(struct v4l2_component_t *component)
{
	struct v4l2_event evt;
	int ret;

	if (!component || component->fd < 0)
		return;

	if (!pitcher_poll(component->fd, POLLPRI, 0))
		return;

	memset(&evt, 0, sizeof(struct v4l2_event));
	ret = ioctl(component->fd, VIDIOC_DQEVENT, &evt);
	if (ret < 0)
		return;

	switch (evt.type) {
	case V4L2_EVENT_EOS:
		PITCHER_LOG("Receive EOS, fd = %d\n", component->fd);
		component->eos_received = true;
		break;
	case V4L2_EVENT_SOURCE_CHANGE:
		PITCHER_LOG("Receive Source Change, fd = %d\n", component->fd);
		component->resolution_change = true;
		break;
	default:
		break;
	}
}

static int check_v4l2_ready(void *arg, int *is_end)
{
	struct v4l2_component_t *component = arg;
	int i;

	if (!component || component->fd < 0) {
		if (is_end)
			*is_end = true;
		return false;
	}

	if (__is_v4l2_end(component)) {
		if (is_end)
			*is_end = true;
		return false;
	}

	if (V4L2_TYPE_IS_OUTPUT(component->type))
		__check_v4l2_events(component);

	if (V4L2_TYPE_IS_OUTPUT(component->type) && component->seek)
		return false;

	__clear_error_buffers(component);
	for (i = 0; i < component->buffer_count; i++) {
		if (component->buffers[i])
			return true;
	}

	if (V4L2_TYPE_IS_OUTPUT(component->type))
		return pitcher_poll(component->fd, POLLOUT, 0);
	else
		return pitcher_poll(component->fd, POLLIN, 0);
}

static int __transfer_output_buffer_userptr(struct pitcher_buffer *src,
					struct pitcher_buffer *dst)
{
	int i;
	unsigned long size;

	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->count == 1 && dst->count > 1) {
		unsigned long total = 0;
		unsigned long bytesused;

		for (i = 0; i < dst->count; i++) {
			bytesused = src->planes[0].bytesused - total;
			size = pitcher_get_buffer_plane_size(dst, i);
			bytesused = min(bytesused, size);

			dst->planes[i].virt = src->planes[0].virt + total;
			dst->planes[i].bytesused = bytesused;
			total += bytesused;
			if (total >= src->planes[0].bytesused)
				break;
		}
	} else if (src->count == dst->count) {
		for (i = 0; i < dst->count; i++)
			memcpy(&dst->planes[i], &src->planes[i],
					sizeof(struct pitcher_buf_ref));
	} else {
		return -RET_E_INVAL;
	}

	dst->priv = pitcher_get_buffer(src);
	return RET_OK;
}

static int __transfer_output_buffer_mmap(struct pitcher_buffer *src,
					struct pitcher_buffer *dst)
{
	int i;
	unsigned long size;

	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->count == 1 && dst->count > 1) {
		unsigned long total = 0;
		unsigned long bytesused;

		for (i = 0; i < dst->count; i++) {
			bytesused = src->planes[0].bytesused - total;
			size = pitcher_get_buffer_plane_size(dst, i);
			bytesused = min(bytesused, size);

			memcpy(dst->planes[i].virt,
					src->planes[0].virt + total,
					bytesused);
			dst->planes[i].bytesused = bytesused;
			total += bytesused;
			if (total >= src->planes[0].bytesused)
				break;
		}
		if (total < src->planes[0].bytesused)
			PITCHER_ERR("Not all data transferred 0x%lx / 0x%lx\n",
					src->planes[0].bytesused, total);
	} else if (src->count == dst->count) {
		for (i = 0; i < dst->count; i++) {
			if (dst->planes[i].size < src->planes[i].bytesused) {
				PITCHER_ERR("dst->planes[%d].size(0x%lx) < src->planes[%d].bytesused(0x%lx)\n",
					     i, dst->planes[i].size,
					     i, src->planes[i].bytesused);
				return -RET_E_INVAL;
			}
			memcpy(dst->planes[i].virt, src->planes[i].virt,
					src->planes[i].bytesused);
			dst->planes[i].bytesused = src->planes[i].bytesused;
		}
	} else {
		return -RET_E_INVAL;
	}

	return RET_OK;
}

static int __transfer_output_buffer_dmabuf(struct pitcher_buffer *src,
					struct pitcher_buffer *dst)
{
	int i;
	unsigned long size;

	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->count == 1 && dst->count > 1) {
		unsigned long total = 0;
		unsigned long bytesused;

		for (i = 0; i < dst->count; i++) {
			bytesused = src->planes[0].bytesused - total;
			size = pitcher_get_buffer_plane_size(dst, i);
			bytesused = min(bytesused, size);

			dst->planes[i].dmafd = src->planes[0].dmafd;
			dst->planes[i].bytesused = bytesused;
			dst->planes[i].offset = total;
			total += bytesused;
			if (total >= src->planes[0].bytesused)
				break;
		}
	} else if (src->count == dst->count) {
		for (i = 0; i < dst->count; i++)
			memcpy(&dst->planes[i], &src->planes[i],
					sizeof(struct pitcher_buf_ref));
	} else {
		return -RET_E_INVAL;
	}

	dst->priv = pitcher_get_buffer(src);
	return RET_OK;
}


static int __run_v4l2_output(struct v4l2_component_t *component,
				struct pitcher_buffer *pbuf)
{
	struct pitcher_buffer *buffer;
	int ret = RET_OK;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	if (!V4L2_TYPE_IS_OUTPUT(component->type))
		return RET_OK;

	__check_v4l2_events(component);

	if (!pbuf || !pbuf->count || !pbuf->planes)
		return -RET_E_NOT_READY;
	if (pbuf->planes[0].bytesused == 0)
		return 0;

	buffer = __get_buf(component);
	if (!buffer)
		return -RET_E_NOT_READY;

	switch (component->memory) {
	case V4L2_MEMORY_MMAP:
		ret = __transfer_output_buffer_mmap(pbuf, buffer);
		break;
	case V4L2_MEMORY_USERPTR:
		ret =  __transfer_output_buffer_userptr(pbuf, buffer);
		break;
	case V4L2_MEMORY_DMABUF:
		ret = __transfer_output_buffer_dmabuf(pbuf, buffer);
		break;
	default:
		ret = -RET_E_NOT_SUPPORT;
		break;
	}

	SAFE_RELEASE(component->buffers[buffer->index], pitcher_put_buffer);

	if (pbuf->flags & PITCHER_BUFFER_FLAG_SEEK)
		component->seek = true;

	return ret;
}

static int run_v4l2(void *arg, struct pitcher_buffer *pbuf)
{
	struct v4l2_component_t *component = arg;
	struct pitcher_buffer *buffer;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

	if (V4L2_TYPE_IS_OUTPUT(component->type) && pbuf) {
		ret = __run_v4l2_output(component, pbuf);
		if (ret == RET_OK)
			pbuf = NULL;
	}

	buffer = __dqbuf(component);
	if (!buffer)
		return -RET_E_NOT_READY;

	if (buffer->priv)
		SAFE_RELEASE(buffer->priv, pitcher_put_buffer);

	if (V4L2_TYPE_IS_OUTPUT(component->type))
		component->buffers[buffer->index] = pitcher_get_buffer(buffer);
	else
		pitcher_push_back_output(component->chnno, buffer);

	if (buffer->flags & PITCHER_BUFFER_FLAG_LAST) {
		PITCHER_LOG("LAST BUFFER received\n");
		component->end = true;
	}

	SAFE_RELEASE(buffer, pitcher_put_buffer);
	ret = RET_OK;

	if (V4L2_TYPE_IS_OUTPUT(component->type) && pbuf) {
		ret = __run_v4l2_output(component, pbuf);
		if (ret == RET_OK)
			pbuf = NULL;
	}

	if (component->run_hook)
		component->run_hook(component);

	return ret;
}

struct pitcher_unit_desc pitcher_v4l2_capture = {
	.name = "capture",
	.init = init_v4l2,
	.cleanup = cleanup_v4l2,
	.start = start_v4l2,
	.stop = stop_v4l2,
	.check_ready = check_v4l2_ready,
	.runfunc = run_v4l2,
	.buffer_count = 0,
	.fd = -1,
	.events = EPOLLIN | EPOLLET,
};

struct pitcher_unit_desc pitcher_v4l2_output = {
	.name = "output",
	.init = init_v4l2,
	.cleanup = cleanup_v4l2,
	.start = start_v4l2,
	.stop = stop_v4l2,
	.check_ready = check_v4l2_ready,
	.runfunc = run_v4l2,
	.buffer_count = 0,
	.fd = -1,
	.events = EPOLLOUT | EPOLLET | EPOLLPRI,
};

int is_v4l2_mplane(struct v4l2_capability *cap)
{
    if (cap->capabilities & (V4L2_CAP_VIDEO_CAPTURE_MPLANE
			| V4L2_CAP_VIDEO_OUTPUT_MPLANE)
			&& cap->capabilities & V4L2_CAP_STREAMING)
        return TRUE;

    if (cap->capabilities & V4L2_CAP_VIDEO_M2M_MPLANE)
        return TRUE;

    return FALSE;
}

int is_v4l2_splane(struct v4l2_capability *cap)
{
    if (cap->capabilities & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT)
			&& cap->capabilities & V4L2_CAP_STREAMING)
        return TRUE;

    if (cap->capabilities & V4L2_CAP_VIDEO_M2M)
        return TRUE;

    return FALSE;
}

int check_v4l2_support_fmt(int fd, uint32_t type, uint32_t pixelformat)
{
	struct v4l2_fmtdesc fmt_desc;

	fmt_desc.type = type;
	fmt_desc.index = 0;

	while (!v4l2_enum_fmt(fd, &fmt_desc)) {
		if (pitcher_get_format_by_fourcc(fmt_desc.pixelformat) == pixelformat) {
			PITCHER_LOG("%c%c%c%c %s is found\n",
					fmt_desc.pixelformat,
					fmt_desc.pixelformat >> 8,
					fmt_desc.pixelformat >> 16,
					fmt_desc.pixelformat >> 24,
					fmt_desc.description);
			return TRUE;
		}
		fmt_desc.index++;
	}

	return FALSE;
}

int check_v4l2_device_type(int fd, unsigned int out_fmt, unsigned int cap_fmt)
{
	uint32_t out_type;
	uint32_t cap_type;
	int out_find = TRUE;
	int cap_find = TRUE;
	struct v4l2_capability cap;

	if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0)
		return FALSE;

	PITCHER_LOG("check v4l2 devcie for %s to %s\n",
			pitcher_get_format_name(out_fmt),
			pitcher_get_format_name(cap_fmt));
	if (is_v4l2_mplane(&cap)) {
		cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else {
		cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	}

	if (cap_fmt != PIX_FMT_NONE)
		cap_find = check_v4l2_support_fmt(fd, cap_type, cap_fmt);
	if (out_fmt != PIX_FMT_NONE)
		out_find = check_v4l2_support_fmt(fd, out_type, out_fmt);

	if (cap_find && out_find)
		return TRUE;

	return FALSE;
}

int lookup_v4l2_device_and_open(unsigned int out_fmt, unsigned int cap_fmt)
{
	const int MAX_INDEX = 64;
	int i;
	int fd;
	char devname[MAXPATHLEN];

	for (i = 0; i < MAX_INDEX; i++) {
		snprintf(devname, MAXPATHLEN - 1, "/dev/video%d", i);
		fd = open(devname, O_RDWR | O_NONBLOCK);
		if (fd < 0)
			continue;
		if (check_v4l2_device_type(fd, out_fmt, cap_fmt) == TRUE) {
			PITCHER_LOG("open %s\n", devname);
			return fd;
		}
		SAFE_CLOSE(fd, close);
	}

	return -1;
}

int set_ctrl(int fd, int id, int value)
{
	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;
	int ret;

	memset(&qctrl, 0, sizeof(qctrl));
	qctrl.id = id;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, &qctrl);
	if (ret < 0) {
		PITCHER_ERR("query ctrl(%d) fail, %s, %d\n", id, strerror(errno), errno);
		return -RET_E_INVAL;
	}

	value = max(value, qctrl.minimum);
	value = min(value, qctrl.maximum);

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ctrl.value = value;

	ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		PITCHER_ERR("set ctrl(%s : %d) fail\n", qctrl.name, value);
		return -RET_E_INVAL;
	}

	PITCHER_LOG("[S]%s : %d (%d)\n", qctrl.name, ctrl.value, value);

	return ret;
}

int get_ctrl(int fd, int id, int *value)
{
	struct v4l2_queryctrl qctrl;
	struct v4l2_control ctrl;
	int ret;

	memset(&qctrl, 0, sizeof(qctrl));
	qctrl.id = id;
	ret = ioctl(fd, VIDIOC_QUERYCTRL, &qctrl);
	if (ret < 0)
		return -RET_E_INVAL;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = id;
	ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
	if (ret < 0)
		return -RET_E_INVAL;

	if (value)
		*value = ctrl.value;

	return 0;
}

uint32_t get_image_size(uint32_t fmt, uint32_t width, uint32_t height, uint32_t alignment)
{
	uint32_t size = width * height;
	struct pix_fmt_info format;

	memset(&format, 0, sizeof(format));
	format.format = fmt;
	format.width = width;
	format.height = height;
	if (!pitcher_get_pix_fmt_info(&format, alignment))
		size = format.size;

	return size;
}

