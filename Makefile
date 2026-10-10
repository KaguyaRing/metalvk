# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing

# Generic, vendor-neutral GPU library core. No vendor backend is linked here.
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
SOURCES := src/base/status.c src/gpu/backend.c src/ipc/diagnostics.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))
TEST_OBJECT := $(BUILD)/tests/test_backend.o
DIAG_TEST_OBJECT := $(BUILD)/tests/test_diagnostics.o
LIBRARY := $(BUILD)/libmetalvk.a

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXE := .exe
mkdir_parent = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
RUN_TEST = $(subst /,\,$(BUILD)/test-backend$(EXE))
RUN_DIAG_TEST = $(subst /,\,$(BUILD)/test-diagnostics$(EXE))
else
mkdir_parent = mkdir -p "$(@D)"
RUN_TEST = ./$(BUILD)/test-backend
RUN_DIAG_TEST = ./$(BUILD)/test-diagnostics
endif

.PHONY: all test clean
all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	$(AR) rcs "$@" $(OBJECTS)

$(BUILD)/%.o: %.c
	@$(mkdir_parent)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/test-backend$(EXE): $(TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-diagnostics$(EXE): $(DIAG_TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DIAG_TEST_OBJECT) $(LIBRARY) -o "$@"

test: $(BUILD)/test-backend$(EXE) $(BUILD)/test-diagnostics$(EXE)
	$(RUN_TEST)
	$(RUN_DIAG_TEST)

clean:
ifeq ($(OS),Windows_NT)
	@if exist "target" rmdir /S /Q "target"
else
	rm -rf target
endif

-include $(OBJECTS:.o=.d) $(TEST_OBJECT:.o=.d) $(DIAG_TEST_OBJECT:.o=.d)
