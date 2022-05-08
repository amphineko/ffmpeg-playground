cmake_minimum_required(VERSION 3.21)
project(audio_transcode)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_COMPILER gcc-11)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++-11)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswresample)

set(CMAKE_CXX_FLAGS "-D__STDC_CONSTANT_MACROS")

add_executable(transcode transcode/audio_transcode.cpp)
target_compile_options(transcode PUBLIC ${LIBAV_CFLAGS_OTHER})
target_link_libraries(transcode PRIVATE PkgConfig::LIBAV)
target_include_directories(transcode PRIVATE ${LIBAV_INCLUDE_DIRS})
