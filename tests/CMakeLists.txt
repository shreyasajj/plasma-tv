# SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
#
# SPDX-License-Identifier: BSD-2-Clause

find_package(Qt6 REQUIRED COMPONENTS Test Qml)

# Each test builds the sources it needs, so it runs without the QML module and
# without a server.
function(flimmer_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
    qt_add_executable(${name} ${name}.cpp ${arg_SOURCES})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${name} PRIVATE Qt6::Test ${arg_LIBRARIES})
    add_test(NAME ${name} COMMAND ${name})
    # No display or user settings are touched.
    set_tests_properties(
        ${name}
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endfunction()

flimmer_add_test(
    test_serverconfig
    SOURCES ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_m3u
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/m3u.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_xmltv
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/xmltv.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_epgstore
    SOURCES ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_channelmodel
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/channelmodel.h
        ${PROJECT_SOURCE_DIR}/src/channelmodel.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core Qt6::Qml
)

flimmer_add_test(
    test_epgdatabase
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/epgdatabase.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core Qt6::Sql
)

flimmer_add_test(
    test_streambuffer
    SOURCES ${PROJECT_SOURCE_DIR}/src/streambuffer.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_streamproxy
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/streamproxy.cpp
        ${PROJECT_SOURCE_DIR}/src/streambuffer.cpp
        ${PROJECT_SOURCE_DIR}/src/networkauth.cpp
        ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
    LIBRARIES Qt6::Core Qt6::Network
)

flimmer_add_test(
    test_sourceconfig
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/sources/sourceconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_channelid
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/sources/channelid.cpp
    LIBRARIES Qt6::Core
)

flimmer_add_test(
    test_sourcesettings
    SOURCES
        ${PROJECT_SOURCE_DIR}/src/sources/sourcesettings.h
        ${PROJECT_SOURCE_DIR}/src/sources/sourcesettings.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/secretstore.h
        ${PROJECT_SOURCE_DIR}/src/sources/secretstore.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/sourceconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/networkauth.cpp
    LIBRARIES Qt6::Core Qt6::Network Qt6::Qml Qt6Keychain::Qt6Keychain
)

flimmer_add_test(
    test_m3usource
    SOURCES
        fakeserver.h
        ${PROJECT_SOURCE_DIR}/src/sources/m3usource.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/source.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/headers.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/sourceconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/m3u.cpp
        ${PROJECT_SOURCE_DIR}/src/tvhapi.cpp
        ${PROJECT_SOURCE_DIR}/src/networkauth.cpp
        ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/xmltv.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core Qt6::Network
)

flimmer_add_test(
    test_xtream
    SOURCES
        fakeserver.h
        ${PROJECT_SOURCE_DIR}/src/sources/xtream.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/xtreamsource.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/source.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/headers.cpp
        ${PROJECT_SOURCE_DIR}/src/sources/sourceconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/tvhapi.cpp
        ${PROJECT_SOURCE_DIR}/src/networkauth.cpp
        ${PROJECT_SOURCE_DIR}/src/serverconfig.cpp
        ${PROJECT_SOURCE_DIR}/src/xmltv.cpp
        ${PROJECT_SOURCE_DIR}/src/epgstore.cpp
    LIBRARIES Qt6::Core Qt6::Network
)
target_compile_definitions(
    test_xtream
    PRIVATE FLIMMER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data"
)
