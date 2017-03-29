OBJS = \
	$(OBJDIR)/etm

all : $(OBJS)
	mv $(OBJDIR)/etm $(OBJDIR)/ETM
	cp README $(OBJDIR)/ETM

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

