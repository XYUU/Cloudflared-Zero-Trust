# ---- Cap'n Proto (control-plane) ---------------------------------------
# Without it, encode_register_request() returns an empty buffer and the
# RegisterConnection call short-circuits with not_supported.
include(${CMAKE_SOURCE_DIR}/cmake/capnp.cmake)
set(CFD_GENERATED_CAPNP_SRCS)
if(TARGET cfd_capnp)
    cfd_capnp_generate(CFD_GENERATED_CAPNP_SRCS
        ${CMAKE_SOURCE_DIR}/proto/tunnelrpc.capnp)
endif()

add_library(cfd_core STATIC
    log.cpp
    config.cpp
    util/buffer.cpp
    util/socket.cpp
    net/lpm_trie.cpp
    net/cidr.cpp
    net/netlink.cpp
    tun/tun_device_linux.cpp
    tunnel/frame.cpp
    tunnel/quic_client.cpp
    tunnel/registration.cpp
    router/router.cpp
    ${CFD_GENERATED_CAPNP_SRCS}
)
if(TARGET cfd_capnp)
    target_link_libraries(cfd_core PUBLIC cfd_capnp)
    target_include_directories(cfd_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_definitions(cfd_core PUBLIC CFD_HAVE_CAPNP=1)
endif()

target_include_directories(cfd_core
    PUBLIC ${CMAKE_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

# Threading is mandatory
find_package(Threads REQUIRED)
target_link_libraries(cfd_core PUBLIC Threads::Threads)

# QUIC backend (msquic). Three resolution paths, in order:
#   1) MSQUIC_LIB + MSQUIC_INCLUDE explicitly provided  -> prebuilt
#   2) CFD_FETCH_MSQUIC=ON                              -> FetchContent build from source
#   3) find_library(msquic) on the system               -> distro package
# All three define CFD_HAVE_MSQUIC=1.
option(CFD_FETCH_MSQUIC "Build msquic from source via FetchContent" OFF)

if(MSQUIC_LIB AND MSQUIC_INCLUDE)
    target_compile_definitions(cfd_core PUBLIC CFD_HAVE_MSQUIC=1)
    target_include_directories(cfd_core PUBLIC ${MSQUIC_INCLUDE})
    target_link_libraries(cfd_core PUBLIC ${MSQUIC_LIB})
    message(STATUS "msquic (prebuilt): ${MSQUIC_LIB}")
elseif(CFD_FETCH_MSQUIC)
    include(${CMAKE_SOURCE_DIR}/cmake/msquic.cmake)
    target_compile_definitions(cfd_core PUBLIC CFD_HAVE_MSQUIC=1)
    target_link_libraries(cfd_core PUBLIC ${MSQUIC_TARGET})
    message(STATUS "msquic (FetchContent): ${MSQUIC_TAG}")
else()
    find_library(MSQUIC_LIB_AUTO msquic)
    find_path(MSQUIC_INC_AUTO    msquic.h)
    if(MSQUIC_LIB_AUTO AND MSQUIC_INC_AUTO)
        target_compile_definitions(cfd_core PUBLIC CFD_HAVE_MSQUIC=1)
        target_include_directories(cfd_core PUBLIC ${MSQUIC_INC_AUTO})
        target_link_libraries(cfd_core PUBLIC ${MSQUIC_LIB_AUTO})
        message(STATUS "msquic (system): ${MSQUIC_LIB_AUTO}")
    else()
        message(WARNING
            "msquic not found - tunnel will be a no-op stub.\n"
            "  enable it with: -DCFD_FETCH_MSQUIC=ON  (fetch+build), or\n"
            "                   -DMSQUIC_LIB=... -DMSQUIC_INCLUDE=...")
    endif()
endif()

add_executable(cfd main.cpp)
target_link_libraries(cfd PRIVATE cfd_core)

# MIPS32 has no native 64-bit atomic instructions; GCC emits __atomic_load_8 /
# __atomic_store_8 calls (from capnproto's layout code) that live in libatomic.
# Must appear after cfd_core in the link order so --as-needed keeps it.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "mips64")
    target_link_libraries(cfd PRIVATE atomic)
endif()

# End-to-end handshake probe: connects to a Cloudflare edge, negotiates
# ALPN, prints handshake stats, exits. Useful as a CI gate and for hand
# debugging from the embedded target.
add_executable(cfd_probe tools/probe_handshake.cpp)
target_link_libraries(cfd_probe PRIVATE cfd_core)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "mips64")
    target_link_libraries(cfd_probe PRIVATE atomic)
endif()

if(CFD_STRIP_BINARY AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET cfd POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:cfd>
        COMMENT "Stripping cfd")
endif()

install(TARGETS cfd RUNTIME DESTINATION bin)
