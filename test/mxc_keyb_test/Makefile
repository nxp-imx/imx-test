OBJS = $(OBJDIR)/autorun-keypad.sh $(OBJDIR)/mxc_keyb_test.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-keypad.sh $(OBJDIR)/Keyboard
	mv $(OBJDIR)/mxc_keyb_test.sh $(OBJDIR)/Keyboard

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

