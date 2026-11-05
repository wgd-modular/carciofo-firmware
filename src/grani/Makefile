# Project Name
TARGET = grani

# Sources
CPP_SOURCES = \
grani.cpp \
beads/dsp/delay_clock.cpp \
beads/dsp/delay_controller.cpp \
beads/dsp/mu_law.cpp \
beads/dsp/processor.cpp \
beads/dsp/trigger_generator.cpp \
beads/gain_control.cpp \
beads/resources.cpp \
stmlib/dsp/units.cpp \
stmlib/utils/random.cpp

C_INCLUDES += -I. -I../../lib

# Beads' DSP is too large for the 128 KB flash at -O2.
OPT = -Os -flto
LDFLAGS += -flto -Os

# Library Locations
LIBDAISY_DIR = ../../lib/libDaisy

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
