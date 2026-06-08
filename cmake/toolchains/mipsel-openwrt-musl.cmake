# Toolchain for MIPS little-endian OpenWrt / 光猫 (musl)
# Usage:
#   cmake -B build-mipsel \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mipsel-openwrt-musl.cmake \
#         -DCFD_SYSROOT=/opt/openwrt-sdk/staging_dir/target-mipsel_24kc_musl
#
# Expect the OpenWrt SDK in $OPENWRT_SDK or pass -DCFD_TOOLCHAIN_PREFIX=...
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

if(NOT CFD_TOOLCHAIN_PREFIX)
    set(CFD_TOOLCHAIN_PREFIX "mipsel-openwrt-linux-musl-")
endif()

# msquic uses GNU_MACHINE as the stem for OpenSSL's --cross-compile-prefix (appends '-').
# Required by the mipsel Configure path added via cmake/patches/msquic-mipsel-openssl.cmake.
string(REGEX REPLACE "-$" "" _gnu_machine "${CFD_TOOLCHAIN_PREFIX}")
set(GNU_MACHINE "${_gnu_machine}" CACHE STRING "msquic: stem for OpenSSL --cross-compile-prefix")

set(CMAKE_C_COMPILER   ${CFD_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CFD_TOOLCHAIN_PREFIX}g++)
set(CMAKE_AR           ${CFD_TOOLCHAIN_PREFIX}ar)
set(CMAKE_RANLIB       ${CFD_TOOLCHAIN_PREFIX}ranlib)
set(CMAKE_STRIP        ${CFD_TOOLCHAIN_PREFIX}strip)

if(CFD_SYSROOT)
    set(CMAKE_SYSROOT ${CFD_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH ${CFD_SYSROOT})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Footprint flags for 光猫-grade hardware (~64MB RAM, ~16MB flash)
add_compile_options(-Os -ffunction-sections -fdata-sections)
add_link_options(-Wl,--gc-sections -Wl,--as-needed -s)
