#
# Makefile for MediaLB driver test app.
#
INC += -I$(LINUXPATH)/include
CFLAGS += -lpthread -lcec -lrt

OBJS = $(OBJDIR)/mxc_cec_test.out

all : $(OBJS)

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
