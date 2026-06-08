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

# 32-bit ARM and MIPS lack native 64-bit atomic instructions; GCC emits calls to
# __atomic_load_8 / __atomic_store_8 / __atomic_fetch_{add,sub}_8 etc., which
# live in libatomic.  msquic (shared) and capnproto (static) both produce such
# references on these architectures.
#
# Must be a PUBLIC dep of cfd_core so it appears AFTER libcapnp.a in the link
# order: --as-needed discards a library if no unresolved symbols exist at the
# point it is encountered, so ordering relative to the archives that need it matters.
if((CMAKE_SYSTEM_PROCESSOR MATCHES "^arm"  AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64") OR
   (CMAKE_SYSTEM_PROCESSOR MATCHES "^mips" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES  "mips64"))
    target_link_libraries(cfd_core PUBLIC atomic)
endif()

add_executable(cfd main.cpp)
target_link_libraries(cfd PRIVATE cfd_core)

# End-to-end handshake probe: connects to a Cloudflare edge, negotiates
# ALPN, prints handshake stats, exits. Useful as a CI gate and for hand
# debugging from the embedded target.
add_executable(cfd_probe tools/probe_handshake.cpp)
target_link_libraries(cfd_probe PRIVATE cfd_core)

# When cross-compiling with FetchContent msquic the binary carries a $ORIGIN
# RPATH. Stage the versioned .so and its SONAME symlink (libmsquic.so.2)
# alongside the binary so the deployment directory is self-contained.
# The dynamic linker resolves libraries by SONAME, so libmsquic.so.2.4.5 alone
# is not enough — the symlink named libmsquic.so.2 must also be present.
if(CMAKE_CROSSCOMPILING AND CFD_FETCH_MSQUIC)
    add_custom_command(TARGET cfd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:msquic>
            $<TARGET_FILE_DIR:cfd>/libmsquic.so.2.4.5
        COMMAND ${CMAKE_COMMAND} -E remove -f
            $<TARGET_FILE_DIR:cfd>/libmsquic.so.2
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libmsquic.so.2.4.5
            $<TARGET_FILE_DIR:cfd>/libmsquic.so.2
        COMMENT "Staging libmsquic.so.2 alongside cfd"
    )
endif()

if(CFD_STRIP_BINARY AND CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET cfd POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:cfd>
        COMMENT "Stripping cfd")
endif()

install(TARGETS cfd RUNTIME DESTINATION bin)
