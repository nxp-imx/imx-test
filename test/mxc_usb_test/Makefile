OBJS = $(OBJDIR)/autorun-usb-gadget.sh $(OBJDIR)/autorun-usb-host.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-usb-gadget.sh $(OBJDIR)/USB
	mv $(OBJDIR)/autorun-usb-host.sh $(OBJDIR)/USB
	cp README $(OBJDIR)/USB

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

