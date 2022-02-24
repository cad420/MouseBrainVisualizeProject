include(${PROJECT_SOURCE_DIR}/deps/VolumeCompression.cmake)
file(
        GLOB
        H264VolumeBlockProviderPlugin_SRCS
        "*.hpp"
        "*.cpp"
)

add_library(H264VolumeBlockProviderPlugin SHARED ${H264VolumeBlockProviderPlugin_SRCS})

target_link_libraries(
        H264VolumeBlockProviderPlugin
        PRIVATE
        MRAYNS_CORE
        voxel_uncompress
)
