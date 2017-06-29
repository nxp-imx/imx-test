CC		?= $(CROSS_COMPILE)gcc
STRIP		?= $(CROSS_COMPILE)strip
BASE_CFLAGS	= -Wall -Wstrict-prototypes -Wno-trigraphs -O2 \
		  -fno-strict-aliasing -fno-common -g

TARGETS :=

all: targets
	@:

ALL_OBJS :=

%.o: %.c
	@echo "	CC	$@"
	$(Q)$(CC) -c $(CFLAGS) $< -o $@


# This creates the build and copy rules for one executable. Because we
# have different source/destination directories we must customize it
# for each executable and we can't use generic build rules.
define do_build =

exec := $(1)
srcdir := $(2)
dstdir := $(3)
cflags := $(4)
ldflags := $(5)

# find out the objects we need to build the executable from

# if the target has multiple objects the user has defined the objects
ifdef $$(exec)
objs := $$($$(exec))
else
# otherwise if the target is named %.out (e.g. test.out) the object is named %.o (e.g. test.o)
ifeq ($$(filter-out %.out,$$(exec)),)
objs := $$(exec:%.out=%.o)
# otherwise (e.g. test) the object is named %.o (e.g. test.o)
else
objs := $$(exec:%=%.o)
endif
endif

# determine the full path for the objects
fp_objs := $$(foreach o,$$(objs),$$(srcdir)/$$(o))

# record what object were generated
ALL_OBJS += $$(srcdir)/$$(exec)
ALL_OBJS += $$(fp_objs)

# set CFLAGS for the objects
$$(fp_objs): CFLAGS := $$(cflags)

# link the executable
$$(srcdir)/$$(exec): LDFLAGS := $$(ldflags)
$$(srcdir)/$$(exec): $$(fp_objs)
	@echo "	LD	$$@"
	$(Q)$(CC) $$^ $$(LDFLAGS) -o $$@

# copy the executable to destdir and strip it
$$(OBJDIR)/$$(dstdir)/$$(exec): $$(srcdir)/$$(exec)
	@echo "	STRIP	$$@"
	@mkdir -p `dirname $$@`
	$(Q)cp -af $$< $$@
	$(Q)$(STRIP) $$@
endef

define srcdir_wildcard =
$(patsubst $(SRCDIR)/%,%,$(wildcard $(SRCDIR)/$(1)))
endef

# This creates rules to build and copy the targets to the output
# directory.
define do_makefile =

DIR :=
BUILD :=
COPY :=
CFLAGS :=
LDFLAGS :=
SRCDIR := $(patsubst %/Makefile,%,$(1))

include $(1)

TARGETS += $$(foreach obj,$$(BUILD),$(OBJDIR)/$$(DIR)/$$(obj))
TARGETS += $$(foreach obj,$$(COPY),$(OBJDIR)/$$(DIR)/$$(obj))

# copy files to output directory
$$(OBJDIR)/$$(DIR)/%: $$(SRCDIR)/%
	@echo "	COPY 	$$@"
	@mkdir -p `dirname $$@`
	$(Q)cp -af $$< $$@

# build is more complex and we need a separate function for it
$$(foreach o,$$(BUILD),$$(eval $$(call do_build,$$(o),$$(SRCDIR),$$(DIR),$$(BASE_CFLAGS) $$(CFLAGS),$$(LDFLAGS))))

endef

# include Makefiles from all subdirs and learn what we need to build
# and copy
makefiles := $(foreach dir, $(wildcard *), $(wildcard $(dir)/Makefile))
$(foreach m,$(makefiles),$(eval $(call do_makefile,$(m))))

.PHONY: all clean
.PHONY: $(DIRS)

# these needs to be at the end, after we have collected all targets
targets: $(TARGETS)
	@:
clean:
	@echo "	CLEAN	test"
	$(Q)rm -f $(TARGETS) $(ALL_OBJS)
