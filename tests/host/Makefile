# ---------------------------------------------------------------------------
# Host test Makefile for the MC75 HomeBox client
#
# Builds and runs the platform-independent core logic (JsonLite, Item,
# Location, Config, Journal, HttpClient, HbClient) against a small Win32/CE
# shim (tests/host/shim) using the native host compiler, so unit and
# integration tests can run in CI without the Windows Mobile SDK or a device.
#
# Usage (from anywhere):
#     make -C tests/host          # build the test runner
#     make -C tests/host run      # build + run the tests
#     make -C tests/host clean
# ---------------------------------------------------------------------------

CXX ?= g++

# Repository root, derived from this Makefile's location (tests/host/Makefile).
ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SHIM  := $(ROOT)/tests/host/shim
INC   := $(ROOT)/include
HOST  := $(ROOT)/tests/host
BUILD := $(ROOT)/build/host-tests
BIN   := $(BUILD)/hbx_host_tests

# -std=c++03 keeps the harness faithful to the Visual Studio 2008 / pre-C++11
# target. The shim include dir is searched first so <windows.h>, <winsock.h>
# and <wchar.h> resolve to the shims instead of any real system header.
CXXFLAGS := -std=c++03 -D_GNU_SOURCE \
            -I$(SHIM) -I$(INC) -I$(HOST) \
            -Wall -Wextra -g -O0 -pthread \
            -Wno-deprecated-declarations -Wno-sign-compare -Wno-unused-parameter

# Platform-independent production sources exercised by the tests.
#
# SyncEngine belongs here even though it talks to HbClient: its queue/replay
# logic is the part of the app most likely to break silently, and leaving it
# out meant the entire offline pipeline was compile-checked but never run.
CORE_SRC := \
    $(ROOT)/src/StrUtil.cpp \
    $(ROOT)/src/Models/JsonLite.cpp \
    $(ROOT)/src/Models/Item.cpp \
    $(ROOT)/src/Models/Location.cpp \
    $(ROOT)/src/Models/AssetSummary.cpp \
    $(ROOT)/src/Config.cpp \
    $(ROOT)/src/Journal.cpp \
    $(ROOT)/src/HttpClient.cpp \
    $(ROOT)/src/HbClient.cpp \
    $(ROOT)/src/SyncEngine.cpp

# Every production source, including the GUI / scanner / controller layer.
# These are syntax-checked (not run) against the shim as a full-codebase build
# gate: if any translation unit stops compiling, this catches it on any host.
ALL_SRC := $(wildcard $(ROOT)/src/*.cpp $(ROOT)/src/Models/*.cpp $(ROOT)/src/Views/*.cpp)

# The GUI layer triggers a couple of warnings that only exist in the 64-bit
# host build (e.g. casting a 32-bit control id to a 64-bit HMENU pointer); they
# do not apply to the 32-bit ARMV4I target, so silence them for the gate only.
GATE_FLAGS := $(CXXFLAGS) -Wno-int-to-pointer-cast -Wno-missing-field-initializers \
              -Wno-write-strings -Wno-unused-value -fsyntax-only

# The device build enables real Zebra EMDK scanning and the WinInet HTTP/HTTPS
# transport. These paths are Windows-Mobile-only, but we syntax-check them here
# against the shim headers so the device code cannot silently rot.
DEVICE_GATE_FLAGS := $(GATE_FLAGS) -DHBX_USE_EMDK -DHBX_USE_WININET

# Test sources + runner entry point.
TEST_SRC := \
    $(ROOT)/tests/unit/test_strutil.cpp \
    $(ROOT)/tests/unit/test_json.cpp \
    $(ROOT)/tests/unit/test_journal.cpp \
    $(ROOT)/tests/unit/test_http.cpp \
    $(ROOT)/tests/integration/test_api_endpoints.cpp \
    $(ROOT)/tests/integration/test_hb_backend.cpp \
    $(ROOT)/tests/integration/test_offline_sync.cpp \
    $(ROOT)/tests/host/test_main.cpp

SOURCES := $(CORE_SRC) $(TEST_SRC)

.PHONY: all run compile-all compile-device check clean

all: $(BIN)

$(BIN): $(SOURCES) $(SHIM)/windows.h $(SHIM)/winsock.h $(SHIM)/wchar.h $(SHIM)/commctrl.h $(HOST)/test_framework.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(BIN)

# Full-codebase build gate: syntax-check every production source (including the
# GUI layer that cannot be linked/run on a host) against the shim.
compile-all:
	@echo "Compile-checking all production sources against the host shim..."
	@rc=0; for f in $(ALL_SRC); do \
		if $(CXX) $(GATE_FLAGS) "$$f" ; then \
			echo "  OK   $$f" ; \
		else \
			echo "  FAIL $$f" ; rc=1 ; \
		fi ; \
	done ; \
	if [ $$rc -eq 0 ]; then echo "All sources compile cleanly."; else echo "One or more sources failed to compile."; fi ; \
	exit $$rc

# Same as compile-all, but with the device features (EMDK scanning + WinInet
# HTTPS) turned on, so the Windows-Mobile-only code paths are compile-checked.
compile-device:
	@echo "Compile-checking device paths (HBX_USE_EMDK + HBX_USE_WININET)..."
	@rc=0; for f in $(ALL_SRC); do \
		if $(CXX) $(DEVICE_GATE_FLAGS) "$$f" ; then \
			echo "  OK   $$f" ; \
		else \
			echo "  FAIL $$f" ; rc=1 ; \
		fi ; \
	done ; \
	if [ $$rc -eq 0 ]; then echo "All device paths compile cleanly."; else echo "One or more device paths failed to compile."; fi ; \
	exit $$rc

run: $(BIN)
	@$(BIN)

# Recommended CI entry point: host compile gate + device compile gate + tests.
check: compile-all compile-device run

clean:
	@rm -rf $(BUILD)
