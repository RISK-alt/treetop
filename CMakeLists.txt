cmake_minimum_required(VERSION 3.15)
project(treetop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
)
target_include_directories(treetop_core PUBLIC include)

add_executable(treetop
    src/main.c
)
target_link_libraries(treetop PRIVATE treetop_core iphlpapi psapi)

enable_testing()
add_executable(treetop_tests
    tests/main.c
    tests/test_smoke.c
)
target_include_directories(treetop_tests PRIVATE tests)
target_link_libraries(treetop_tests PRIVATE treetop_core)
add_test(NAME unit COMMAND treetop_tests)
