cmake_minimum_required(VERSION 3.10)
project(iot-monitoring-system VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

option(BUILD_TESTS "Build test programs" OFF)

add_compile_options(-Wall -Wextra -g)

include_directories(
    ${CMAKE_SOURCE_DIR}/src/common
    ${CMAKE_SOURCE_DIR}/src/linux-app
    ${CMAKE_SOURCE_DIR}/src/drivers
)

file(GLOB COMMON_SOURCES "src/common/*.c")
file(GLOB LINUX_APP_SOURCES "src/linux-app/*.c")
file(GLOB DRIVER_SOURCES "src/drivers/*.c")

add_executable(iot-main
    ${LINUX_APP_SOURCES}
    ${COMMON_SOURCES}
    ${DRIVER_SOURCES}
)

target_link_libraries(iot-main m pthread rt)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
