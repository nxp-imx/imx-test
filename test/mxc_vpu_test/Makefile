HAS_VPU ?= true
ifeq ($(HAS_VPU),true)
ifneq ($(ARCH),arm64)
DIR = VPU
BUILD = mxc_vpu_test.out
LDFLAGS = -lvpu -lipu -lrt -lpthread
mxc_vpu_test.out = main.o dec.o enc.o capture.o display.o fb.o utils.o \
	           loopback.o transcode.o
COPY = README autorun-vpu.sh config_dec config_enc config_encdec config_net akiyo.mp4
endif
endif
