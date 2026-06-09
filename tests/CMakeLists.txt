add_executable(test_cidr     test_cidr.cpp)
add_executable(test_lpm      test_lpm.cpp)
add_executable(test_frame    test_frame.cpp)
add_executable(test_registration test_registration.cpp)
add_executable(test_netlink      test_netlink.cpp)
target_link_libraries(test_cidr         PRIVATE cfd_core)
target_link_libraries(test_lpm          PRIVATE cfd_core)
target_link_libraries(test_frame        PRIVATE cfd_core)
target_link_libraries(test_registration PRIVATE cfd_core)
target_link_libraries(test_netlink      PRIVATE cfd_core)

if(TARGET msquic)
    function(stage_msquic test_target)
        add_custom_command(TARGET ${test_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:msquic>
                $<TARGET_FILE_DIR:${test_target}>/libmsquic.so.2
            COMMENT "Staging libmsquic.so.2 alongside ${test_target}")
    endfunction()

    stage_msquic(test_cidr)
    stage_msquic(test_lpm)
    stage_msquic(test_frame)
    stage_msquic(test_registration)
    stage_msquic(test_netlink)
endif()

add_test(NAME cidr         COMMAND test_cidr)
add_test(NAME lpm          COMMAND test_lpm)
add_test(NAME frame        COMMAND test_frame)
add_test(NAME registration COMMAND test_registration)
add_test(NAME netlink      COMMAND test_netlink)
