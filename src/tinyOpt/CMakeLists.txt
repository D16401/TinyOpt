cmake_minimum_required(VERSION 3.25)

project(tinyOpt
    VERSION 0.1.0
    LANGUAGES CXX
)

add_library(optCtrl STATIC "${CMAKE_CURRENT_SOURCE_DIR}/optCtrl/optCtrl.cpp")
target_include_directories(optCtrl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/optCtrl")
target_compile_features(optCtrl PUBLIC cxx_std_17)

add_library(statistics STATIC "${CMAKE_CURRENT_SOURCE_DIR}/statistics/statistics.cpp")
target_include_directories(statistics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/statistics")
target_compile_features(statistics PUBLIC cxx_std_17)

add_library(formater STATIC "${CMAKE_CURRENT_SOURCE_DIR}/formater/formater.cpp")
target_link_libraries(formater PUBLIC statistics)
target_include_directories(formater PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/formater")
target_compile_features(formater PUBLIC cxx_std_17)

add_library(timer STATIC "${CMAKE_CURRENT_SOURCE_DIR}/timer/timer.cpp")
target_link_libraries(timer PUBLIC optCtrl formater statistics)
target_include_directories(timer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/timer")
target_compile_features(timer PUBLIC cxx_std_17)

add_library(testFunc STATIC "${CMAKE_CURRENT_SOURCE_DIR}/testFunc/testFunc.cpp")
target_link_libraries(testFunc PUBLIC optCtrl)
target_include_directories(testFunc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/testFunc")
target_compile_features(testFunc PUBLIC cxx_std_17)

#tinyOpt 接口库，集合上面三个库
add_library(tinyOpt INTERFACE)
target_link_libraries(tinyOpt INTERFACE optCtrl formater statistics timer testFunc)
target_include_directories(tinyOpt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")