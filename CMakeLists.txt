cmake_minimum_required (VERSION 3.8)
project ("mediaprocessor")

add_library (mediaprocessor STATIC   "src/transcode.h" "src/transcode.cpp" "src/util.h" "src/util.cpp" "include/mediaprocessor.h" "src/context.cpp" "src/extract.h" "src/extract.cpp")
target_link_libraries(mediaprocessor avcodec avdevice avfilter avformat avutil swresample swscale)
target_include_directories(mediaprocessor PUBLIC  
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>  
    $<INSTALL_INTERFACE:include>
)