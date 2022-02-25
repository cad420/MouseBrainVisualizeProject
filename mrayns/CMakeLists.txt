include_directories(.)

file(
        GLOB
        MRAYNS_SRCS
        "*/*.hpp"
        "*/*.cpp"
)

include(${PROJECT_SOURCE_DIR}/deps/glm.cmake)
include(${PROJECT_SOURCE_DIR}/deps/spdlog.cmake)

add_library(MRAYNS_CORE STATIC ${MRAYNS_SRCS})

target_include_directories(
        MRAYNS_CORE
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/mrayns>
)

target_link_libraries(
        MRAYNS_CORE
        PUBLIC
        spdlog::spdlog
        glm::glm
)

target_compile_features(
        MRAYNS_CORE
        PRIVATE
        cxx_std_17
)