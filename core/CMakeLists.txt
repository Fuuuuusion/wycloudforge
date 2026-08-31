add_library(neteclone_core STATIC
    Song.h
    MusicSource.h
    MusicSource.cpp
    MusicSourceRegistry.h
    MusicSourceRegistry.cpp
    TagReader.h
    TagReader.cpp
    LrcParser.h
    LrcParser.cpp
    LyricsLoader.h
    LyricsLoader.cpp
    SettingsService.h
    SettingsService.cpp
    CredentialStore.h
    CredentialStore.cpp
    LibraryService.h
    LibraryService.cpp
    DownloadService.h
    DownloadService.cpp
    PlayerService.h
    PlayerService.cpp
    PlaylistController.h
    PlaylistController.cpp
    SearchTypes.h
    SearchService.h
    SearchService.cpp
    SearchAggregator.h
    SearchAggregator.cpp
    SearchCache.h
    SearchCache.cpp
    NeteaseApiClient.h
    NeteaseApiClient.cpp
    QqMusicSource.h
    QqMusicSource.cpp
    ApiService.h
    ApiService.cpp
    QqApiService.h
    QqApiService.cpp
)

target_include_directories(neteclone_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(neteclone_core PUBLIC Qt6::Core Qt6::Sql Qt6::Multimedia Qt6::Network)

if(WIN32)
    target_link_libraries(neteclone_core PRIVATE Crypt32)
endif()

if(NETECLONE_HAVE_TAGLIB)
    target_compile_definitions(neteclone_core PUBLIC NETECLONE_HAVE_TAGLIB)
    target_link_libraries(neteclone_core PRIVATE TagLib::tag)
endif()
