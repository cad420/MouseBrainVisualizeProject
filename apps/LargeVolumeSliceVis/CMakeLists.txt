
include(${PROJECT_SOURCE_DIR}/deps/sdl2.cmake)

add_executable(LargeVolumeSliceVis main.cpp)

target_link_libraries(
        LargeVolumeSliceVis
        PRIVATE
        MRAYNS_CORE
        SDL2-static
        SDL2main
)

target_compile_features(
        LargeVolumeSliceVis
        PRIVATE
        cxx_std_17
)