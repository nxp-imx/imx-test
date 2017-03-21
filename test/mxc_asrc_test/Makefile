#
# Makefile for OSS sound driver test app.
#

CFLAGS += -lpthread

OBJS = \
	$(OBJDIR)/mxc_asrc_test.out \
	$(OBJDIR)/autorun-asrc.sh

all : $(OBJS)
	cp audio8k16S.wav $(OBJDIR)/ASRC
	mv $(OBJDIR)/mxc_asrc_test.out $(OBJDIR)/ASRC
	mv $(OBJDIR)/autorun-asrc.sh $(OBJDIR)/ASRC

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
