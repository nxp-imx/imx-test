/*
 * Copyright 2018 NXP
 *
 * utils/v4l2.c
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

	memset(&format, 0, sizeof(format));
	format.type = component->type;
	ioctl(fd, VIDIOC_G_FMT, &format);
	if (!component->pixelformat) {
		if (!V4L2_TYPE_IS_MULTIPLANAR(component->type))
			component->pixelformat = format.fmt.pix.pixelformat;
		else
			component->pixelformat = format.fmt.pix_mp.pixelformat;
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
		format.fmt.pix.pixelformat = component->pixelformat;
		format.fmt.pix.width = component->width;
		format.fmt.pix.height = component->height;
		format.fmt.pix.bytesperline = component->bytesperline;
		format.fmt.pix.sizeimage = component->sizeimage;
	} else {
		format.fmt.pix_mp.pixelformat = component->pixelformat;
		format.fmt.pix_mp.width = component->width;
		format.fmt.pix_mp.height = component->height;
		format.fmt.pix_mp.num_planes = 2;
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
		component->pixelformat = format.fmt.pix.pixelformat;
		component->sizeimage = format.fmt.pix.sizeimage;
		component->bytesperline = format.fmt.pix.bytesperline;
	} else {
		component->num_planes = format.fmt.pix_mp.num_planes;
		component->width = format.fmt.pix_mp.width;
		component->height = format.fmt.pix_mp.height;
		component->pixelformat = format.fmt.pix_mp.pixelformat;
		component->sizeimage = format.fmt.pix_mp.plane_fmt[0].sizeimage;
		component->bytesperline = format.fmt.pix_mp.plane_fmt[0].bytesperline;
	}

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
	component->buffer_count = req_bufs.count;

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
			buffer->flags |= PITCHER_BUFFER_FLAG_LAST;
		}
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
	}
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
		PITCHER_LOG("streamon fail, error : %s\n", strerror(errno));
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

static int init_v4l2_mmap_plane(struct pitcher_plane *plane,
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

static int uninit_v4l2_mmap_plane(struct pitcher_plane *plane,
				unsigned int index, void *arg)
{
	if (plane && plane->virt && plane->size)
		munmap(plane->virt, plane->size);

	return RET_OK;
}

static int init_v4l2_userptr_plane(struct pitcher_plane *plane,
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

static int uninit_v4l2_userptr_plane(struct pitcher_plane *plane,
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
	default:
		return -RET_E_INVAL;
	}

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

static int start_v4l2(void *arg)
{
	struct v4l2_component_t *component = arg;
	int ret;

	if (!component || component->fd < 0)
		return -RET_E_INVAL;

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

	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->count == 1 && dst->count > 1) {
		unsigned long total = 0;
		unsigned long bytesused;

		for (i = 0; i < dst->count; i++) {
			bytesused = src->planes[0].bytesused - total;
			if (bytesused > dst->planes[i].size)
				bytesused = dst->planes[i].size;

			dst->planes[i].virt = src->planes[0].virt + total;
			dst->planes[i].bytesused = bytesused;
			total += bytesused;
			if (total >= src->planes[0].bytesused)
				break;
		}
	} else if (src->count == dst->count) {
		for (i = 0; i < dst->count; i++)
			memcpy(&dst->planes[i], &src->planes[i],
					sizeof(struct pitcher_plane));
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

	if (!src || !dst)
		return -RET_E_NULL_POINTER;

	if (src->count == 1 && dst->count > 1) {
		unsigned long total = 0;
		unsigned long bytesused;

		for (i = 0; i < dst->count; i++) {
			bytesused = src->planes[0].bytesused - total;
			if (bytesused > dst->planes[i].size)
				bytesused = dst->planes[i].size;

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

	if (!pbuf)
		return -RET_E_NOT_READY;

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
	default:
		ret = -RET_E_NOT_SUPPORT;
		break;
	}

	SAFE_RELEASE(component->buffers[buffer->index], pitcher_put_buffer);

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

static int v4l2_enum_fmt(int fd, struct v4l2_fmtdesc *fmt)
{
	return ioctl(fd, VIDIOC_ENUM_FMT, fmt);
}

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

static int check_v4l2_support_fmt(int fd, uint32_t type, uint32_t pixelformat)
{
	struct v4l2_fmtdesc fmt_desc;

	fmt_desc.type = type;
	fmt_desc.index = 0;

	while (!v4l2_enum_fmt(fd, &fmt_desc)) {
		if (fmt_desc.pixelformat == pixelformat)
			return TRUE;
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

	if (is_v4l2_mplane(&cap)) {
		cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	} else {
		cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	}

	if (cap_fmt)
		cap_find = check_v4l2_support_fmt(fd, cap_type, cap_fmt);
	if (out_fmt)
		out_find = check_v4l2_support_fmt(fd, out_type, out_fmt);

	if ((cap_find & out_find))
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
		PITCHER_ERR("query ctrl(%d) fail\n", id);
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

uint32_t get_image_size(uint32_t fmt, uint32_t width, uint32_t height)
{
	uint32_t size = width * height;

	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV21:
		size = ((width * 12) >> 3) * height;
		break;
	case V4L2_PIX_FMT_YUYV:
		size = width * height * 2;
		break;
	case V4L2_PIX_FMT_NV12_TILE:
		width = ALIGN(width, IMX8X_HORIZONTAL_STRIDE);
		height = ALIGN(height, IMX8X_VERTICAL_STRIDE);
		size = ((width * 12) >> 3) * height;
		break;
	case V4L2_PIX_FMT_NV12_TILE_10BIT:
		width = ALIGN(((width * 10) >> 3), IMX8X_HORIZONTAL_STRIDE);
		height = ALIGN(height, IMX8X_VERTICAL_STRIDE);
		size = ((width * 12) >> 3) * height;
		break;
	default:
		break;
	}

	return size;
}
