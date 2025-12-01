
cmake_minimum_required(VERSION 3.16)
project(mini2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find packages with proper handling
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

add_subdirectory(src/cpp)
enable_testing()
