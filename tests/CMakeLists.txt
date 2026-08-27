set(NETECLONE_TESTS
    tst_lrcparser
    tst_tagreader
    tst_playlistcontroller
    tst_playerservice
)

foreach(test_name ${NETECLONE_TESTS})
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE neteclone_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

