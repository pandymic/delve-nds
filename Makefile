# Delve - Nintendo DS roguelike
#
# Based on the BlocksDS rom_arm9_only template (CC0-1.0,
# $(BLOCKSDS)/templates/rom_arm9_only/Makefile) plus the Nitro Engine
# 3d_single_screen template for library flags.
#
# Targets:
#   make            build delve.nds (the prod build)
#   make debug      build delve-debug.nds (debug cheats and meters; same as
#                   make DEBUG=1 or make BUILD=debug)
#   make release    build delve-v<VERSION>.nds, the file for releases: runs
#                   the host tests first, treats warnings as errors and
#                   refuses uncommitted changes (ALLOW_DIRTY=1 overrides)
#   make dldipatch  patch the ROM with the R4 DLDI driver, for carts whose
#                   loader does not patch homebrew itself
#   make test       build and run host-side unit tests with plain gcc
#   make clean
#
# Each build type keeps its own objects (build/<rom name>/), so switching
# between them never needs a clean.

export BLOCKSDS		?= /opt/blocksds/core
export BLOCKSDSEXT	?= /opt/blocksds/external

export WONDERFUL_TOOLCHAIN	?= /opt/wonderful
ARM_NONE_EABI_PATH	?= $(WONDERFUL_TOOLCHAIN)/toolchain/gcc-arm-none-eabi/bin/

# User config

NAME		:= delve

# The version lives in the VERSION file and nowhere else. It reaches the
# code as DELVE_VERSION_STRING (About, the Easter egg, the crash screen,
# Rogue's intro), the ROM banner and the release ROM's name.
VERSION		:= $(strip $(shell cat VERSION 2>/dev/null))
ifeq ($(shell printf '%s' '$(VERSION)' | grep -Ex '[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?'),)
    $(error VERSION must hold MAJOR.MINOR.PATCH (optionally -suffix), found "$(VERSION)")
endif

GAME_TITLE	:= Delve
GAME_SUBTITLE	:= An endless descent
GAME_AUTHOR	:= Michal Pandyra
GAME_ICON	:= icon.gif

COMPDB		:= 0

SOURCEDIRS	:= source
INCLUDEDIRS	:= source
GFXDIRS		:= gfx
BINDIRS		:= data
AUDIODIRS	:= audio
NITROFSDIR	:=

# The commit, with "-dirty" when the tree has uncommitted changes.
GIT_HASH	:= $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)$(shell git diff --quiet HEAD -- 2>/dev/null || echo -dirty)

DEFINES		:= -DDELVE_GIT_HASH=\"$(GIT_HASH)\" \
		   -DDELVE_BLOCKSDS_VERSION=\"$(shell cat $(BLOCKSDS)/version.txt 2>/dev/null || echo unknown)\"

# Build types (BUILD=...):
#   prod     delve.nds             the everyday build (default)
#   debug    delve-debug.nds       debug cheats and meters (see README.md)
#   release  delve-v<VERSION>.nds  for releases; warnings are errors
# The game itself is the same in prod and release; only debug differs.
ifeq ($(DEBUG),1)
    BUILD	?= debug
endif
BUILD		?= prod

ifeq ($(BUILD),debug)
    DEFINES	+= -DDELVE_DEBUG
    OUTNAME	:= $(NAME)-debug
    BANNER_TAG	:= (debug)
else ifeq ($(BUILD),prod)
    OUTNAME	:= $(NAME)
else ifeq ($(BUILD),release)
    OUTNAME	:= $(NAME)-v$(VERSION)
    WERROR	:= -Werror
else
    $(error BUILD must be debug, prod or release, not "$(BUILD)")
endif

DEFINES		+= -DDELVE_VERSION_STRING=\"$(VERSION)\" -DDELVE_BUILD_TYPE=\"$(BUILD)\"

# Libraries

ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_maxmod.elf

LIBS		:= -lNE -lmm9 -lnds9
LIBDIRS		:= $(BLOCKSDSEXT)/nitro-engine \
		   $(BLOCKSDS)/libs/maxmod \
		   $(BLOCKSDS)/libs/libnds

# Build artifacts

BUILDDIR	:= build/$(OUTNAME)
ELF		:= build/$(OUTNAME).elf
DUMP		:= build/$(OUTNAME).dump
MAP		:= build/$(OUTNAME).map
ROM		:= $(OUTNAME).nds

SOUNDBANKINFODIR	:= $(BUILDDIR)/maxmod
SOUNDBANKDIR		:= $(BUILDDIR)/maxmod

# Tools

PREFIX		:= $(ARM_NONE_EABI_PATH)arm-none-eabi-
CC		:= $(PREFIX)gcc
LD		:= $(PREFIX)gcc
OBJDUMP		:= $(PREFIX)objdump
MKDIR		:= mkdir
RM		:= rm -rf
HOSTCC		?= gcc

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

# Source files

SOURCES_BIN	:= $(shell find -L $(BINDIRS) -name "*.bin" 2>/dev/null)
INCLUDEDIRS	+= $(addprefix $(BUILDDIR)/,$(BINDIRS))
SOURCES_PNG	:= $(shell find -L $(GFXDIRS) -name "*.png" 2>/dev/null)
INCLUDEDIRS	+= $(addprefix $(BUILDDIR)/,$(GFXDIRS))
SOURCES_AUDIO	:= $(shell find -L $(AUDIODIRS) -regex '.*\.\(it\|mod\|s3m\|wav\|xm\)' 2>/dev/null)
ifneq ($(SOURCES_AUDIO),)
    INCLUDEDIRS	+= $(SOUNDBANKINFODIR)
endif

SOURCES_S	:= $(shell find -L $(SOURCEDIRS) -name "*.s")
SOURCES_C	:= $(shell find -L $(SOURCEDIRS) -name "*.c")

# Compiler and linker flags

DEFINES		+= -D__NDS__ -D__BLOCKSDS__ -DARM9

ARCH		:= -mthumb -mcpu=arm946e-s+nofp

SPECS		:= $(BLOCKSDS)/sys/crts/ds_arm9.specs

WARNFLAGS	:= -Wall -Wextra $(WERROR)

LIBS		+= -lc

INCLUDEFLAGS	:= $(foreach path,$(INCLUDEDIRS),-I$(path)) \
		   $(foreach path,$(LIBDIRS),-I$(path)/include)

LIBDIRSFLAGS	:= $(foreach path,$(LIBDIRS),-L$(path)/lib)

ASFLAGS		+= -x assembler-with-cpp $(INCLUDEFLAGS) $(DEFINES) \
		   $(ARCH) -ffunction-sections -fdata-sections \
		   -specs=$(SPECS)

CFLAGS		+= -std=gnu11 $(WARNFLAGS) $(INCLUDEFLAGS) $(DEFINES) \
		   $(ARCH) -O2 -ffunction-sections -fdata-sections \
		   -specs=$(SPECS)

# The toolchain crtn.o lacks .note.GNU-stack; irrelevant on bare metal.
LDFLAGS		+= $(ARCH) $(LIBDIRSFLAGS) -Wl,-Map,$(MAP) -Wl,--no-warn-execstack $(DEFINES) \
		   -Wl,--start-group $(LIBS) -Wl,--end-group -specs=$(SPECS)

# Intermediate build files

OBJS_ASSETS	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_BIN))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_PNG)))

HEADERS_ASSETS	:= $(patsubst %.bin,%_bin.h,$(addprefix $(BUILDDIR)/,$(SOURCES_BIN))) \
		   $(patsubst %.png,%.h,$(addprefix $(BUILDDIR)/,$(SOURCES_PNG)))

ifneq ($(SOURCES_AUDIO),)
    OBJS_ASSETS		+= $(SOUNDBANKDIR)/soundbank.c.o
    # soundbank_bin.h (audio.c) comes from the soundbank.c.o rule's bin2c:
    # listing it keeps a parallel build from compiling audio.c first.
    HEADERS_ASSETS	+= $(SOUNDBANKINFODIR)/soundbank.h $(SOUNDBANKDIR)/soundbank_bin.h
endif

OBJS_SOURCES	:= $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_S))) \
		   $(addsuffix .o,$(addprefix $(BUILDDIR)/,$(SOURCES_C)))

OBJS		:= $(OBJS_ASSETS) $(OBJS_SOURCES)

DEPS		:= $(OBJS:.o=.d)

# Targets

.PHONY: all clean dump dldipatch test debug prod release

all: $(ROM)

debug:
	$(V)$(MAKE) --no-print-directory BUILD=debug

prod:
	$(V)$(MAKE) --no-print-directory BUILD=prod

# A release ROM is only built from committed code, after the tests pass.
release:
	$(V)if ! git diff --quiet HEAD -- 2>/dev/null && [ "$(ALLOW_DIRTY)" != 1 ]; then \
		echo "release: uncommitted changes; commit them first (or ALLOW_DIRTY=1)" >&2; \
		exit 1; \
	fi
	$(V)$(MAKE) --no-print-directory test
	$(V)$(MAKE) --no-print-directory BUILD=release

# The banner (the loader's menu): "Delve v<VERSION>", the subtitle, the author.
GAME_FULL_TITLE := $(strip $(GAME_TITLE) v$(VERSION) $(BANNER_TAG));$(GAME_SUBTITLE);$(GAME_AUTHOR)

$(ROM): $(ELF) VERSION
	@echo "  NDSTOOL $@"
	$(V)$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 $(ARM7ELF) -9 $(ELF) \
		-b $(GAME_ICON) "$(GAME_FULL_TITLE)"

$(ELF): $(OBJS)
	@echo "  LD      $@"
	$(V)$(LD) -o $@ $(OBJS) $(LDFLAGS)

$(DUMP): $(ELF)
	@echo "  OBJDUMP $@"
	$(V)$(OBJDUMP) -h -C -S $< > $@

dump: $(DUMP)

clean:
	@echo "  CLEAN"
	$(V)$(RM) $(NAME).nds $(NAME)-debug.nds $(NAME)-v*.nds build tests/bin compile_commands.json

# The ROM carries a blank DLDI stub that flashcart loaders patch with their
# own driver when they start it. For a cart whose loader does not, this
# patches in the R4 (original R4DS / M3 Simply) driver shipped with BlocksDS.
dldipatch: $(ROM)
	@echo "  DLDIPATCH $(ROM)"
	$(V)$(BLOCKSDS)/tools/dldipatch/dldipatch patch \
		$(BLOCKSDS)/sys/dldi_r4/r4tf.dldi $(ROM)

# Host unit tests
# Only platform-independent modules (no libnds includes) are listed, in
# tests/host_sources.txt.

HOST_SOURCES	:= $(shell sed -n 's/^HOST: *//p' tests/host_sources.txt)
HOST_TESTS	:= $(filter-out tests/test_main.c,$(wildcard tests/test_*.c))
HOST_CFLAGS	:= -std=c11 -Wall -Wextra -Werror -O1 -g -Isource -DDELVE_HOST \
		   -DDELVE_VERSION_STRING=\"$(VERSION)\" -DDELVE_BUILD_TYPE=\"host\"

test: tests/bin/run_tests
	@./tests/bin/run_tests

tests/bin/run_tests: $(HOST_TESTS) $(HOST_SOURCES) tests/test_main.c tests/test.h $(wildcard source/*.h) VERSION
	@$(MKDIR) -p tests/bin
	@echo "  HOSTCC  $@"
	$(V)$(HOSTCC) $(HOST_CFLAGS) -o $@ tests/test_main.c $(HOST_TESTS) $(HOST_SOURCES)


# Rules

$(BUILDDIR)/%.s.o : %.s
	@echo "  AS      $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(ASFLAGS) -MMD -MP -c -o $@ $<

# The crash screen shows the commit hash: rebuild it when HEAD moves.
$(BUILDDIR)/source/crash.c.o: $(wildcard .git/HEAD .git/index)

# Every object follows VERSION, so a bump reaches everything that shows it.
$(BUILDDIR)/%.c.o : %.c VERSION
	@echo "  CC      $<"
	@$(MKDIR) -p $(@D)
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILDDIR)/%.bin.o $(BUILDDIR)/%_bin.h : %.bin
	@echo "  BIN2C   $<"
	@$(MKDIR) -p $(@D)
	$(V)$(BLOCKSDS)/tools/bin2c/bin2c $< $(@D)
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $(BUILDDIR)/$*.bin.o $(BUILDDIR)/$*_bin.c

$(BUILDDIR)/%.png.o $(BUILDDIR)/%.h : %.png %.grit
	@echo "  GRIT    $<"
	@$(MKDIR) -p $(@D)
	$(V)$(BLOCKSDS)/tools/grit/grit $< -ftc -W1 -o$(BUILDDIR)/$*
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $(BUILDDIR)/$*.png.o $(BUILDDIR)/$*.c
	$(V)touch $(BUILDDIR)/$*.png.o $(BUILDDIR)/$*.h

ifneq ($(SOURCES_AUDIO),)

$(SOUNDBANKINFODIR)/soundbank.h: $(SOURCES_AUDIO)
	@echo "  MMUTIL  $^"
	@$(MKDIR) -p $(SOUNDBANKDIR)
	@$(MKDIR) -p $(SOUNDBANKINFODIR)
	$(V)$(BLOCKSDS)/tools/mmutil/mmutil $^ -d \
		-o$(SOUNDBANKDIR)/soundbank.bin -h$(SOUNDBANKINFODIR)/soundbank.h

$(SOUNDBANKDIR)/soundbank_bin.h: $(SOUNDBANKDIR)/soundbank.c.o

$(SOUNDBANKDIR)/soundbank.c.o: $(SOUNDBANKINFODIR)/soundbank.h
	@echo "  BIN2C   soundbank.bin"
	$(V)$(BLOCKSDS)/tools/bin2c/bin2c $(SOUNDBANKDIR)/soundbank.bin \
		$(SOUNDBANKDIR)
	@echo "  CC      soundbank_bin.c"
	$(V)$(CC) $(CFLAGS) -MMD -MP -c -o $(SOUNDBANKDIR)/soundbank.c.o \
		$(SOUNDBANKDIR)/soundbank_bin.c

endif

# All assets must be built before the source code
$(SOURCES_S) $(SOURCES_C): $(HEADERS_ASSETS)

-include $(DEPS)
