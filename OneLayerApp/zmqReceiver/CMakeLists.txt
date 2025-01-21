cmake_minimum_required(VERSION 3.10)
project(zmq_receiver C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_library(ZMQ_LIB zmq)

# Check if ZMQ was found
if(NOT ZMQ_LIB)
    message(FATAL_ERROR "ZeroMQ library not found")
endif()

# Add include directories
include_directories(${CMAKE_SOURCE_DIR})

# Create executable
add_executable(receiver
    receiver.c
    step_manager.c
)

# Link libraries
target_link_libraries(receiver
    PRIVATE
    ${ZMQ_LIB}
    Threads::Threads
)

# Add compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(receiver PRIVATE -Wall -Wextra -Wpedantic)
endif()
