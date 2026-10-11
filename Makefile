# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 KaguyaRing

# Vendor-neutral base layer. No vendor backend, GPU driver, kernel or Vulkan
# code belongs in the default library.
.DEFAULT_GOAL := all
CONFIG ?= Debug
CC = clang
CXX = clang++
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
SOURCES := src/base/status.c src/memory/budget.c src/runtime/work.c src/runtime/time.c src/display/validation.c src/display/control.c src/display/edid.c src/display/cta.c src/display/displayid.c src/display/edid-capabilities.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))
DIAG_OBJECT := $(BUILD)/src/ipc/diagnostics.o
DIAG_TEST_OBJECT := $(BUILD)/tests/test_diagnostics.o
MEMORY_TEST_OBJECT := $(BUILD)/tests/test_memory_budget.o
MEMORY_THREAD_OBJECT := $(BUILD)/tests/test_memory_threads.o
WORK_TEST_OBJECT := $(BUILD)/tests/test_work_queue.o
WORK_THREAD_OBJECT := $(BUILD)/tests/test_work_threads.o
DISPLAY_TEST_OBJECT := $(BUILD)/tests/test_display.o
DISPLAY_THREAD_OBJECT := $(BUILD)/tests/test_display_threads.o
EDID_TEST_OBJECT := $(BUILD)/tests/test_edid.o
EDID_TOOL_OBJECT := $(BUILD)/tools/edid-inspect.o
DIAG_LIBRARY := $(BUILD)/libmetalvk-diagnostics.a
LIBRARY := $(BUILD)/libmetalvk.a

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXE := .exe
mkdir_parent = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
RUN_DIAG_TEST = $(subst /,\,$(BUILD)/test-diagnostics$(EXE))
RUN_MEMORY_TEST = $(subst /,\,$(BUILD)/test-memory-budget$(EXE))
RUN_MEMORY_THREADS = $(subst /,\,$(BUILD)/test-memory-threads$(EXE))
RUN_WORK_TEST = $(subst /,\,$(BUILD)/test-work-queue$(EXE))
RUN_WORK_THREADS = $(subst /,\,$(BUILD)/test-work-threads$(EXE))
RUN_DISPLAY_TEST = $(subst /,\,$(BUILD)/test-display$(EXE))
RUN_DISPLAY_THREADS = $(subst /,\,$(BUILD)/test-display-threads$(EXE))
RUN_EDID_TEST = $(subst /,\,$(BUILD)/test-edid$(EXE))
THREAD_FLAGS :=
remove_temporary = if exist "$(subst /,\,$@).tmp" del "$(subst /,\,$@).tmp"
replace_archive = move /Y "$(subst /,\,$@).tmp" "$(subst /,\,$@)" >nul
else
mkdir_parent = mkdir -p "$(@D)"
RUN_DIAG_TEST = ./$(BUILD)/test-diagnostics
RUN_MEMORY_TEST = ./$(BUILD)/test-memory-budget
RUN_MEMORY_THREADS = ./$(BUILD)/test-memory-threads
RUN_WORK_TEST = ./$(BUILD)/test-work-queue
RUN_WORK_THREADS = ./$(BUILD)/test-work-threads
RUN_DISPLAY_TEST = ./$(BUILD)/test-display
RUN_DISPLAY_THREADS = ./$(BUILD)/test-display-threads
RUN_EDID_TEST = ./$(BUILD)/test-edid
THREAD_FLAGS := -pthread
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

$(BUILD)/test-memory-budget$(EXE): $(MEMORY_TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(MEMORY_TEST_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/tests/%.o: tests/%.cpp
	@$(mkdir_parent)
	$(CXX) $(CPPFLAGS) $(filter-out -std=c11,$(CFLAGS)) -std=c++17 $(THREAD_FLAGS) -MMD -MP -c "$<" -o "$@"

$(BUILD)/test-memory-threads$(EXE): $(MEMORY_THREAD_OBJECT) $(LIBRARY)
	$(CXX) $(filter-out -std=c11,$(CFLAGS)) -std=c++17 $(THREAD_FLAGS) $(LDFLAGS) $(MEMORY_THREAD_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-work-queue$(EXE): $(WORK_TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(WORK_TEST_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-work-threads$(EXE): $(WORK_THREAD_OBJECT) $(LIBRARY)
	$(CXX) $(filter-out -std=c11,$(CFLAGS)) -std=c++17 $(THREAD_FLAGS) $(LDFLAGS) $(WORK_THREAD_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-display$(EXE): $(DISPLAY_TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DISPLAY_TEST_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-display-threads$(EXE): $(DISPLAY_THREAD_OBJECT) $(LIBRARY)
	$(CXX) $(filter-out -std=c11,$(CFLAGS)) -std=c++17 $(THREAD_FLAGS) $(LDFLAGS) $(DISPLAY_THREAD_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/test-edid$(EXE): $(EDID_TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EDID_TEST_OBJECT) $(LIBRARY) -o "$@"

$(BUILD)/edid-inspect$(EXE): $(EDID_TOOL_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(EDID_TOOL_OBJECT) $(LIBRARY) -o "$@"

test: $(BUILD)/test-diagnostics$(EXE) $(BUILD)/test-memory-budget$(EXE) $(BUILD)/test-memory-threads$(EXE) $(BUILD)/test-work-queue$(EXE) $(BUILD)/test-work-threads$(EXE) $(BUILD)/test-display$(EXE) $(BUILD)/test-display-threads$(EXE) $(BUILD)/test-edid$(EXE) $(BUILD)/edid-inspect$(EXE)
	$(RUN_DIAG_TEST)
	$(RUN_MEMORY_TEST)
	$(RUN_MEMORY_THREADS)
	$(RUN_WORK_TEST)
	$(RUN_WORK_THREADS)
	$(RUN_DISPLAY_TEST)
	$(RUN_DISPLAY_THREADS)
	$(RUN_EDID_TEST)

clean:
ifeq ($(OS),Windows_NT)
	@if exist "target" rmdir /S /Q "target"
else
	rm -rf target
endif

-include $(OBJECTS:.o=.d) $(DIAG_OBJECT:.o=.d) $(DIAG_TEST_OBJECT:.o=.d) $(MEMORY_TEST_OBJECT:.o=.d) $(MEMORY_THREAD_OBJECT:.o=.d) $(WORK_TEST_OBJECT:.o=.d) $(WORK_THREAD_OBJECT:.o=.d) $(DISPLAY_TEST_OBJECT:.o=.d) $(DISPLAY_THREAD_OBJECT:.o=.d) $(EDID_TEST_OBJECT:.o=.d) $(EDID_TOOL_OBJECT:.o=.d)
