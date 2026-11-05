# Project Name
TARGET = grani

# Beads and stmlib come from the lib/eurorack submodule, copied to
# build/upstream with grani.patch applied.
EURORACK = ../../lib/eurorack
UPSTREAM = build/upstream

# Sources
CPP_SOURCES = grani.cpp tables.cpp
CC_SOURCES = $(addprefix $(UPSTREAM)/, \
beads/dsp/delay_clock.cc \
beads/dsp/delay_controller.cc \
beads/dsp/processor.cc \
beads/dsp/trigger_generator.cc \
beads/gain_control.cc \
stmlib/utils/random.cc)

# shim/ first: its codec.h replaces Beads' STM32F4 codec driver.
C_INCLUDES += -Ishim -I$(UPSTREAM) -I../../lib

# Beads' DSP is too large for the 128 KB flash at -O2.
OPT = -Os -flto
LDFLAGS += -flto -Os

ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(shell test $(UPSTREAM)/.patched -nt grani.patch || \
  (rm -rf $(UPSTREAM) && mkdir -p $(UPSTREAM) && \
   cp -R $(EURORACK)/beads $(EURORACK)/stmlib $(UPSTREAM)/ && \
   patch -s -p1 -d $(UPSTREAM) < grani.patch && \
   touch $(UPSTREAM)/.patched) >&2)
ifeq ($(wildcard $(UPSTREAM)/.patched),)
$(error Could not patch lib/eurorack, run: git submodule update --init \
  --recursive)
endif
endif

# Library Locations
LIBDAISY_DIR = ../../lib/libDaisy

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# The generic Makefile only knows .cpp, Beads uses .cc.
CC_OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
OBJECTS += $(CC_OBJECTS)
vpath %.cc $(sort $(dir $(CC_SOURCES)))

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(CC_OBJECTS)

# Only prepares build/upstream, for the host tests.
upstream:
.PHONY: upstream
