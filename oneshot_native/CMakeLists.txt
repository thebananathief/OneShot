cmake_minimum_required(VERSION 3.28)

project(OneShotNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(oneshot WIN32
    src/main.cpp
    src/AppHost.cpp
    src/CaptureService.cpp
    src/CommandClient.cpp
    src/CommandProtocol.cpp
    src/CommandServer.cpp
    src/DiagnosticsService.cpp
    src/DragDropService.cpp
    src/MarkupEditorWindow.cpp
    src/NotificationManager.cpp
    src/OverlayManager.cpp
    src/OutputService.cpp
    src/PathService.cpp
    src/StartupService.cpp
    src/TempFileManager.cpp
    src/TrayController.cpp
)

target_include_directories(oneshot PRIVATE include)

target_compile_definitions(oneshot PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    ONESHOT_VERSION=L"0.1.0"
)

target_link_libraries(oneshot PRIVATE
    comctl32
    comdlg32
    gdi32
    msimg32
    windowscodecs
    ole32
    shell32
    shlwapi
    user32
    advapi32
)

set_target_properties(oneshot PROPERTIES OUTPUT_NAME "oneshot")
