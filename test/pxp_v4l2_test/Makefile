# Just use the pxp library to get contiguous physical memory for USERPTR testing
# However, be aware that this is not the only way.
CFLAGS += -lpthread -lpxp -DGET_CONTI_PHY_MEM_VIA_PXP_LIB

OBJS = 	$(OBJDIR)/pxp_v4l2_test.out \
	$(OBJDIR)/pxp_v4l2_out_test.sh \
	$(OBJDIR)/mx6s_v4l2_capture.out

all : $(OBJS)

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

