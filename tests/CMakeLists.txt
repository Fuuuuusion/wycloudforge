set(NETECLONE_TESTS
    tst_lrcparser
    tst_tagreader
    tst_playlistcontroller
    tst_playerservice
    tst_multisourcesupport
    tst_searchservice
    tst_searchpage
    tst_songlistview
)

get_filename_component(NETECLONE_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)

foreach(test_name ${NETECLONE_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    if(test_name STREQUAL "tst_songlistview")
        target_sources(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
    endif()
    if(test_name STREQUAL "tst_songlistview" OR test_name STREQUAL "tst_searchpage")
        target_link_libraries(${test_name} PRIVATE neteclone_ui Qt6::Test)
    else()
        target_link_libraries(${test_name} PRIVATE neteclone_core Qt6::Test)
    endif()
    # Windows GUI/QtTest executables may otherwise send the detailed report to
    # the debugger channel, leaving CTest with only a non-zero exit code.
    add_test(NAME ${test_name} COMMAND ${test_name} -o -,txt)
    if(WIN32)
        # The Windows deployment used by this project ships qwindows.dll, while
        # qoffscreen.dll is not present in the test runtime directory.
        set(test_runtime_modifications
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${test_name}>"
            "PATH=path_list_prepend:${NETECLONE_QT_BIN_DIR}"
            "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        set(test_runtime_environment "QT_QPA_PLATFORM=windows")
        if(test_name STREQUAL "tst_songlistview")
            list(APPEND test_runtime_environment "QT_SCALE_FACTOR=1.5")
        endif()
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT "${test_runtime_environment}"
            ENVIRONMENT_MODIFICATION "${test_runtime_modifications}")
    else()
        set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endforeach()
