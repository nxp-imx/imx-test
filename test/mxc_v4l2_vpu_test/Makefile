ifeq ($(ARCH),arm64)
DIR = V4L2_VPU
BUILD = mxc_v4l2_vpu_dec.out \
	mxc_v4l2_vpu_enc.out \
	mxc_v4l2_vpu_test.out

mxc_v4l2_vpu_dec.out = mxc_vpu_dec.o
mxc_v4l2_vpu_enc.out = mxc_v4l2_vpu_enc.o \
			pitcher/memory.o \
			pitcher/misc.o \
			pitcher/queue.o \
			pitcher/loop.o \
			pitcher/obj.o \
			pitcher/buffer.o \
			pitcher/pixfmt.o \
			pitcher/pipe.o \
			pitcher/unit.o \
			pitcher/core.o \
			pitcher/v4l2.o \
			pitcher/dmabuf.o \
			pitcher/parse.o \
			pitcher/h264_parse.o \
			pitcher/h265_parse.o \
			pitcher/jpeg_parse.o \
			pitcher/h263_parse.o \
			pitcher/mpegx_parse.o \
			pitcher/vpx_parse.o \
			pitcher/vc1_parse.o \
			pitcher/vp6_parse.o \
			pitcher/platform.o \
			pitcher/platform_8x.o \
			pitcher/convert.o \
			pitcher/sysloadso.o \
			dmanode.o \
			pitcher/bitstream.o

ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/linux/dma-heap.h))
CFLAGS += -DENABLE_DMA_HEAP
endif

ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/imx/linux/ion.h))
CFLAGS += -DENABLE_ION
endif

ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/g2d.h))
mxc_v4l2_vpu_enc.out += pitcher/g2dcvt.o
CFLAGS += -DENABLE_G2D
endif

ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/wayland-client.h))
ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/wayland-client-protocol.h))
ifneq (,$(wildcard $(SDKTARGETSYSROOT)/usr/include/libdrm/drm_fourcc.h))
mxc_v4l2_vpu_enc.out += waylandsink.o \
			wayland-generated-protocols/xdg-shell-protocol.o \
			wayland-generated-protocols/linux-dmabuf-unstable-v1-protocol.o
CFLAGS += -DENABLE_WAYLAND
LDFLAGS += -lwayland-client
endif
endif
endif

mxc_v4l2_vpu_test.out = $(mxc_v4l2_vpu_enc.out)

GIT_SHA=`git -C . rev-parse --short=12 HEAD`
GIT_COMMIT_DATE=`TZ=UTC-8 git -C . show --quiet --date='format-local:\"%F %T\"' --format='%cd'`
CFLAGS += -DGIT_SHA="\"$(GIT_SHA)\"" -DGIT_COMMIT_DATE="$(GIT_COMMIT_DATE)"
LDFLAGS += -ldl -lpthread -rdynamic
COPY = README
endif
