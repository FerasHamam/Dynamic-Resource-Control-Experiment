cmake_minimum_required(VERSION 3.10)

project(Receiver LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR})

find_package(PkgConfig REQUIRED)

pkg_check_modules(ZMQ REQUIRED libzmq)

add_executable(receiver receiver.c)

target_link_libraries(receiver ${ZMQ_LIBRARIES})

target_include_directories(receiver PRIVATE ${ZMQ_INCLUDE_DIRS})

target_compile_options(receiver PRIVATE ${ZMQ_CFLAGS_OTHER})