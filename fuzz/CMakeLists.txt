# libFuzzer harnesses. Host-only (clang + -fsanitize=fuzzer,address). The
# embedded toolchains (gcc + musl) cannot build these, so we guard.

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "fuzzers disabled (require clang)")
    return()
endif()
if(CMAKE_CROSSCOMPILING)
    message(STATUS "fuzzers disabled (cross-compiling)")
    return()
endif()

set(FUZZ_FLAGS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

function(cfd_add_fuzzer name src)
    add_executable(${name} ${src})
    target_compile_options(${name} PRIVATE ${FUZZ_FLAGS})
    target_link_options(${name}    PRIVATE ${FUZZ_FLAGS})
    target_link_libraries(${name}  PRIVATE cfd_core)
endfunction()

cfd_add_fuzzer(fuzz_frame        fuzz_frame.cpp)
cfd_add_fuzzer(fuzz_registration fuzz_registration.cpp)
cfd_add_fuzzer(fuzz_cidr         fuzz_cidr.cpp)
