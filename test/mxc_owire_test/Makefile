OBJS = $(OBJDIR)/autorun-owire.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-owire.sh $(OBJDIR)/OWire
	cp README $(OBJDIR)/OWire

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

