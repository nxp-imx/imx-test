ifeq ($(ARCH),arm64)
DIR = V4L2_VPU
BUILD = mxc_v4l2_vpu_dec.out \
		mxc_v4l2_vpu_enc.out

mxc_v4l2_vpu_dec.out = mxc_vpu_dec.o
mxc_v4l2_vpu_enc.out = mxc_vpu_enc.o

LDFLAGS += -lpthread
COPY = README
endif
