cmake_minimum_required(VERSION 3.21)
project(NeteaseClone VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Svg Sql Test)

qt_standard_project_setup()

# 元数据解析:优先 TagLib(vcpkg 安装);找不到时回退内置最小解析器
find_package(taglib CONFIG QUIET)
set(NETECLONE_HAVE_TAGLIB FALSE)
if(TARGET TagLib::tag)
    set(NETECLONE_HAVE_TAGLIB TRUE)
    message(STATUS "TagLib found: using full metadata parsing")
else()
    message(WARNING "TagLib not found: falling back to built-in minimal tag parser")
endif()

add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(app)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt)
    enable_testing()
    add_subdirectory(tests)
endif()

