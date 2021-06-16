cmake_minimum_required(VERSION 3.5)

project(lps LANGUAGES C)

add_subdirectory(lib/termbox_next)

add_executable(lps main.c)

set_property(TARGET lps PROPERTY C_STANDARD 99)

# Based off of instructions in https://dominikberner.ch/cmake-find-library/
find_library(LIBRARY_ALPM
    NAMES libalpm alpm
    HINTS /usr/lib/)

target_link_libraries(lps PRIVATE ${LIBRARY_ALPM} termbox)

## Will pass -DUSE_ARRAYS to compiler
# target_compile_definitions(lps PRIVATE USE_ARRAYS)

# target_compile_options(lps PRIVATE -Werror -Wall -Wextra)
