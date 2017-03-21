OBJS = $(OBJDIR)/autorun-firi.sh

SCRIPTS = $(OBJDIR)/mxc_irlan_test.sh

all : $(OBJS) $(SCRIPTS)
	mv $(OBJDIR)/autorun-firi.sh $(OBJDIR)/ENET
	mv $(OBJDIR)/mxc_irlan_test.sh $(OBJDIR)/ENET

.PHONY: clean
clean :
	rm -f $(OBJS) $(SCRIPTS)

#
# include the Rules
#
include ../make.rules

