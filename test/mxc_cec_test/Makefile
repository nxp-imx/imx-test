ifneq ($(ARCH),arm64)
DIR = HDMI
BUILD = mxc_cec_test.out
LDFLAGS = -pthread -lpthread -lcec -lrt -lstdc++
COPY = README
endif

ifeq ($(ARCH),arm64)
DIR = HDMI
BUILD = mx8_cec_test.out
LDFLAGS = -pthread -lpthread -lrt -lstdc++
COPY = README
endif

