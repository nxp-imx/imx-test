OBJS = $(OBJDIR)/gpu.sh \
	   $(OBJDIR)/gpuinfo.sh

all: $(OBJS)
	mv $(OBJDIR)/gpu.sh $(OBJDIR)/GPU
	mv $(OBJDIR)/gpuinfo.sh $(OBJDIR)/GPU

.PHONY: clean
clean:
	rm -f $(OBJS)

include ../make.rules
