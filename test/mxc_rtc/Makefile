OBJS = $(OBJDIR)/rtctest.out $(OBJDIR)/rtcwakeup.out $(OBJDIR)/autorun-rtc.sh

all : $(OBJS)
	mv $(OBJDIR)/rtctest.out $(OBJDIR)/SRTC
	mv $(OBJDIR)/rtcwakeup.out $(OBJDIR)/SRTC
	mv $(OBJDIR)/autorun-rtc.sh $(OBJDIR)/SRTC
	cp README $(OBJDIR)/SRTC

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

