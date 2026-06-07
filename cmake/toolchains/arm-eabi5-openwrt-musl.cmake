# Toolchain for ARMv5 OpenWrt / 光猫 (EABI5, musl, soft-float)
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT CFD_TOOLCHAIN_PREFIX)
    set(CFD_TOOLCHAIN_PREFIX "arm-linux-musleabi-")
endif()

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
                    -march=armv5te -mfloat-abi=soft
                    -fno-exceptions -fno-rtti -fno-strict-aliasing)
# Fully static musl link: zero libc version dependency on target (e.g. Buildroot GLIBC 2.26)
add_link_options(-static -Wl,--gc-sections -Wl,--as-needed -s)
