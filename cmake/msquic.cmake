# Pulls msquic via FetchContent. We pin a known-good tag and build it static
# with the OpenSSL backend already chosen by msquic's own CMake.
#
# Usage: include(cmake/msquic.cmake) from src/CMakeLists when CFD_FETCH_MSQUIC=ON.

include(FetchContent)

# ---- Isolate msquic from project-wide warning and sanitizer flags ------------
#
# cmake processes flags in two places:
#   1. CMAKE_<LANG>_FLAGS  — set by set()/string(APPEND ...)
#   2. COMPILE_OPTIONS     — populated by add_compile_options() in *this* directory
#
# The compile command appends them in order (1) then (2).  Because (2) comes
# AFTER (1), any -Wsign-conversion we add via add_compile_options() overrides
# the -Wno-sign-conversion we'd put in CMAKE_C_FLAGS — making msquic's own
# -Werror fatal on its C sources.
#
# LINK_OPTIONS (from add_link_options) must also be cleared: when
# CFD_ENABLE_ASAN=ON, -fsanitize=address is in LINK_OPTIONS. Directory-level
# LINK_OPTIONS are inherited by FetchContent subdirectories, so libmsquic.so
# would be linked against libasan even though its objects have no ASan
# instrumentation. Loading such a .so alongside an ASan-instrumented test
# binary causes "incompatible ASan runtimes" at startup.
#
# Fix: save and clear both COMPILE_OPTIONS and LINK_OPTIONS before FetchContent;
# restore after so that our own targets still see the full set.
get_directory_property(_cfd_saved_compile_opts COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

get_directory_property(_cfd_saved_link_opts LINK_OPTIONS)
set_property(DIRECTORY PROPERTY LINK_OPTIONS "")

# Also save/patch CMAKE_<LANG>_FLAGS for the small set of msquic-internal
# warnings that are legitimately present in its C code.
set(CMAKE_C_FLAGS_SAVED   "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_SAVED "${CMAKE_CXX_FLAGS}")
string(APPEND CMAKE_C_FLAGS
    " -Wno-pedantic -Wno-conversion -Wno-sign-conversion"
    " -Wno-error=pedantic -Wno-error=conversion -Wno-error=sign-conversion")
string(APPEND CMAKE_CXX_FLAGS
    " -Wno-pedantic -Wno-conversion -Wno-sign-conversion"
    " -Wno-error=pedantic -Wno-error=conversion -Wno-error=sign-conversion")
# Note: -Wno-free-labels/-Wno-error=free-labels intentionally absent.
# -Wfree-labels was introduced in GCC 14; trying to suppress it on GCC 13
# (Ubuntu 24.04 default) causes a fatal "no option '-Wfree-labels'" error.

set(MSQUIC_TAG "v2.4.5" CACHE STRING "msquic git tag")
set(QUIC_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST     OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_PERF     OFF CACHE BOOL "" FORCE)
set(QUIC_TLS            "openssl" CACHE STRING "" FORCE)
set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)

FetchContent_Declare(msquic
    GIT_REPOSITORY https://github.com/microsoft/msquic.git
    GIT_TAG        ${MSQUIC_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
    # Patch: msquic has no MIPS branch for OpenSSL; without this, the 'config'
    # auto-detect script runs on the x86-64 host, picks linux-x86_64, and injects
    # -m64 which mipsel-linux-gnu-gcc rejects.  The script is idempotent.
    PATCH_COMMAND
        ${CMAKE_COMMAND}
            -DSOURCE_DIR=<SOURCE_DIR>
            -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
            -P ${CMAKE_CURRENT_LIST_DIR}/patches/msquic-mipsel-openssl.cmake
)
FetchContent_MakeAvailable(msquic)

# On 32-bit ARM and MIPS, GCC emits __sync_*_8 / __atomic_*_8 calls for 8-byte
# atomic ops (no native hardware instruction).  These must be resolved inside
# libmsquic.so itself at build time; undefined 8-byte atomic refs in the .so
# cause a hard linker error when linking the final executable.
#
# ARM: libatomic.a from the ARM cross-toolchain (Bootlin musl / Ubuntu
#      arm-linux-gnueabihf) is compiled with -fPIC, so it can be embedded
#      directly via -Wl,-Bstatic -latomic -Wl,-Bdynamic.
#
# MIPS: Ubuntu's mipsel-linux-gnu libatomic.a is NOT compiled with -fPIC.
#       Embedding it into a .so fails with R_MIPS_HI16 relocation errors.
#       Instead, a tiny hand-written PIC implementation (stubs/atomic_mips32.c)
#       is added as a source file to the msquic target; it uses a compiler-
#       generated LL/SC spinlock on a 32-bit word (inline on MIPS II+, all
#       modern Linux MIPS targets) so there is no recursive libatomic call.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm" AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_libraries(msquic PRIVATE "-Wl,-Bstatic" "-latomic" "-Wl,-Bdynamic")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "mips64")
    target_sources(msquic PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/stubs/atomic_mips32.c)
endif()

# Restore everything so subsequent targets see the full project flags.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS_SAVED}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_SAVED}")
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_cfd_saved_compile_opts}")
set_property(DIRECTORY PROPERTY LINK_OPTIONS    "${_cfd_saved_link_opts}")

# msquic exports the `msquic` target.
set(MSQUIC_TARGET msquic CACHE INTERNAL "")
