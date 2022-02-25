cmake_minimum_required(VERSION 3.16)

project(MouseBrainVisualizeProject LANGUAGES CXX)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

include_directories(${PROJECT_SOURCE_DIR}/deps)

add_subdirectory(mrayns)

add_subdirectory(plugins)

add_subdirectory(tests)

add_subdirectory(apps)