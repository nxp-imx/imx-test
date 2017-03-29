#
# Makefile for MediaLB driver test app.

INC += -I$(LINUXPATH)/include

OBJS = $(OBJDIR)/mxc_mlb_test.out

all : $(OBJS)
	mv $(OBJDIR)/mxc_mlb_test.out $(OBJDIR)/Media_Local_Bus
	cp README $(OBJDIR)/Media_Local_Bus

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
