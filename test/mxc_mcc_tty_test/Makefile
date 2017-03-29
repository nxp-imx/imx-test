#
# Makefile for MXC MCC TTY driver test app.
#

OBJS = $(OBJDIR)/mxc_mcc_tty_test.out

all : $(OBJS)
	mv $(OBJDIR)/mxc_mcc_tty_test.out $(OBJDIR)/Remote_Processor_Messaging
	cp README $(OBJDIR)/Remote_Processor_Messaging

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
