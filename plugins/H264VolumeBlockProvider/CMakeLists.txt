include(${PROJECT_SOURCE_DIR}/deps/VolumeCompression.cmake)

set(FFmpeg_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg/include)
set(
        FFmpeg_LIBS
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg/lib/avcodec.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/ffmpeg/lib/avutil.lib

)

file(
        GLOB
        H264VolumeBlockProviderPlugin_SRCS
        "src/*.hpp"
        "src/*.cpp"
)

add_library(H264VolumeBlockProviderPlugin SHARED ${H264VolumeBlockProviderPlugin_SRCS})

target_include_directories(
        H264VolumeBlockProviderPlugin
        PRIVATE
        ${FFmpeg_INCLUDE_DIR}
)
target_link_libraries(
        H264VolumeBlockProviderPlugin
        PRIVATE
        MRAYNS_CORE
        voxel_uncompress
        ${FFmpeg_LIBS}
)

target_compile_features(
        H264VolumeBlockProviderPlugin
        PRIVATE
        cxx_std_14
)
