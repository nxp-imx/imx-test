SRCS = pxp_test.c utils.c
CFLAGS += -lpxp -lpthread

OBJS = $(OBJDIR)/pxp_test.out \
	$(OBJDIR)/autorun-pxp.sh
$(OBJS): $(SRCS)
	mkdir -p $(OBJDIR)
	$(VERBOSE) $(CC) -g $(INC) $(CFLAGS) $^ $(LIBS) -o $@

all: $(OBJS)

.PHONY: clean
clean:
	rm -f $(OBJS)

include ../make.rules
