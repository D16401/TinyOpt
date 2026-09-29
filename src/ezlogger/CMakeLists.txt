cmake_minimum_required(VERSION 3.25)

project(ezlogger
    VERSION 0.1.0
    LANGUAGES CXX
)

add_library(ezlogger ezlogger.cpp)

target_include_directories(
    ezlogger
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   
)

#CPP17
target_compile_features(
    ezlogger
    PUBLIC
    cxx_std_17
)