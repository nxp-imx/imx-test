OBJS = $(OBJDIR)/mxc_isl29023.out

all : $(OBJS)
	mv $(OBJDIR)/mxc_isl29023.out $(OBJDIR)/Sensors
	cp README_als $(OBJDIR)/Sensors

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
