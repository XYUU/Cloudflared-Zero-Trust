# Locate capnproto. Two modes:
#   1) System install (capnp/capnpc-c++ in PATH; libcapnp/libkj as packages)
#   2) FetchContent build from source (CFD_FETCH_CAPNP=ON)
#
# We export a single INTERFACE library `cfd_capnp` that callers link against.

option(CFD_FETCH_CAPNP "Build capnproto from source via FetchContent" OFF)

# Targets that cfd_capnp_generate's custom commands must wait for before running.
# Populated only when we build the capnp tool as part of this cmake invocation.
set(_cfd_capnp_tool_deps "")

if(CFD_FETCH_CAPNP)
    include(FetchContent)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(EXTERNAL_CAPNP OFF CACHE BOOL "" FORCE)
    # GIT_TAG must match the system capnproto apt package version used on CI
    # (Ubuntu 24.04 ships capnproto 1.0.1.1 = upstream v1.0.1, CAPNP_VERSION=2000001).
    # On native (non-cross-compile) builds the capnp_tool built here is used directly
    # instead of any system binary, so the versions always match automatically.
    FetchContent_Declare(capnproto
        GIT_REPOSITORY https://github.com/capnproto/capnproto.git
        GIT_TAG        v1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(capnproto)
    add_library(cfd_capnp INTERFACE)
    target_link_libraries(cfd_capnp INTERFACE CapnProto::capnp CapnProto::capnp-rpc CapnProto::kj CapnProto::kj-async)

    if(CMAKE_CROSSCOMPILING)
        # capnp_tool is built for the target arch and cannot run on the host.
        # Fall back to the system capnp, which must be the same version as GIT_TAG above.
        find_program(CAPNP_EXECUTABLE      capnp      REQUIRED)
        find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ REQUIRED)
    else()
        # Native build: use the just-built capnp tool (same version as library — no mismatch).
        set(CAPNP_EXECUTABLE      $<TARGET_FILE:capnp_tool> CACHE INTERNAL "")
        set(CAPNPC_CXX_EXECUTABLE $<TARGET_FILE:capnpc_cpp> CACHE INTERNAL "")
        set(_cfd_capnp_tool_deps capnp_tool capnpc_cpp)
    endif()
else()
    find_package(CapnProto QUIET)
    if(CapnProto_FOUND)
        add_library(cfd_capnp INTERFACE)
        target_link_libraries(cfd_capnp INTERFACE CapnProto::capnp CapnProto::capnp-rpc CapnProto::kj CapnProto::kj-async)
        # find_package(CapnProto) exports CAPNP_EXECUTABLE / CAPNPC_CXX_EXECUTABLE.
        # Add fallbacks for older cmake package configs that omit them.
        if(NOT CAPNP_EXECUTABLE)
            find_program(CAPNP_EXECUTABLE capnp REQUIRED)
        endif()
        if(NOT CAPNPC_CXX_EXECUTABLE)
            find_program(CAPNPC_CXX_EXECUTABLE capnpc-c++ REQUIRED)
        endif()
    else()
        message(WARNING "capnproto not found - control-plane (RegisterConnection) disabled. "
                        "Pass -DCFD_FETCH_CAPNP=ON or install libcapnp-dev.")
    endif()
endif()

# Run capnpc to generate <name>.capnp.{c++,h} into the build tree.
function(cfd_capnp_generate OUT_SRCS_VAR)
    set(generated_srcs)
    foreach(schema IN LISTS ARGN)
        get_filename_component(stem ${schema} NAME)
        set(out_cxx ${CMAKE_CURRENT_BINARY_DIR}/${stem}.c++)
        set(out_h   ${CMAKE_CURRENT_BINARY_DIR}/${stem}.h)
        add_custom_command(
            OUTPUT  ${out_cxx} ${out_h}
            COMMAND ${CAPNP_EXECUTABLE} compile
                    --src-prefix=${CMAKE_SOURCE_DIR}/proto
                    -o${CAPNPC_CXX_EXECUTABLE}:${CMAKE_CURRENT_BINARY_DIR}
                    ${schema}
            DEPENDS ${schema} ${_cfd_capnp_tool_deps}
            COMMENT "capnp: ${stem}"
            VERBATIM
        )
        list(APPEND generated_srcs ${out_cxx})
    endforeach()
    set(${OUT_SRCS_VAR} ${generated_srcs} PARENT_SCOPE)
endfunction()
