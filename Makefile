# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing

# Vendor-neutral base layer. No vendor backend, GPU driver, kernel or Vulkan
# code belongs in the default library.
.DEFAULT_GOAL := all
CONFIG ?= Debug
CC = clang
AR = ar
CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow

ifeq ($(CONFIG),Release)
CFLAGS += -O2
else ifeq ($(CONFIG),Debug)
CFLAGS += -O0 -g
else ifeq ($(CONFIG),Alpha)
CFLAGS += -O1 -g -fsanitize=undefined -fno-sanitize-recover=all
LDFLAGS += -fsanitize=undefined
else
$(error CONFIG must be Release, Debug or Alpha)
endif

BUILD := target/$(CONFIG)
SOURCES := src/base/status.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))
DIAG_OBJECT := $(BUILD)/src/ipc/diagnostics.o
DIAG_TEST_OBJECT := $(BUILD)/tests/test_diagnostics.o
DIAG_LIBRARY := $(BUILD)/libmetalvk-diagnostics.a
LIBRARY := $(BUILD)/libmetalvk.a

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXE := .exe
mkdir_parent = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
RUN_DIAG_TEST = $(subst /,\,$(BUILD)/test-diagnostics$(EXE))
remove_temporary = if exist "$(subst /,\,$@).tmp" del "$(subst /,\,$@).tmp"
replace_archive = move /Y "$(subst /,\,$@).tmp" "$(subst /,\,$@)" >nul
else
mkdir_parent = mkdir -p "$(@D)"
RUN_DIAG_TEST = ./$(BUILD)/test-diagnostics
remove_temporary = rm -f "$@.tmp"
replace_archive = mv -f "$@.tmp" "$@"
endif

.PHONY: all test clean
all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	@$(remove_temporary)
	$(AR) rcs "$@.tmp" $(OBJECTS)
	@$(replace_archive)

$(BUILD)/%.o: %.c
	@$(mkdir_parent)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(DIAG_LIBRARY): $(DIAG_OBJECT)
	@$(remove_temporary)
	$(AR) rcs "$@.tmp" $(DIAG_OBJECT)
	@$(replace_archive)

$(BUILD)/test-diagnostics$(EXE): $(DIAG_TEST_OBJECT) $(DIAG_LIBRARY) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DIAG_TEST_OBJECT) $(DIAG_LIBRARY) $(LIBRARY) -o "$@"

test: $(BUILD)/test-diagnostics$(EXE)
	$(RUN_DIAG_TEST)

clean:
ifeq ($(OS),Windows_NT)
	@if exist "target" rmdir /S /Q "target"
else
	rm -rf target
endif

-include $(OBJECTS:.o=.d) $(DIAG_OBJECT:.o=.d) $(DIAG_TEST_OBJECT:.o=.d)
