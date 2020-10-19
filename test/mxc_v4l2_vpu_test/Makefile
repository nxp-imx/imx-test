ifeq ($(ARCH),arm64)
DIR = V4L2_VPU
VSIPARSE ?= y
BUILD = mxc_v4l2_vpu_dec.out \
	mxc_v4l2_vpu_enc.out

mxc_v4l2_vpu_dec.out = mxc_vpu_dec.o
mxc_v4l2_vpu_enc.out = mxc_v4l2_vpu_enc.o \
			pitcher/memory.o \
			pitcher/misc.o \
			pitcher/queue.o \
			pitcher/loop.o \
			pitcher/obj.o \
			pitcher/buffer.o \
			pitcher/pipe.o \
			pitcher/unit.o \
			pitcher/core.o \
			pitcher/v4l2.o \
			pitcher/parse.o \
			pitcher/h264_parse.o \
			pitcher/h265_parse.o \
			pitcher/jpeg_parse.o \
			pitcher/platform_8x.o \
			pitcher/convert.o
ifeq ($(VSIPARSE),y)
mxc_v4l2_vpu_enc.out += pitcher/vsi_parse.o \
			pitcher/vsi_parse_mpeg2.o \
			pitcher/vsi_parse_h26x.o \
			pitcher/vsi_parse_vpx.o
CFLAGS += -DVSI_PARSE
endif

LDFLAGS += -lpthread
COPY = README
endif
