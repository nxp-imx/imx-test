#
#Makefile for motion sensor driver test.
#
OBJS = $(OBJDIR)/acctest.out  $(OBJDIR)/evtest.out

all : $(OBJS)
	mv $(OBJDIR)/acctest.out $(OBJDIR)/Sensors
	mv $(OBJDIR)/evtest.out $(OBJDIR)/Sensors

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

