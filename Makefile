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
SOURCES := src/base/status.c src/gpu/runtime.c
OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(SOURCES))
TEST_OBJECT := $(BUILD)/tests/test_backend.o
DIAG_TEST_OBJECT := $(BUILD)/tests/test_diagnostics.o
DIAG_OBJECT := $(BUILD)/src/ipc/diagnostics.o
DIAG_LIBRARY := $(BUILD)/libmetalvk-diagnostics.a
LIBRARY := $(BUILD)/libmetalvk.a

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
EXE := .exe
mkdir_parent = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
RUN_TEST = $(subst /,\,$(BUILD)/test-backend$(EXE))
RUN_DIAG_TEST = $(subst /,\,$(BUILD)/test-diagnostics$(EXE))
PLUGIN_EXT := .dll
PLUGIN_LINK_FLAGS :=
PLUGIN_FLAGS := -shared
RUN_PLUGIN_TEST = $(subst /,\,$(BUILD)/test-plugin$(EXE))
remove_temporary = if exist "$(subst /,\,$@).tmp" del "$(subst /,\,$@).tmp"
replace_archive = move /Y "$(subst /,\,$@).tmp" "$(subst /,\,$@)" >nul
else
mkdir_parent = mkdir -p "$(@D)"
RUN_TEST = ./$(BUILD)/test-backend
RUN_DIAG_TEST = ./$(BUILD)/test-diagnostics
TEST_THREAD_FLAGS := -pthread
PLUGIN_EXT := .so
PLUGIN_LINK_FLAGS := -ldl
PLUGIN_FLAGS := -shared -fPIC
RUN_PLUGIN_TEST = ./$(BUILD)/test-plugin
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

$(BUILD)/test-backend$(EXE): $(TEST_OBJECT) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_THREAD_FLAGS) $(TEST_OBJECT) $(LIBRARY) -o "$@"

$(DIAG_LIBRARY): $(DIAG_OBJECT)
	@$(remove_temporary)
	$(AR) rcs "$@.tmp" $(DIAG_OBJECT)
	@$(replace_archive)

$(BUILD)/test-diagnostics$(EXE): $(DIAG_TEST_OBJECT) $(DIAG_LIBRARY) $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(DIAG_TEST_OBJECT) $(DIAG_LIBRARY) $(LIBRARY) -o "$@"

.PHONY: test-legacy
test-legacy: $(BUILD)/tests/test_legacy_backend.o $(BUILD)/src/gpu/backend.o $(BUILD)/src/base/status.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o "$(BUILD)/test-legacy$(EXE)"
ifeq ($(OS),Windows_NT)
	$(subst /,\,$(BUILD)/test-legacy$(EXE))
else
	./$(BUILD)/test-legacy
endif

test: $(BUILD)/test-backend$(EXE) $(BUILD)/test-diagnostics$(EXE)
	$(RUN_TEST)
	$(RUN_DIAG_TEST)

$(BUILD)/provider-one$(PLUGIN_EXT): tests/provider_plugin.c include/gpu/runtime.h
	@$(mkdir_parent)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(PLUGIN_FLAGS) -DPROVIDER_NUMBER=1 "$<" -o "$@"

$(BUILD)/provider-two$(PLUGIN_EXT): tests/provider_plugin.c include/gpu/runtime.h
	@$(mkdir_parent)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(PLUGIN_FLAGS) -DPROVIDER_NUMBER=2 "$<" -o "$@"

$(BUILD)/test-plugin$(EXE): $(BUILD)/tests/test_plugin.o $(LIBRARY)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_THREAD_FLAGS) $^ $(PLUGIN_LINK_FLAGS) -o "$@"

.PHONY: test-plugin
test-plugin: $(BUILD)/provider-one$(PLUGIN_EXT) $(BUILD)/provider-two$(PLUGIN_EXT) $(BUILD)/test-plugin$(EXE)
	$(RUN_PLUGIN_TEST) "$(BUILD)/provider-one$(PLUGIN_EXT)" "$(BUILD)/provider-two$(PLUGIN_EXT)"

clean:
ifeq ($(OS),Windows_NT)
	@if exist "target" rmdir /S /Q "target"
else
	rm -rf target
endif

-include $(OBJECTS:.o=.d) $(TEST_OBJECT:.o=.d) $(DIAG_TEST_OBJECT:.o=.d) $(DIAG_OBJECT:.o=.d)
-include $(BUILD)/tests/test_plugin.d $(BUILD)/tests/test_legacy_backend.d $(BUILD)/src/gpu/backend.d
