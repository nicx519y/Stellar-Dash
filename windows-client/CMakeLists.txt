cmake_minimum_required(VERSION 3.24)
project(HBoxClient VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(HBOX_BUILD_DESKTOP "Build the Windows WebView2 desktop host" ON)
option(HBOX_INTERNAL_VIGEM_MVP "Enable the internal ViGEm MVP backend" ON)
option(HBOX_PUBLIC_RELEASE "Enforce public-release dependency gates" OFF)

if(HBOX_PUBLIC_RELEASE AND HBOX_INTERNAL_VIGEM_MVP)
  message(FATAL_ERROR "Public releases must use the signed HBox UMDF2 backend; ViGEm is internal-only")
endif()

add_library(hbox_client_core
  native/src/protocol.cpp
  native/src/runtime_snapshot.cpp
  native/src/stream_statistics.cpp
)
target_include_directories(hbox_client_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/native/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../common
)
target_compile_definitions(hbox_client_core PUBLIC
  $<$<BOOL:${HBOX_INTERNAL_VIGEM_MVP}>:HBOX_INTERNAL_VIGEM_MVP=1>
)

include(CTest)
if(BUILD_TESTING)
  add_executable(hbox_client_tests native/tests/core_tests.cpp)
  target_link_libraries(hbox_client_tests PRIVATE hbox_client_core)
  add_test(NAME hbox_client_tests COMMAND hbox_client_tests)
endif()

if(WIN32 AND HBOX_BUILD_DESKTOP)
  find_program(HBOX_NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
  file(GLOB_RECURSE HBOX_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/*
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/lib/*
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/styles/*
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/public/*
  )
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/ui/out/index.html
    COMMAND ${HBOX_NPM_EXECUTABLE} ci
    COMMAND ${HBOX_NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ui
    DEPENDS ${HBOX_UI_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/package.json
            ${CMAKE_CURRENT_SOURCE_DIR}/ui/package-lock.json
    COMMENT "Building the static HBox WebView2 UI"
  )
  add_custom_target(hbox_client_ui
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/out/index.html)

  include(FetchContent)
  FetchContent_Declare(webview2_sdk
    URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4191.47
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(webview2_sdk)

  add_executable(HBoxClient WIN32
    native/src/main.cpp
    native/src/hid_control_transport.cpp
    native/src/winusb_transport.cpp
    native/src/vigem_gamepad.cpp
    native/src/umdf_gamepad.cpp
    native/src/client_runtime.cpp
    native/src/webview_window.cpp
  )
  add_dependencies(HBoxClient hbox_client_ui)
  target_include_directories(HBoxClient PRIVATE
    ${webview2_sdk_SOURCE_DIR}/build/native/include
    ${CMAKE_CURRENT_SOURCE_DIR}/driver/include
  )
  target_compile_definitions(HBoxClient PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    HBOX_UI_DIST_DIR=L"${CMAKE_CURRENT_SOURCE_DIR}/ui/out"
  )
  target_compile_options(HBoxClient PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
  target_link_libraries(HBoxClient PRIVATE
    hbox_client_core
    ${webview2_sdk_SOURCE_DIR}/build/native/x64/WebView2LoaderStatic.lib
    cfgmgr32 setupapi hid winusb ole32 shell32 shlwapi user32 advapi32 avrt
  )
  install(TARGETS HBoxClient RUNTIME DESTINATION .)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ui/out/ DESTINATION ui)
endif()
