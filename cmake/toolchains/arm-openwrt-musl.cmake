# Toolchain for ARMv7 OpenWrt / 光猫 (musl, hard-float)
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT CFD_TOOLCHAIN_PREFIX)
    set(CFD_TOOLCHAIN_PREFIX "arm-openwrt-linux-muslgnueabi-")
endif()

# msquic uses GNU_MACHINE as the stem for OpenSSL's --cross-compile-prefix (appends '-').
# Without it the prefix degenerates to "-" and the OpenSSL cross-build cannot find a compiler.
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

add_compile_options(-Os -ffunction-sections -fdata-sections
                    -mfloat-abi=hard -mfpu=neon)
add_link_options(-Wl,--gc-sections -Wl,--as-needed -s)
