#!/bin/bash
# ---------------------------------------------------------------------------
# Host debug build + test runner for the MC75 HomeBox client.
#
# This is the single entry point for the host gate: it runs
# `make -C tests/host check`, i.e. compile-all + compile-device + the test
# suite. CI should call this script rather than the Makefile directly.
#
# The production application targets Windows Mobile 6.5 (ARMV4I) and is built
# with Visual Studio 2008 (see scripts/build_winmobile.bat). That toolchain
# only exists on Windows, so it cannot run in most CI environments.
#
# This script instead builds the platform-independent CORE LOGIC (StrUtil,
# JsonLite, Item, Location, Config, Journal, HttpClient, HbClient, SyncEngine)
# against a small Win32/CE shim (tests/host/shim) using the host's native
# compiler, then runs the unit + integration test suite. Every remaining source
# file - the GUI views, Controller, main and ScannerHAL - is compile-checked in
# both the host and the device (HBX_USE_EMDK + HBX_USE_WININET) configuration.
# It lets developers and CI validate the core logic on any POSIX machine
# without a device or the Windows Mobile SDK.
#
# Exit status is non-zero if the build fails or any test fails.
# ---------------------------------------------------------------------------
set -euo pipefail

echo "========================================"
echo " HomeBox Client - Host Debug Build & Test"
echo "========================================"

# Resolve repository root relative to this script so it works from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CXX="${CXX:-g++}"
if ! command -v "${CXX}" >/dev/null 2>&1; then
    echo "ERROR: C++ compiler '${CXX}' not found on PATH." >&2
    echo "Install g++/clang++ or set CXX to your compiler." >&2
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    echo "ERROR: 'make' not found on PATH." >&2
    exit 1
fi

echo "Compiler : $(${CXX} --version | head -1)"
echo "Root     : ${ROOT_DIR}"
echo

# Full-codebase compile gate (all production sources, including the GUI layer)
# followed by the runnable unit + integration test suite. CXX is passed on the
# command line so the compiler reported above is the one actually used.
make -C "${ROOT_DIR}/tests/host" check CXX="${CXX}"

echo
echo "Host debug build & tests completed successfully."
