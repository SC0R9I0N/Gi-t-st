cmake_minimum_required(VERSION 3.16)

project(gitst
    VERSION 1.0.0
    DESCRIPTION "Gi(t/st) - native interactive Git branch history visualizer"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Gi(t/st) is a native Windows GUI application (Win32 + GDI+ + WinHTTP).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_compile_definitions(
    UNICODE _UNICODE
    NOMINMAX WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00 _WIN32_WINNT=0x0A00)

# ---------------------------------------------------------------------------
# Dependencies — only nlohmann/json (header-only) is fetched. Everything else
# (GDI+, WinHTTP, Win32) is a Windows system library, so the result is a single
# self-contained executable with no third-party DLLs to ship.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Backend (platform-agnostic logic + WinHTTP client)
# ---------------------------------------------------------------------------
add_library(gitst_core STATIC
    src/HttpClient.cpp
    src/TimeUtil.cpp
    src/RepoUrl.cpp
    src/ColorPalette.cpp
    src/Provider.cpp
    src/GraphBuilder.cpp
    src/DemoData.cpp
    src/Fetcher.cpp)

target_include_directories(gitst_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gitst_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE winhttp)

# ---------------------------------------------------------------------------
# GUI executable (WIN32 == no console window)
# ---------------------------------------------------------------------------
add_executable(gitst WIN32
    src/main.cpp
    src/win/Gui.cpp
    src/win/GraphView.cpp
    src/win/DetailsPanel.cpp
    resources/app.rc)

target_include_directories(gitst PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gitst PRIVATE
    gitst_core
    gdiplus gdi32 user32 shell32 comctl32 ole32 winhttp)

set_target_properties(gitst PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MinGW: static-link the runtimes so the .exe truly stands alone.
if(MINGW)
    target_link_options(gitst PRIVATE -static -static-libgcc -static-libstdc++)
endif()
