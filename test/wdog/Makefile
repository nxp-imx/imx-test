OBJS = \
	$(OBJDIR)/wdt_driver_test.out \
	$(OBJDIR)/autorun-wdog.sh \
	$(OBJDIR)/dbg_monitor.sh

all : $(OBJS)
	mv $(OBJDIR)/wdt_driver_test.out $(OBJDIR)/Watchdog
	mv $(OBJDIR)/autorun-wdog.sh $(OBJDIR)/Watchdog
	mv $(OBJDIR)/dbg_monitor.sh $(OBJDIR)/Watchdog
	cp README $(OBJDIR)/Watchdog

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

