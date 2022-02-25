
include(${PROJECT_SOURCE_DIR}/deps/sdl2.cmake)

add_executable(LargeVolumeVis main.cpp)

target_link_libraries(
        LargeVolumeVis
        PRIVATE
        MRAYNS_CORE
        SDL2-static
        SDL2main
)

target_compile_features(
        LargeVolumeVis
        PRIVATE
        cxx_std_17
)