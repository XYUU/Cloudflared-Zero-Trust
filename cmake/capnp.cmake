# Locate capnproto. Two modes:
#   1) System install (capnp/capnpc-c++ in PATH; libcapnp/libkj as packages)
#   2) FetchContent build from source (CFD_FETCH_CAPNP=ON)
#
# We export a single INTERFACE library `cfd_capnp` that callers link against.

option(CFD_FETCH_CAPNP "Build capnproto from source via FetchContent" OFF)

if(CFD_FETCH_CAPNP)
    include(FetchContent)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(EXTERNAL_CAPNP OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(capnproto
        GIT_REPOSITORY https://github.com/capnproto/capnproto.git
        GIT_TAG        v1.0.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(capnproto)
    add_library(cfd_capnp INTERFACE)
    target_link_libraries(cfd_capnp INTERFACE CapnProto::capnp CapnProto::kj)
    set(CAPNP_EXECUTABLE     $<TARGET_FILE:capnp_tool>     CACHE INTERNAL "")
    set(CAPNPC_CXX_EXECUTABLE $<TARGET_FILE:capnpc_cpp>     CACHE INTERNAL "")
else()
    find_package(CapnProto QUIET)
    if(CapnProto_FOUND)
        add_library(cfd_capnp INTERFACE)
        target_link_libraries(cfd_capnp INTERFACE CapnProto::capnp CapnProto::kj)
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
            COMMAND capnp compile
                    --src-prefix=${CMAKE_SOURCE_DIR}/proto
                    -ocapnpc-c++:${CMAKE_CURRENT_BINARY_DIR}
                    ${schema}
            DEPENDS ${schema}
            COMMENT "capnp: ${stem}"
            VERBATIM
        )
        list(APPEND generated_srcs ${out_cxx})
    endforeach()
    set(${OUT_SRCS_VAR} ${generated_srcs} PARENT_SCOPE)
endfunction()
