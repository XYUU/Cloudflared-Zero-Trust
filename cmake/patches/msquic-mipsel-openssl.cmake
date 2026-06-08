# Patch script: applied by cmake/msquic.cmake via FetchContent PATCH_COMMAND.
#
# Problem: msquic's submodules/CMakeLists.txt has no MIPS branch for OpenSSL.
# It falls through to the final else() which uses OpenSSL's 'config' auto-detect
# script. That script runs on the x86-64 host, picks linux-x86_64, and injects
# -m64 into the Makefile. mipsel-linux-gnu-gcc does not accept -m64.
#
# Fix: insert an explicit elseif(MIPS) branch that calls OpenSSL's 'Configure'
# with the literal linux-mips32 target and the correct --cross-compile-prefix,
# analogous to how msquic already handles the arm case.
#
# Usage (via FetchContent PATCH_COMMAND):
#   cmake -DSOURCE_DIR=<SOURCE_DIR>
#         -DCMAKE_SYSTEM_PROCESSOR=<value>
#         -P cmake/patches/msquic-mipsel-openssl.cmake

cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
    return()
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "Pass -DSOURCE_DIR=<path> to this script.")
endif()

set(_f "${SOURCE_DIR}/submodules/CMakeLists.txt")
file(READ "${_f}" _contents)

if(_contents MATCHES "linux-mips32")
    return()  # idempotent: already patched
endif()

# The exact text being replaced (12-space indent, matches v2.4.5 submodules cmake).
string(REPLACE
    [=[            else()
                set(OPENSSL_CONFIG_CMD ${CMAKE_CURRENT_SOURCE_DIR}/${QUIC_OPENSSL}/config
                            CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER})
            endif()]=]
    [=[            elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
                set(OPENSSL_CONFIG_CMD ${CMAKE_CURRENT_SOURCE_DIR}/${QUIC_OPENSSL}/Configure
                    linux-mips32 --cross-compile-prefix=${GNU_MACHINE}-)
            else()
                set(OPENSSL_CONFIG_CMD ${CMAKE_CURRENT_SOURCE_DIR}/${QUIC_OPENSSL}/config
                            CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER})
            endif()]=]
    _contents "${_contents}")

file(WRITE "${_f}" "${_contents}")
message(STATUS "msquic patch: added linux-mips32 OpenSSL Configure branch")
