cmake_minimum_required(VERSION 3.21)
project(SnoppyWindows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

set(FFMPEG_ROOT "" CACHE PATH "Path to FFmpeg root (contains include, lib, bin)")
if(NOT FFMPEG_ROOT)
    message(FATAL_ERROR "Please specify -DFFMPEG_ROOT=... pointing to your FFmpeg folder.")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
set(FFMPEG_BIN_DIR "${FFMPEG_ROOT}/bin")

find_library(AVCODEC_LIB avcodec PATHS "${FFMPEG_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(AVFORMAT_LIB avformat PATHS "${FFMPEG_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(AVUTIL_LIB avutil PATHS "${FFMPEG_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)
find_library(SWSCALE_LIB swscale PATHS "${FFMPEG_LIB_DIR}" REQUIRED NO_DEFAULT_PATH)

add_executable(Snoppy WIN32
    src/main.cpp
    src/ScreenSaverApp.cpp
    src/LayeredVideoWindow.cpp
    src/FFmpegDecoder.cpp
    src/VideoPlaylist.cpp
)

target_include_directories(Snoppy PRIVATE
    src
    "${FFMPEG_INCLUDE_DIR}"
)

target_compile_definitions(Snoppy PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(Snoppy PRIVATE
    ${AVCODEC_LIB}
    ${AVFORMAT_LIB}
    ${AVUTIL_LIB}
    ${SWSCALE_LIB}
    user32
    gdi32
    comctl32
    shcore
)

set_target_properties(Snoppy PROPERTIES
    OUTPUT_NAME "Snoppy"
    SUFFIX ".scr"
)

add_custom_command(TARGET Snoppy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Snoppy>/video"
    COMMENT "Ensuring output video directory exists"
)

file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")
if(FFMPEG_DLLS)
    add_custom_command(TARGET Snoppy POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FFMPEG_DLLS}
            "$<TARGET_FILE_DIR:Snoppy>"
        COMMENT "Copying FFmpeg runtime DLLs"
    )
endif()
