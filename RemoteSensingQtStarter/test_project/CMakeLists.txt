cmake_minimum_required(VERSION 3.16)
project(TestProject VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_run
    ${CMAKE_SOURCE_DIR}/../src/test.cpp
    ${CMAKE_SOURCE_DIR}/../include/rs/test.h
)

target_include_directories(test_run PRIVATE ${CMAKE_SOURCE_DIR}/../include)
