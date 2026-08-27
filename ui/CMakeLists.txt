add_library(neteclone_ui STATIC
    TitleBar.h
    TitleBar.cpp
    SideBar.h
    SideBar.cpp
    ProgressSlider.h
    ProgressSlider.cpp
    PlayerBar.h
    PlayerBar.cpp
    AuroraBackground.h
    AuroraBackground.cpp
    CoverProvider.h
    CoverProvider.cpp
    SongListModel.h
    SongListModel.cpp
    SongListView.h
    SongListView.cpp
    CoverCard.h
    CoverCard.cpp
    DiscoverPage.h
    DiscoverPage.cpp
    LibraryPage.h
    LibraryPage.cpp
    SongListPage.h
    SongListPage.cpp
    LyricWidget.h
    LyricWidget.cpp
    PlayingPage.h
    PlayingPage.cpp
    SearchPage.h
    SearchPage.cpp
    SettingsDialog.h
    SettingsDialog.cpp
    LyricEditorDialog.h
    LyricEditorDialog.cpp
)

target_include_directories(neteclone_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(neteclone_ui PUBLIC Qt6::Widgets Qt6::Svg)
target_link_libraries(neteclone_ui PUBLIC neteclone_core)
