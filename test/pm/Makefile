OBJS = \
	$(OBJDIR)/suspend_quick_auto.sh \
	$(OBJDIR)/suspend_random_auto.sh

all : $(OBJS)
	mv $(OBJDIR)/suspend_quick_auto.sh $(OBJDIR)/Power_Management
	mv $(OBJDIR)/suspend_random_auto.sh $(OBJDIR)/Power_Management

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

