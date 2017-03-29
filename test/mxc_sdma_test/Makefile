HPATH += -I$(LINUXPATH)/include

OBJS = $(OBJDIR)/mxc_sdma_test.out

all : $(OBJS)
	mv $(OBJDIR)/mxc_sdma_test.out $(OBJDIR)/SDMA_API
	cp README $(OBJDIR)/SDMA_API

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

