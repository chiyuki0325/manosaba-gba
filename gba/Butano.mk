# Butano 21.10.0; override BUTANO to use a separately installed checkout.
MANOSABA_GBA_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
DEBUG_MENU ?= 1
SAVE_BACKEND ?= flash
ifeq ($(filter $(SAVE_BACKEND),flash sram),)
$(error SAVE_BACKEND must be flash or sram)
endif
ifeq ($(filter $(DEBUG_MENU),0 1),)
$(error DEBUG_MENU must be 0 or 1)
endif
TARGET := $(if $(filter 0,$(DEBUG_MENU)),manosaba-release,manosaba)$(if $(filter sram,$(SAVE_BACKEND)),-sram)
BUILD := $(if $(filter sram,$(SAVE_BACKEND)),build-sram$(if $(filter 0,$(DEBUG_MENU)),-release),$(if $(filter 0,$(DEBUG_MENU)),build-release,build-butano))
LIBBUTANO := $(BUTANO)
PYTHON ?= python3
SOURCES := src
INCLUDES := src
DATA :=
GRAPHICS :=
AUDIO := audio audio/sfx
AUDIOBACKEND := maxmod
AUDIOTOOL ?= $(PYTHON) $(MANOSABA_GBA_ROOT)/tools/mmutil_compact.py
DMGAUDIO :=
DMGAUDIOBACKEND := null
ROMTITLE := MANOSABA
ROMCODE := MSBA
USERFLAGS := -DMM_MMUTIL_DEVKITPRO_COMPAT -DBN_CFG_AUDIO_MAX_MUSIC_CHANNELS=16 -DBN_CFG_AUDIO_MAX_SOUND_CHANNELS=2
USERFLAGS += -DMANOSABA_DEBUG_MENU=$(DEBUG_MENU)
USERFLAGS += -DMANOSABA_SAVE_SRAM=$(if $(filter sram,$(SAVE_BACKEND)),1,0)
USERCXXFLAGS := -iquote $(abspath $(BUTANO))/hw/3rd_party/maxmod/src/core -iquote $(abspath $(BUTANO))/hw/include
USERASFLAGS :=
# Story portraits use ordinary OBJ at 1x; host budget scans can still compare scale policies.
USERFLAGS += -DMANOSABA_ACTOR_SCALE_SNAP -DMANOSABA_ACTOR_SCALE_SNAP_115
USERLDFLAGS :=
USERLIBDIRS :=
USERLIBS :=
DEFAULTLIBS :=
STACKTRACE :=
USERBUILD :=
EXTTOOL :=

# Respect an explicitly configured devkitARM or Wonderful installation.
ifeq ($(strip $(DEVKITARM)$(WONDERFUL_TOOLCHAIN)),)
ifneq ($(wildcard /opt/wonderful/target/gba/rom/makedefs.mk),)
export WONDERFUL_TOOLCHAIN := /opt/wonderful
else ifneq ($(wildcard /opt/devkitpro/devkitARM/gba_rules),)
export DEVKITARM := /opt/devkitpro/devkitARM
export DEVKITPRO := /opt/devkitpro
export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)
endif
endif

ifndef LIBBUTANOABS
export LIBBUTANOABS := $(realpath $(LIBBUTANO))
endif
ifeq ($(LIBBUTANOABS),)
$(error Butano is missing. Run make setup, or set BUTANO=/path/to/butano/butano)
endif
include $(LIBBUTANOABS)/butano.mak

# Keep the existing ARM division routine, but make room for the audio mixer.
# This project-local rule leaves the shared Butano checkout untouched.
ifeq ($(BUILD),$(notdir $(CURDIR)))
$(OFILES_SOURCES): $(MANOSABA_GBA_ROOT)/Butano.mk
# The .incbin wrapper can stay unchanged when prepared home art is replaced.
home_bytes.o: $(MANOSABA_GBA_ROOT)/assets/home/home.bin
bn_hw_sram.bn_noflto.o: $(MANOSABA_GBA_ROOT)/tools/flash_sram_init.cpp $(MANOSABA_GBA_ROOT)/Butano.mk
	$(CXX) -MMD -MP -MF $(DEPSDIR)/bn_hw_sram.bn_noflto.d $(CPPFLAGS) $(CXXFLAGS) -fno-lto -c $< -o $@
stdgba_ldiv.o: $(LIBBUTANOABS)/hw/3rd_party/stdgba/src/stdgba_ldiv.s $(MANOSABA_GBA_ROOT)/Butano.mk
	$(CC) $(ASFLAGS) -c $< -o $@
	$(OBJCOPY) --rename-section .iwram._stdgba_ldiv=.ewram._stdgba_ldiv $@

# Music sequencing and mixing stay in IWRAM; only their non-cache state moves.
MANOSABA_MAXMOD := $(LIBBUTANOABS)/hw/3rd_party/maxmod/src
define MANOSABA_AUDIO_STATE_RULE
$(1).o: $(MANOSABA_MAXMOD)/$(2)/$(1).c $(MANOSABA_GBA_ROOT)/Butano.mk $(MANOSABA_GBA_ROOT)/tools/audio_layout.py
	$$(CC) -MMD -MP -MF $$(DEPSDIR)/$(1).d $$(CPPFLAGS) $$(CFLAGS) -c $$< -o $$@
	$$(PYTHON) $(MANOSABA_GBA_ROOT)/tools/audio_layout.py $$@ --objcopy $$(OBJCOPY)
endef
$(eval $(call MANOSABA_AUDIO_STATE_RULE,mas,core))
$(eval $(call MANOSABA_AUDIO_STATE_RULE,mas_arm,core))
$(eval $(call MANOSABA_AUDIO_STATE_RULE,effect,core))
$(eval $(call MANOSABA_AUDIO_STATE_RULE,mixer,gba))
$(eval $(call MANOSABA_AUDIO_STATE_RULE,main_gba,gba))
endif
