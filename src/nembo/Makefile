# Project Name
TARGET = nembo

# Sources
CPP_SOURCES = \
nembo.cpp \
dsp/correlator.cpp \
dsp/granular_processor.cpp \
dsp/pvoc/frame_transformation.cpp \
dsp/pvoc/phase_vocoder.cpp \
dsp/pvoc/stft.cpp \
resources.cpp

C_INCLUDES += -I. -Idsp -Idsp/pvoc -Idsp/fx

# Library Locations
LIBDAISY_DIR = ../../lib/libDaisy
DAISYSP_DIR = ../../lib/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
