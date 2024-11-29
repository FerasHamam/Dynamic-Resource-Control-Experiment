cmake_minimum_required(VERSION 3.10)


project(ZMQCLIENT C)


find_package(PkgConfig REQUIRED)


pkg_check_modules(ZMQ REQUIRED libzmq)


add_executable(client client.c)


include_directories(${ZMQ_INCLUDE_DIRS})


target_link_libraries(client ${ZMQ_LIBRARIES} pthread)
