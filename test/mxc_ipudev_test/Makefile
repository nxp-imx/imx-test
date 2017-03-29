SRCS = mxc_ipudev_test.c utils.c
LIBS += -lm -lrt

OBJS = $(OBJDIR)/mxc_ipudev_test.out
$(OBJS): $(SRCS)
	mkdir -p $(OBJDIR)
	$(VERBOSE) $(CC) -g $(INC) $(CFLAGS) $^ $(LIBS) -o $@
	cp ipudev_config_file $(OBJDIR)/Display
	cp autorun-ipu.sh $(OBJDIR)/Display
	cp stefan_interlaced_320x240_5frames.yv12 $(OBJDIR)/Display
	cp wall-1024x768-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_ipudev_test.out $(OBJDIR)/Display
	cp README_ipu $(OBJDIR)/Display

all: $(OBJS)

.PHONY: clean
clean:
	rm -f $(OBJS)

include ../make.rules
