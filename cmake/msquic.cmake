# Pulls msquic via FetchContent. We pin a known-good tag and build it static
# with the OpenSSL backend already chosen by msquic's own CMake.
#
# Usage: include(cmake/msquic.cmake) from src/CMakeLists when CFD_FETCH_MSQUIC=ON.

include(FetchContent)

# ---- Isolate msquic from project-wide warning flags --------------------------
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
# Fix: save and clear the directory COMPILE_OPTIONS before FetchContent so
# msquic's subdirectory inherits an empty set.  Restore for our own targets.
get_directory_property(_cfd_saved_compile_opts COMPILE_OPTIONS)
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")

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

set(MSQUIC_TAG "v2.4.7" CACHE STRING "msquic git tag")
set(QUIC_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST     OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_PERF     OFF CACHE BOOL "" FORCE)
set(QUIC_TLS            "openssl" CACHE STRING "" FORCE)
set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS   OFF CACHE BOOL "" FORCE)

# For cross-compiling, msquic's internal OpenSSL build can be flaky.
# If we are on the host, prefer system OpenSSL.
if(NOT CMAKE_CROSSCOMPILING)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        message(STATUS "Found system OpenSSL, disabling msquic internal OpenSSL build")
        set(QUIC_USE_SYSTEM_LIBCRYPTO ON CACHE BOOL "" FORCE)
    endif()
endif()

FetchContent_Declare(msquic
    GIT_REPOSITORY https://github.com/microsoft/msquic.git
    GIT_TAG        ${MSQUIC_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
    # Workaround for msquic 2.4.x bug: -latomic is incorrectly passed as a top-level 
    # argument to OpenSSL's Configure script, causing configuration failure.
    PATCH_COMMAND sed -i "/-latomic/d" submodules/CMakeLists.txt
)
FetchContent_MakeAvailable(msquic)

# Restore everything so subsequent targets see the full project flags.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS_SAVED}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_SAVED}")
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_cfd_saved_compile_opts}")

# msquic exports the `msquic` target.
set(MSQUIC_TARGET msquic CACHE INTERNAL "")
