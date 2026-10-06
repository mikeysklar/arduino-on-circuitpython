# SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
# SPDX-License-Identifier: MIT
#
# C++ rules for an Arduino sketch native module, included after
# $(MPY_DIR)/py/dynruntime.mk by the Makefile ardbuild.py generates.
# Expects CORE (this directory) and SKETCH_DIR.

CXX = $(CROSS)g++
OBJCOPY = $(CROSS)objcopy

# Sketches are not warning-clean C99; drop the C-only and -Werror flags.
CXXFLAGS = $(filter-out -std=c99 -Werror,$(CFLAGS)) -std=gnu++17
CXXFLAGS += -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
# mpy_ld rejects static variables in .bss; keep everything in .data instead.
CXXFLAGS += -fno-zero-initialized-in-bss
CXXFLAGS += -I$(CORE) -I$(SKETCH_DIR)

# mpy_ld only keeps .text, .rodata, .data.rel.ro and .bss. Native code is
# loaded into RAM, so moving writable data into .data.rel.ro keeps it writable.
# It also drops .init_array, so expose the constructor function for ard_mp.c.
# objcopy globalizes after renaming, so --globalize-symbol names the new symbol.
OBJCOPY_FLAGS = --redefine-sym _GLOBAL__sub_I_sketch_all.cpp=ard_ctors --globalize-symbol=ard_ctors
OBJCOPY_FLAGS += --rename-section .data=.data.rel.ro.ard_data
OBJCOPY_FLAGS += --rename-section .data.rel=.data.rel.ro.ard_data_rel
OBJCOPY_FLAGS += --rename-section .data.rel.local=.data.rel.ro.ard_data_local

$(BUILD)/$(MOD).mpy: $(BUILD)/sketch_all.o

$(BUILD)/sketch_all.o: sketch_all.cpp $(CONFIG_H) Makefile $(wildcard $(CORE)/*.h $(CORE)/*.cpp)
	$(ECHO) "CXX $<"
	$(Q)$(CXX) $(CXXFLAGS) -o $@ -c $<
	$(Q)$(OBJCOPY) $(OBJCOPY_FLAGS) $@
