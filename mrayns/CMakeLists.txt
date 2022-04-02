include_directories(.)

file(
        GLOB
        MRAYNS_SRCS
        "*/*.hpp"
        "*/*.cpp"
        "*/*/*.cpp"
        "*/*/*.hpp"
)

include(${PROJECT_SOURCE_DIR}/deps/glm.cmake)
include(${PROJECT_SOURCE_DIR}/deps/spdlog.cmake)

find_package(Vulkan REQUIRED)

add_library(MRAYNS_CORE STATIC ${MRAYNS_SRCS})

target_include_directories(
        MRAYNS_CORE
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/mrayns>
        PRIVATE
        ${Vulkan_INCLUDE_DIR}
)

target_link_libraries(
        MRAYNS_CORE
        PUBLIC
        spdlog::spdlog
        glm::glm
        PRIVATE
        ${Vulkan_LIBRARY}
        glfw
)

target_compile_features(
        MRAYNS_CORE
        PRIVATE
        cxx_std_17
)