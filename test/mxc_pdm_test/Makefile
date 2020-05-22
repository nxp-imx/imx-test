DIR = Audio
BUILD = mxc_pdm_test.out
mxc_pdm_test.out = mxc_pdm_test.o mxc_pdm_alsa.o mxc_pdm_cic.o
LDFLAGS += -lasound -lpthread
CFLAGS += -O3 -fstrict-overflow
HAS_IMX_SW_PDM ?= flase
ifeq ($(HAS_IMX_SW_PDM),true)
ifdef SDKTARGETSYSROOT
    LDFLAGS += -limxswpdm -lstdc++ -lm
    CFLAGS += -I${SDKTARGETSYSROOT}/usr/include/imx-mm/audio-codec/swpdm -DHAS_IMX_SW_PDM
endif
endif
COPY = README
