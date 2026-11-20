
cmake_minimum_required(VERSION 3.20)

project(Music VERSION 0.6.7 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Installation prefix" FORCE)
endif()

include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET gstreamer-1.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

add_executable(Music
    src/main.c
    src/application.c
    src/window.c
    src/player.c
    src/visualizer.c
    src/playlist.c
    src/config.c
    src/mpris.c
)

target_include_directories(Music PRIVATE include)

target_link_libraries(Music PRIVATE
    PkgConfig::GTK4
    PkgConfig::ADWAITA
    PkgConfig::GSTREAMER
    PkgConfig::GIO
)

target_compile_options(Music PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS Music RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES data/icons/hicolor/scalable/apps/io.github.sametbilir33.Music.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

install(FILES data/io.github.sametbilir33.Music.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES data/metainfo/io.github.sametbilir33.Music.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/Music)
