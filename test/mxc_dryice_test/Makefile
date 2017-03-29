#*****************************************************************************
#
# makefile for dryice test program
#
#****************************************************************************/
OBJS = $(OBJDIR)/dryice.sh

all : $(OBJS)
	mv $(OBJDIR)/dryice.sh $(OBJDIR)/Dryice
	cp README $(OBJDIR)/Dryice

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
