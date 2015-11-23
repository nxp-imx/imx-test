CFLAGS += -lm
LIBS += -lX11 -lXdamage -lXrender -lXrandr

OBJS = $(OBJDIR)/mxc_epdc_x11_fb_test.out $(OBJDIR)/x11_overlay.out

all : $(OBJS)

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
