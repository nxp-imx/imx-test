OBJS = $(OBJDIR)/autorun-oprofile.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-oprofile.sh $(OBJDIR)/OProfile
	cp README $(OBJDIR)/OProfile

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

