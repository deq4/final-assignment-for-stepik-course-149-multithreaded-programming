cmake_minimum_required(VERSION 2.8)

project(stepik_course_149_final)

include_directories("asio-1.22.1/include/")
add_executable(final final.cpp)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
target_link_libraries(final rt)
