# list of platforms which want this test case
INCLUDE_LIST:=IMX50 IMX51 IMX5 IMX6Q IMX6S IMX7 IMX6UL IMX8ULP
ifeq ($(PLATFORM), $(findstring $(PLATFORM), $(INCLUDE_LIST)))
DIR = Display
BUILD = pxp_test.out
pxp_test.out = pxp_test.o utils.o
LDFLAGS = -lpthread -lpxp -lstdc++
COPY = README_pxp
endif

