cmake_minimum_required(VERSION 3.20)
project(AcessoRemoto LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /MP /utf-8)
    add_definitions(
        -D_WIN32_WINNT=0x0601
        -DWIN32_LEAN_AND_MEAN
        -DNOMINMAX
        -D_CRT_SECURE_NO_WARNINGS
    )
endif()

# ─── Step 1: gen_icon utility (console, no WIN32) ─────────────────────────────
add_executable(gen_icon tools/gen_icon.cpp)
target_link_libraries(gen_icon PRIVATE gdiplus gdi32 user32 ole32)
if(MSVC)
    target_link_options(gen_icon PRIVATE /SUBSYSTEM:CONSOLE)
endif()
set_target_properties(gen_icon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    OUTPUT_NAME "gen_icon"
)

# ─── Step 2: generate assets/umbrelaviewer.ico at build time ─────────────────
set(APP_ICO "${CMAKE_SOURCE_DIR}/assets/umbrelaviewer.ico")
add_custom_command(
    OUTPUT  "${APP_ICO}"
    COMMAND "$<TARGET_FILE:gen_icon>" "${APP_ICO}"
    DEPENDS gen_icon
    COMMENT "Generating application icon (${APP_ICO})..."
    VERBATIM
)
add_custom_target(AppIcon DEPENDS "${APP_ICO}")

# ─── Step 3: main executable ──────────────────────────────────────────────────
add_executable(AcessoRemoto WIN32
    src/main.cpp
    src/screencap.cpp
    src/inputhandler.cpp
    src/resources.rc          # embeds the ICO into the EXE
)
add_dependencies(AcessoRemoto AppIcon)   # ensure ICO exists before RC compile

target_include_directories(AcessoRemoto PRIVATE src)

target_link_libraries(AcessoRemoto PRIVATE
    ws2_32
    gdiplus
    gdi32
    user32
    shell32
    comctl32
    comdlg32
    urlmon
    wtsapi32
)

if(MSVC)
    target_link_options(AcessoRemoto PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:WinMainCRTStartup)
endif()

set_target_properties(AcessoRemoto PROPERTIES OUTPUT_NAME "UmbrelaViewer")
