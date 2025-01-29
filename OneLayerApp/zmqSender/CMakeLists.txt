cmake_minimum_required(VERSION 3.10)

project(ZMQCLIENT C)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find libzmq using pkg-config
pkg_check_modules(ZMQ REQUIRED libzmq)

# Find fftw3 using pkg-config
pkg_check_modules(FFTW REQUIRED fftw3)

# Add the executable
add_executable(sender sender.c)

# Include directories for ZMQ and FFTW using pkg-config paths
include_directories(${ZMQ_INCLUDE_DIRS})
include_directories(${FFTW_INCLUDE_DIRS})

# Link ZMQ, pthread, and FFTW libraries
target_link_libraries(sender ${ZMQ_LIBRARIES} pthread ${FFTW_LIBRARIES} m)