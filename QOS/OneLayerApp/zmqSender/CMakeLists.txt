cmake_minimum_required(VERSION 3.10)

project(ZMQCLIENT C)

# Find pkg-config
find_package(PkgConfig REQUIRED)

# Find libzmq using pkg-config
pkg_check_modules(ZMQ REQUIRED libzmq)

# Find fftw3 using pkg-config
pkg_check_modules(FFTW REQUIRED fftw3)

# Find libpcap using pkg-config
pkg_check_modules(PCAP REQUIRED libpcap)

# Add the executable
add_executable(sender sender.c)

# Include directories
target_include_directories(sender PRIVATE 
    ${ZMQ_INCLUDE_DIRS}
    ${FFTW_INCLUDE_DIRS}
)

# Link libraries
target_link_directories(sender PRIVATE
    ${ZMQ_LIBRARY_DIRS}
    ${FFTW_LIBRARY_DIRS}
)

# Explicitly link against pcap
target_link_libraries(sender
    ${ZMQ_LIBRARIES}
    ${FFTW_LIBRARIES}
    pthread
    m
)