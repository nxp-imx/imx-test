# list of platforms which want this test case
INCLUDE_LIST:=IMX6Q IMX6SL IMX6SX IMX6UL IMX7D IMX8ULP
ifeq ($(PLATFORM), $(filter $(PLATFORM), $(INCLUDE_LIST)))
DIR = Display
BUILD = pxp_test.out
pxp_test.out = pxp_test.o utils.o
LDFLAGS = -lpthread -lpxp -lstdc++
COPY = README_pxp
endif

