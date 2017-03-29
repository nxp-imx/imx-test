OBJS = $(OBJDIR)/mxc_l2switch_test.out

all : $(OBJS)
	mv $(OBJDIR)/mxc_l2switch_test.out $(OBJDIR)/L2_Switch_Driver
	cp README $(OBJDIR)/L2_Switch_Driver

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

