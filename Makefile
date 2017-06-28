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

Q := $(if $(V),,@)

#
# Export all variables that might be needed by other Makefiles
#
export INC CROSS_COMPILE LINUXPATH PLATFORM TOPDIR OBJDIR Q

.PHONY: test module_test doc clean distclean pkg install

all : test

define do_make =
	@echo "	MAKE	$(1) $(2)"
	+$(Q)$(MAKE) $(if $(Q),--no-print-directory,) -C $(1) $(2)
endef

test:
	$(call do_make,test)

module_test:
	$(call do_make,module_test,OBJDIR=$(OBJDIR)/modules)

doc:
	$(call do_make,doc)

install: $(install_target)

install_dummy:
	@echo -e "\n**DESTDIR not set or Build not yet done. No installation done."
	@echo -e "**If build is complete files will be under $(TOPDIR)/platform/$(PLATFORM)/ dir."

install_actual:
	@echo -e "\nInstalling files from platform/$(PLATFORM) to $(DESTDIR)"
	mkdir -p $(DESTDIR)
	-rm -rf $(DESTDIR)/*
	cp -rf $(OBJDIR)/* $(DESTDIR)
	cp autorun.sh test-utils.sh all-suite.txt README $(DESTDIR)

distclean: clean

clean:
	$(call do_make,test,clean)
	$(call do_make,module_test,clean)
	$(call do_make,doc,clean)
	$(Q)-rm -rf platform

pkg : clean
	tar --exclude CVS -C .. $(EXCLUDES) -czf $(PKG_NAME) $(MISC_DIR)
