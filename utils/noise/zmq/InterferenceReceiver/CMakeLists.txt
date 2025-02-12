cmake_minimum_required(VERSION 3.10)

project(ZMQ_RECEIVER_NOISE C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(ZMQ REQUIRED libzmq)

add_executable(receiver receiver.c)

include_directories(${ZMQ_INCLUDE_DIRS})

target_link_libraries(receiver ${ZMQ_LIBRARIES} pthread)