OBJS = $(OBJDIR)/dump-clocks.sh \
       $(OBJDIR)/dump-clocks-dot.sh

all : $(OBJS)
	mv $(OBJDIR)/dump-clocks.sh $(OBJDIR)/Power_Management
	mv $(OBJDIR)/dump-clocks-dot.sh $(OBJDIR)/Power_Management
	cp README_clocks $(OBJDIR)/Power_Management

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

