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

if(MSVC)
    add_compile_options(/W4 /WX /D_CRT_SECURE_NO_WARNINGS /utf-8)
else()
    add_compile_options(-Wall -Wextra -Werror -Wshadow)
endif()

# Portable core: no Win32 anywhere in here. Linked by both the app and tests.
add_library(treetop_core STATIC
    src/util/err.c
    src/render/format.c
    src/model/table.c
    src/model/delta.c
    src/model/tree.c
    src/model/sort.c
    src/model/view.c
    src/agent/rules.c
    src/agent/detect.c
    src/input/filter.c
)
target_include_directories(treetop_core PUBLIC include)

add_executable(treetop
    src/main.c
    src/platform/ntapi.c
    src/platform/win_system.c
)
target_link_libraries(treetop PRIVATE treetop_core iphlpapi psapi)

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
)
target_include_directories(treetop_tests PRIVATE tests)
target_link_libraries(treetop_tests PRIVATE treetop_core)
add_test(NAME unit COMMAND treetop_tests)
