add_executable(FuSinplayer WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    FuSinplayer.rc
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

target_link_libraries(FuSinplayer PRIVATE
    neteclone_core
    neteclone_ui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::Sql
)

if(WIN32)
    target_link_libraries(FuSinplayer PRIVATE dwmapi)
endif()

target_include_directories(FuSinplayer PRIVATE ${CMAKE_SOURCE_DIR})
