cmake_minimum_required(VERSION 3.28...3.30)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES C CXX)
include(compilerconfig)
include(defaults)
include(helpers)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "OZK Donations currently supports Windows x64 only")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 6.11.1 EXACT REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6WebSockets QUIET)
if(NOT Qt6WebSockets_FOUND)
  include(FetchContent)
  FetchContent_Declare(qtwebsockets
    GIT_REPOSITORY https://github.com/qt/qtwebsockets.git
    GIT_TAG 451920600d7f0b8a4b458bba56a2dd303e587026
    SOURCE_SUBDIR unused)
  FetchContent_MakeAvailable(qtwebsockets)
  set(_ws_prefix "${CMAKE_BINARY_DIR}/qtwebsockets-install")
  execute_process(COMMAND "${CMAKE_COMMAND}"
    -S "${qtwebsockets_SOURCE_DIR}" -B "${CMAKE_BINARY_DIR}/qtwebsockets-build"
    -G "${CMAKE_GENERATOR}" -A "${CMAKE_GENERATOR_PLATFORM}"
    "-DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DCMAKE_INSTALL_PREFIX=${_ws_prefix}"
    -DBUILD_WITH_PCH=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF
    COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}/qtwebsockets-build"
    --config Release --parallel COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}/qtwebsockets-build"
    --config Release COMMAND_ERROR_IS_FATAL ANY)
  list(PREPEND CMAKE_PREFIX_PATH "${_ws_prefix}")
  set(Qt6WebSockets_DIR "${_ws_prefix}/lib/cmake/Qt6WebSockets" CACHE PATH "" FORCE)
endif()
find_package(Qt6WebSockets 6.11.1 EXACT REQUIRED)

add_library(${CMAKE_PROJECT_NAME} MODULE
  src/plugin.cpp src/dock.cpp src/dock.hpp src/feed.cpp src/feed.hpp
  src/credentials.cpp src/credentials.hpp src/donation_cache.cpp src/donation_cache.hpp
  src/oauth_loopback.hpp src/brands.qrc
  src/youtube_links.cpp src/youtube_links.hpp src/youtube_player.cpp src/youtube_player.hpp)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets crypt32)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QT_NO_KEYWORDS WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4 /utf-8 /permissive-)
endif()
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
install(FILES "$<TARGET_FILE:Qt6::WebSockets>" DESTINATION "${CMAKE_PROJECT_NAME}/bin/64bit")
get_filename_component(_qt_runtime_root "${Qt6_DIR}/../../.." ABSOLUTE)
find_file(_schannel_backend qschannelbackend.dll
  PATHS "${_qt_runtime_root}/plugins/tls" NO_DEFAULT_PATH REQUIRED)
install(FILES "${_schannel_backend}" DESTINATION "${CMAKE_PROJECT_NAME}/data/qt-plugins/tls")

enable_testing()
add_executable(ozk-donations-tests tests/test_feed.cpp src/feed.cpp src/credentials.cpp src/donation_cache.cpp)
get_target_property(_obs_includes OBS::libobs INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(ozk-donations-tests PRIVATE src ${_obs_includes})
target_link_libraries(ozk-donations-tests PRIVATE Qt6::Core Qt6::Network Qt6::WebSockets crypt32)
target_compile_definitions(ozk-donations-tests PRIVATE QT_NO_KEYWORDS WIN32_LEAN_AND_MEAN NOMINMAX OZK_DONATIONS_TEST)
if(MSVC)
  target_compile_options(ozk-donations-tests PRIVATE /UNDEBUG /utf-8)
endif()
add_test(NAME donation-feed COMMAND ozk-donations-tests)
set_tests_properties(donation-feed PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::WebSockets>")

add_executable(ozk-youtube-links-tests tests/test_youtube_links.cpp src/youtube_links.cpp)
target_include_directories(ozk-youtube-links-tests PRIVATE src)
target_link_libraries(ozk-youtube-links-tests PRIVATE Qt6::Core)
target_compile_definitions(ozk-youtube-links-tests PRIVATE QT_NO_KEYWORDS)
if(MSVC)
  target_compile_options(ozk-youtube-links-tests PRIVATE /UNDEBUG /utf-8)
endif()
add_test(NAME youtube-links COMMAND ozk-youtube-links-tests)
set_tests_properties(youtube-links PROPERTIES
  ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_executable(ozk-donations-ui-preview tests/ui_preview.cpp
  src/dock.cpp src/dock.hpp src/feed.cpp src/feed.hpp src/credentials.cpp src/credentials.hpp
  src/donation_cache.cpp src/donation_cache.hpp src/youtube_links.cpp src/youtube_links.hpp
  src/youtube_player.hpp src/oauth_loopback.hpp src/brands.qrc)
target_include_directories(ozk-donations-ui-preview PRIVATE src)
target_link_libraries(ozk-donations-ui-preview PRIVATE
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::WebSockets crypt32)
target_compile_definitions(ozk-donations-ui-preview PRIVATE
  OZK_DONATIONS_TEST OZK_DONATIONS_PREVIEW QT_NO_KEYWORDS WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
  target_compile_options(ozk-donations-ui-preview PRIVATE /utf-8 /permissive-)
endif()
