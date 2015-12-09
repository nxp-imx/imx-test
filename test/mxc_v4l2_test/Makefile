# list of platforms which do not want this test case
EXCLUDE_LIST:=

ifeq (,$(findstring $(PLATFORM), $(EXCLUDE_LIST)))
OBJS = 	$(OBJDIR)/autorun-v4l2.sh \
	$(OBJDIR)/mxc_v4l2_output.out \
	$(OBJDIR)/mxc_v4l2_still.out \
	$(OBJDIR)/mxc_v4l2_tvin.out \
	$(OBJDIR)/mxc_v4l2_overlay.out \
	$(OBJDIR)/mxc_v4l2_capture.out
else
OBJS =
endif

all : $(OBJS)

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules


