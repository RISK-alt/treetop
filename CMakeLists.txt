cmake_minimum_required(VERSION 3.15)
project(treetop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Without this, CMake compiles as -std=gnu11 on gcc, silently permitting
# GNU extensions MSVC does not support. MSVC is not always available to
# build with locally, so this must be enforced here, not caught by eye.
set(CMAKE_C_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "treetop targets Windows only")
endif()

# docs/install.md and the README both promise Windows 10 build 1809
# (RS5) or later, and the platform layer has relied on that floor since
# Task 9 - but nothing ever told the compiler. Without _WIN32_WINNT and
# NTDDI_VERSION pinned, each toolchain's headers fall back to their own
# default, and the two defaults do not agree: MinGW-w64's headers expose
# version-gated Windows API types permissively, while the Microsoft SDK
# gates the same types strictly behind these macros and simply omits
# them below the floor. MIB_TCP6TABLE_OWNER_PID (win_ports.c) is exactly
# such a type - MinGW declared it, MSVC did not - and the fix is to set
# the floor once, project-wide, before any Windows header in any
# translation unit sees it, not to chase individual types as they turn
# up. 0x0A00 / 0x0A000006 is Windows 10 1809, matching the documented
# floor exactly.
add_compile_definitions(
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000006
)

# README and docs/install.md both promise a single self-contained exe with
# nothing to install alongside it. MSVC's default /MD does not deliver that:
# it links vcruntime140.dll, which ships with the Visual C++ redistributable
# and not with Windows, so a released /MD build would fail to start on a
# clean machine with no diagnostic beyond a missing-DLL box. Static CRT
# instead. The MinGW leg needs no equivalent: it targets UCRT, which is part
# of Windows 10 and later. .github/workflows/release.yml checks the shipped
# binary's imports against an allowlist so this promise stays machine-checked
# rather than remembered.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /WX /D_CRT_SECURE_NO_WARNINGS /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror -Wshadow)
endif()

# Portable core: no Win32 anywhere in here. Linked by both the app and tests.
add_library(treetop_core STATIC
    src/util/err.c
    src/util/str.c
    src/render/format.c
    src/render/json.c
    src/model/table.c
    src/model/delta.c
    src/model/tree.c
    src/model/sort.c
    src/model/view.c
    src/agent/rules.c
    src/agent/detect.c
    src/input/filter.c
    src/input/keys.c
    src/render/frame.c
    src/render/theme.c
    src/render/meters.c
    src/render/table.c
    src/render/chrome.c
    src/cli.c
)
target_include_directories(treetop_core PUBLIC include)

add_executable(treetop
    src/main.c
    src/app.c
    src/platform/ntapi.c
    src/platform/win_system.c
    src/platform/win_process.c
    src/platform/win_cmdline.c
    src/platform/win_ports.c
    src/platform/win_console.c
    src/platform/win_kill.c
)
target_link_libraries(treetop PRIVATE treetop_core iphlpapi psapi ws2_32)

enable_testing()
add_executable(treetop_tests
    tests/main.c
    tests/test_smoke.c
    tests/test_format.c
    tests/test_table.c
    tests/test_delta.c
    tests/test_tree.c
    tests/test_agent.c
    tests/test_view.c
    tests/test_json.c
    tests/test_frame.c
    tests/test_draw_table.c
    tests/test_chrome.c
    tests/test_keys.c
    tests/test_cli.c
)
target_include_directories(treetop_tests PRIVATE tests)
target_link_libraries(treetop_tests PRIVATE treetop_core)
add_test(NAME unit COMMAND treetop_tests)
