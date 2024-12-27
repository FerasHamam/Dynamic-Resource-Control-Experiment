cmake_minimum_required(VERSION 3.10)

project(ZMQCLIENT C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(ZMQ REQUIRED libzmq)

add_executable(sender sender.c)

include_directories(${ZMQ_INCLUDE_DIRS})

target_link_libraries(sender ${ZMQ_LIBRARIES} pthread)
