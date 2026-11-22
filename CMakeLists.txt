# SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
#
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.21)
project(flimmer VERSION 0.1.0 LANGUAGES CXX)

option(
    USE_SYSTEM_BIGSCREEN_UI
    "Use the installed Bigscreen UI instead of building it with Flimmer"
    ON
)
option(
    FLIMMER_DEV_TOOLS
    "Build the scripted UI driver used for screenshots/tests"
    OFF
)
option(BUILD_TESTING "Build the unit tests" ON)

# Keep debug symbols by default so crash backtraces are useful.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Don't export template instances from the executable. Otherwise libraries that
# carry their own copies (e.g. QtKeychain built against an older Qt) are bound
# to ours at runtime, which crashed in QList<QVariant> code when talking to
# KWallet.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Kirigami, Bigscreen UI and the Plasma Qt Quick Controls style are only used
# from QML, so they are runtime dependencies (Ubuntu:
# qml6-module-org-kde-kirigami, qml6-module-org-kde-desktop, kde-style-breeze,
# qt6-svg-plugins; Arch: kirigami, bigscreen-ui, qqc2-desktop-style, breeze,
# qt6-svg).
find_package(
    Qt6
    6.5
    REQUIRED
    COMPONENTS Widgets Quick QuickControls2 Network Sql Multimedia DBus
)
find_package(Qt6Keychain REQUIRED)

include(GNUInstallDirs)

if(USE_SYSTEM_BIGSCREEN_UI)
    find_path(
        BIGSCREEN_UI_QML_DIR
        org/kde/bigscreen/ui/qmldir
        PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}"
        ENV QML_IMPORT_PATH
        ENV QML2_IMPORT_PATH
        NO_DEFAULT_PATH
    )

    if(NOT BIGSCREEN_UI_QML_DIR)
        message(
            FATAL_ERROR
            "Bigscreen UI (org.kde.bigscreen.ui) is not installed. Install "
            "it (Arch: packaging/arch/bigscreen-ui) or build it with Flimmer "
            "using -DUSE_SYSTEM_BIGSCREEN_UI=OFF."
        )
    endif()

    message(
        STATUS
        "Using the installed Bigscreen UI from ${BIGSCREEN_UI_QML_DIR}"
    )
else()
    # Build Bigscreen UI from the submodule and install it into a private QML
    # directory. This needs ECM and the Kirigami and KI18n development files.
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bigscreen-ui/CMakeLists.txt")
        message(
            FATAL_ERROR
            "The bigscreen-ui submodule is missing. Run "
            "\"git submodule update --init\" or use an installed Bigscreen UI "
            "with -DUSE_SYSTEM_BIGSCREEN_UI=ON."
        )
    endif()

    set(FLIMMER_QML_DIR "${CMAKE_INSTALL_LIBDIR}/flimmer/qml")
    # Where main.cpp looks for the bundled QML module, relative to the
    # executable.
    file(
        RELATIVE_PATH
        FLIMMER_QML_DIR_FROM_BIN
        "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}"
        "${CMAKE_INSTALL_PREFIX}/${FLIMMER_QML_DIR}"
    )

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(KDE_INSTALL_QMLDIR "${FLIMMER_QML_DIR}" CACHE PATH "" FORCE)
    # Keep our default install prefix; KDEInstallDirs would replace it with
    # ECM's.
    set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)

    # Install with the app's layout; setting these also keeps ECM from warning
    # about it.
    foreach(
        dir
        BINDIR
        SBINDIR
        LIBDIR
        LIBEXECDIR
        INCLUDEDIR
        DATAROOTDIR
        DATADIR
        LOCALEDIR
        MANDIR
        INFODIR
        SYSCONFDIR
        LOCALSTATEDIR
        SHAREDSTATEDIR
    )
        set(KDE_INSTALL_${dir} "${CMAKE_INSTALL_${dir}}")
    endforeach()

    add_subdirectory(bigscreen-ui)
endif()

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(flimmer src/main.cpp)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(flimmer
    URI Flimmer
    VERSION 1.0
    # Shared Qt build: QML plugins (incl. Kirigami) are loaded at runtime.
    NO_IMPORT_SCAN
    SOURCES
        src/serverconfig.h src/serverconfig.cpp
        src/colorschemes.h src/colorschemes.cpp
        src/networkauth.h src/networkauth.cpp
        src/appsettings.h src/appsettings.cpp
        src/tvhapi.h src/tvhapi.cpp
        src/m3u.h src/m3u.cpp
        src/xmltv.h src/xmltv.cpp
        src/streambuffer.h src/streambuffer.cpp
        src/streamproxy.h src/streamproxy.cpp
        src/epgdatabase.h src/epgdatabase.cpp
        src/epgstore.h src/epgstore.cpp
        src/channelmodel.h src/channelmodel.cpp
        src/sources/sourceconfig.h src/sources/sourceconfig.cpp
        src/sources/source.h src/sources/source.cpp
        src/sources/channelid.h src/sources/channelid.cpp
        src/sources/secretstore.h src/sources/secretstore.cpp
        src/sources/sourcesettings.h src/sources/sourcesettings.cpp
        src/sources/tvheadendsource.h src/sources/tvheadendsource.cpp
        src/sources/m3usource.h src/sources/m3usource.cpp
        src/tvhbackend.h src/tvhbackend.cpp
        src/screensaverinhibitor.h src/screensaverinhibitor.cpp
        src/logoimageprovider.h src/logoimageprovider.cpp
        src/inputdebug.h src/inputdebug.cpp
        src/remotekeys.h src/remotekeys.cpp
        src/streamrecorder.h src/streamrecorder.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/NavBar.qml
        qml/ChannelsPage.qml
        qml/GuidePage.qml
        qml/SettingsPage.qml
        qml/SourceEditor.qml
        qml/TvButton.qml
        qml/TextEntryDialog.qml
        qml/EventDialog.qml
        qml/PlayerView.qml
        qml/WatchOverlay.qml
        qml/ChannelLogo.qml
        qml/ThinProgress.qml
    RESOURCES
        data/icons/hicolor/256x256/apps/flimmer.png
        data/icons/symbolic/behind-live.svg
)

target_include_directories(flimmer PRIVATE src src/sources)
target_link_libraries(
    flimmer
    PRIVATE
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
        Qt6::Sql
        Qt6::Multimedia
        Qt6::DBus
        Qt6Keychain::Qt6Keychain
)

if(NOT USE_SYSTEM_BIGSCREEN_UI)
    target_compile_definitions(
        flimmer
        PRIVATE FLIMMER_QML_DIR="${FLIMMER_QML_DIR_FROM_BIN}"
    )
    add_dependencies(flimmer bigscreenuiplugin)
endif()

if(FLIMMER_DEV_TOOLS)
    # Simulating application focus changes needs QWindowSystemInterface.
    set(QT_NO_PRIVATE_MODULE_WARNING ON)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_sources(flimmer PRIVATE src/devscript.h src/devscript.cpp)
    target_compile_definitions(flimmer PRIVATE FLIMMER_DEV_TOOLS)
    target_link_libraries(flimmer PRIVATE Qt6::GuiPrivate)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS flimmer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(
    FILES data/io.github.jclsn.Flimmer.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
