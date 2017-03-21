OBJS = \
	$(OBJDIR)/mxc_tuner_test.out \
	$(OBJDIR)/mxc_tuner_test.sh

all : $(OBJS)
	mv $(OBJDIR)/mxc_tuner_test.out $(OBJDIR)/ALSA
	mv $(OBJDIR)/mxc_tuner_test.sh $(OBJDIR)/ALSA

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

