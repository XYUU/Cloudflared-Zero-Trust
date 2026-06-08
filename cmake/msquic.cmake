# Pulls msquic via FetchContent. We pin a known-good tag and build it static
# with the OpenSSL/BoringSSL backend already chosen by msquic's own CMake.
#
# Usage: include(cmake/msquic.cmake) from the top-level CMakeLists when
# CFD_FETCH_MSQUIC=ON, or supply -DMSQUIC_LIB=... pointing at a prebuilt copy
# (preferred for cross builds where the toolchain SDK ships one).

include(FetchContent)

# msquic v2.4.x has many pedantic/conversion/sign-conversion warnings that
# become errors under GCC's strict mode. Suppress them for the subproject.
set(CMAKE_C_FLAGS_SAVED   "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS_SAVED "${CMAKE_CXX_FLAGS}")
string(APPEND CMAKE_C_FLAGS
    " -Wno-pedantic -Wno-conversion -Wno-sign-conversion -Wno-free-labels"
    " -Wno-error=pedantic -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=free-labels")
string(APPEND CMAKE_CXX_FLAGS
    " -Wno-pedantic -Wno-conversion -Wno-sign-conversion -Wno-free-labels"
    " -Wno-error=pedantic -Wno-error=conversion -Wno-error=sign-conversion -Wno-error=free-labels")

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

# Restore flags so the rest of the project isn't affected.
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS_SAVED}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_SAVED}")

# msquic exports the `msquic` target.
set(MSQUIC_TARGET msquic CACHE INTERNAL "")
