OBJS = $(OBJDIR)/autorun-ata.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-ata.sh $(OBJDIR)/SATA
	cp README $(OBJDIR)/SATA

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

