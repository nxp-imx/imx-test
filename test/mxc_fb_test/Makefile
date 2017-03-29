LIBS += -lm

OBJS = $(OBJDIR)/autorun-fb.sh $(OBJDIR)/mxc_tve_test.sh $(OBJDIR)/mxc_fb_test.out \
       $(OBJDIR)/mxc_epdc_fb_test.out $(OBJDIR)/mxc_epdc_v2_fb_test.out \
       $(OBJDIR)/mxc_spdc_fb_test.out $(OBJDIR)/mxc_fb_vsync_test.out \
       $(OBJDIR)/desk240x180-565.rgb $(OBJDIR)/daisy-640x480-565.rgb \
       $(OBJDIR)/rose-800x600-565.rgb $(OBJDIR)/wall-1024x768-565.rgb $(OBJDIR)/pansy-1280x720-565.rgb \
       $(OBJDIR)/plumbago-1280x1024-565.rgb $(OBJDIR)/testcard-1920x1080-bgra.rgb \
       $(OBJDIR)/testcard-1920x1080-565.rgb

all : $(OBJS)
	mv $(OBJDIR)/autorun-fb.sh $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_tve_test.sh $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_fb_test.out $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_epdc_fb_test.out $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_epdc_v2_fb_test.out $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_spdc_fb_test.out $(OBJDIR)/Display
	mv $(OBJDIR)/mxc_fb_vsync_test.out $(OBJDIR)/Display
	mv $(OBJDIR)/desk240x180-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/daisy-640x480-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/rose-800x600-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/wall-1024x768-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/pansy-1280x720-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/plumbago-1280x1024-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/testcard-1920x1080-565.rgb $(OBJDIR)/Display
	mv $(OBJDIR)/testcard-1920x1080-bgra.rgb $(OBJDIR)/Display
	cp README $(OBJDIR)/Display

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules
