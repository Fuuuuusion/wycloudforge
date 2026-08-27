add_executable(NeteaseClone WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    ../resources/resources.qrc
)

target_link_libraries(NeteaseClone PRIVATE
    neteclone_core
    neteclone_ui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Svg
    Qt6::Sql
)

target_include_directories(NeteaseClone PRIVATE ${CMAKE_SOURCE_DIR})
