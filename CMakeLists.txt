cmake_minimum_required(VERSION 3.16)
project(cfd_cpp LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(CFD_ENABLE_ASAN     "Build with AddressSanitizer (host only)" OFF)
option(CFD_ENABLE_UBSAN    "Build with UndefinedBehaviorSanitizer"   OFF)
option(CFD_BUILD_TESTS     "Build unit tests"                        ON)
option(CFD_STATIC_LINK     "Statically link libstdc++/libgcc"        ON)
option(CFD_STRIP_BINARY    "Strip release binary"                    ON)

# --- Cross-compile awareness ------------------------------------------------
# When CMAKE_TOOLCHAIN_FILE is set (see cmake/toolchains/*.cmake) we are in
# cross-compile mode and disable host-only features such as sanitizers.
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross compiling for ${CMAKE_SYSTEM_PROCESSOR} (${CMAKE_SYSTEM_NAME})")
    set(CFD_ENABLE_ASAN  OFF CACHE BOOL "" FORCE)
    set(CFD_ENABLE_UBSAN OFF CACHE BOOL "" FORCE)
endif()

# --- Compiler hardening -----------------------------------------------------
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Woverloaded-virtual -Wconversion -Wsign-conversion
    -Wnull-dereference -Wdouble-promotion -Wformat=2
    -fno-common -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    endif()
    add_compile_options(-Os -fdata-sections -ffunction-sections)
    add_link_options(-Wl,--gc-sections -Wl,-z,relro -Wl,-z,now)
endif()

if(CFD_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(CFD_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
endif()

if(CFD_STATIC_LINK AND NOT APPLE)
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

# --- Sources ----------------------------------------------------------------
add_subdirectory(src)

if(CFD_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_subdirectory(tests)
endif()

option(CFD_BUILD_FUZZ "Build libFuzzer harnesses (host clang only)" OFF)
if(CFD_BUILD_FUZZ AND NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(fuzz)
endif()
