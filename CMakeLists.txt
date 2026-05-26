cmake_minimum_required(VERSION 3.20)
project(AcessoRemoto LANGUAGES CXX)

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

add_executable(AcessoRemoto WIN32
    src/main.cpp
    src/screencap.cpp
    src/inputhandler.cpp
)

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
