ifeq ($(ARCH),arm64)
DIR = V4L2_VPU
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
			pitcher/platform_8x.o \
			pitcher/convert.o

LDFLAGS += -lpthread
COPY = README
endif
