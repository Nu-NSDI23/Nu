cmake_minimum_required(VERSION 3.5)
project(social_network_microservices)

set(CMAKE_C_COMPILER gcc-13)
set(CMAKE_CXX_COMPILER g++-13)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-O3 -g -std=c++23 -flto=auto -march=native")
set(CMAKE_INSTALL_PREFIX /usr/local/bin)

add_subdirectory(src)

