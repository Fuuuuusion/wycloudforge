add_library(neteclone_core STATIC
    Song.h
    TagReader.h
    TagReader.cpp
    LrcParser.h
    LrcParser.cpp
    LyricsLoader.h
    LyricsLoader.cpp
    SettingsService.h
    SettingsService.cpp
    LibraryService.h
    LibraryService.cpp
    PlayerService.h
    PlayerService.cpp
    PlaylistController.h
    PlaylistController.cpp
    SearchService.h
    SearchService.cpp
)

target_include_directories(neteclone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(neteclone_core PUBLIC Qt6::Core Qt6::Sql Qt6::Multimedia)

if(NETECLONE_HAVE_TAGLIB)
    target_compile_definitions(neteclone_core PUBLIC NETECLONE_HAVE_TAGLIB)
    target_link_libraries(neteclone_core PRIVATE TagLib::tag)
endif()
