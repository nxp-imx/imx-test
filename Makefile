TOPDIR	:= $(shell /bin/pwd)
OBJDIR=$(TOPDIR)/platform/$(PLATFORM)/

MISC_DIR := $(shell echo $(TOPDIR) | sed 's/^.*\///')
PKG_NAME := $(MISC_DIR).tar.gz

#
# ltib requires CROSS_COMPILE to be undefined
#
#ifeq "$(CROSS_COMPILE)" ""
#$(error CROSS_COMPILE variable not set.)
#endif

ifeq "$(KBUILD_OUTPUT)" ""
$(warn KBUILD_OUTPUT variable not set.)
endif

ifeq "$(PLATFORM)" ""
$(warn "PLATFORM variable not set. Check if you have specified PLATFORM variable")
endif

ifeq "$(DESTDIR)" ""
install_target=install_dummy
else
install_target=$(shell if [ -d $(TOPDIR)/platform/$(PLATFORM) ]; then echo install_actual; \
		       else echo install_dummy; fi; )
endif

#
# Export all variables that might be needed by other Makefiles
#
export INC CROSS_COMPILE LINUXPATH PLATFORM TOPDIR OBJDIR

.PHONY: test module_test doc clean distclean pkg install

all : test doc

test:
	@echo
	@echo Invoking test make...
	mkdir -p $(OBJDIR)/ALSA
	mkdir -p $(OBJDIR)/ASRC
	mkdir -p $(OBJDIR)/DCIC
	mkdir -p $(OBJDIR)/Display
	mkdir -p $(OBJDIR)/Dryice
	mkdir -p $(OBJDIR)/ECSPI
	mkdir -p $(OBJDIR)/ENET
	mkdir -p $(OBJDIR)/ETM
	mkdir -p $(OBJDIR)/GPU
	mkdir -p $(OBJDIR)/HDMI
	mkdir -p $(OBJDIR)/I2C
	mkdir -p $(OBJDIR)/IIM_Driver
	mkdir -p $(OBJDIR)/Keyboard
	mkdir -p $(OBJDIR)/L2_Switch_Driver
	mkdir -p $(OBJDIR)/Media_Local_Bus
	mkdir -p $(OBJDIR)/MMC_SD_SDIO_Host
	mkdir -p $(OBJDIR)/MMDC
	mkdir -p $(OBJDIR)/OProfile
	mkdir -p $(OBJDIR)/OWire
	mkdir -p $(OBJDIR)/Power_Management
	mkdir -p $(OBJDIR)/Remote_Processor_Messaging
	mkdir -p $(OBJDIR)/SATA
	mkdir -p $(OBJDIR)/SDMA_API
	mkdir -p $(OBJDIR)/Sensors
	mkdir -p $(OBJDIR)/SIM
	mkdir -p $(OBJDIR)/SRTC
	mkdir -p $(OBJDIR)/UART
	mkdir -p $(OBJDIR)/USB
	mkdir -p $(OBJDIR)/V4L2
	mkdir -p $(OBJDIR)/VPU
	mkdir -p $(OBJDIR)/Watchdog
	$(MAKE) -C $(TOPDIR)/test

module_test:
	@echo
	@echo Building test modules...
	$(MAKE) -C $(TOPDIR)/module_test OBJDIR=$(OBJDIR)/modules

doc:
	@echo
	@echo Generating user manual...
	$(MAKE) -C $(TOPDIR)/doc

install: $(install_target)

install_dummy:
	@echo -e "\n**DESTDIR not set or Build not yet done. No installtion done."
	@echo -e "**If build is complete files will be under $(TOPDIR)/platform/$(PLATFORM)/ dir."

install_actual:
	@echo -e "\nInstalling files from platform/$(PLATFORM) to $(DESTDIR)"
	mkdir -p $(DESTDIR)
	-rm -rf $(DESTDIR)/*
	cp -rf $(OBJDIR)/* $(DESTDIR)
	cp autorun.sh test-utils.sh all-suite.txt README $(DESTDIR)

distclean: clean

clean:
	$(MAKE) -C $(TOPDIR)/test $@
	$(MAKE) -C $(TOPDIR)/module_test $@
	$(MAKE) -C $(TOPDIR)/doc $@
	-rm -rf platform

pkg : clean
	tar --exclude CVS -C .. $(EXCLUDES) -czf $(PKG_NAME) $(MISC_DIR)

%::
	$(MAKE) -C $(TOPDIR)/test $@
