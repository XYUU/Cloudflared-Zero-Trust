# Pulls msquic via FetchContent. We pin a known-good tag and build it static
# with the OpenSSL/BoringSSL backend already chosen by msquic's own CMake.
#
# Usage: include(cmake/msquic.cmake) from the top-level CMakeLists when
# CFD_FETCH_MSQUIC=ON, or supply -DMSQUIC_LIB=... pointing at a prebuilt copy
# (preferred for cross builds where the toolchain SDK ships one).

include(FetchContent)

set(MSQUIC_TAG "v2.4.5" CACHE STRING "msquic git tag")
set(QUIC_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_TEST   OFF CACHE BOOL "" FORCE)
set(QUIC_BUILD_PERF   OFF CACHE BOOL "" FORCE)
set(QUIC_TLS          "openssl" CACHE STRING "" FORCE)
set(QUIC_ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(msquic
    GIT_REPOSITORY https://github.com/microsoft/msquic.git
    GIT_TAG        ${MSQUIC_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(msquic)

# msquic exports the `msquic` target.
set(MSQUIC_TARGET msquic CACHE INTERNAL "")
