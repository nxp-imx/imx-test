OBJS = 	$(OBJDIR)/autorun-v4l2.sh \
	$(OBJDIR)/mxc_v4l2_output.out \
	$(OBJDIR)/mxc_v4l2_still.out \
	$(OBJDIR)/mxc_v4l2_tvin.out \
	$(OBJDIR)/mxc_v4l2_overlay.out \
	$(OBJDIR)/mxc_v4l2_capture.out

all : $(OBJS)
	mv $(OBJDIR)/autorun-v4l2.sh $(OBJDIR)/V4L2
	mv $(OBJDIR)/mxc_v4l2_output.out $(OBJDIR)/V4L2
	mv $(OBJDIR)/mxc_v4l2_still.out $(OBJDIR)/V4L2
	mv $(OBJDIR)/mxc_v4l2_tvin.out $(OBJDIR)/V4L2
	mv $(OBJDIR)/mxc_v4l2_overlay.out $(OBJDIR)/V4L2
	mv $(OBJDIR)/mxc_v4l2_capture.out $(OBJDIR)/V4L2

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules


