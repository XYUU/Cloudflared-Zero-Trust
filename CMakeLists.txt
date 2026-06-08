cmake_minimum_required(VERSION 3.16)
project(cfd_cpp LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
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

# Use $ORIGIN so the deployed binary finds libmsquic.so.2 in the same directory
# as itself.  Without this, cmake embeds the build-tree path as RPATH (e.g.
# /home/runner/work/.../build-linux-amd64/_deps/msquic-build/bin/Release) which
# doesn't exist on any other machine.  BUILD_WITH_INSTALL_RPATH makes the binary
# carry the deployment RPATH at build time so no separate install step is needed.
# A POST_BUILD command in src/CMakeLists.txt stages libmsquic.so.2{,.4.5}
# alongside the binary so it can be run directly from the build directory too.
set(CMAKE_INSTALL_RPATH "$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

# --- Compiler hardening -----------------------------------------------------
add_compile_options(
    -Wall -Wextra -Wpedantic
    # C++-only flags — guard with generator expression so they are NOT passed
    # to C translation units (e.g. msquic's C files), which would produce
    # "valid for C++/ObjC++ but not for C" warnings that become errors under
    # msquic's own -Werror.
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    -Wshadow -Wcast-align
    -Wconversion -Wsign-conversion
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
