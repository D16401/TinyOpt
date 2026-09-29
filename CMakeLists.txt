cmake_minimum_required(VERSION 3.25)

project(
    TinyOpt
    LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD 20)

# --- Compiler check ---
if(NOT MSVC)
    message(FATAL_ERROR "Only Using MSVC compiler")
endif()
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# --- Add libs ---
set(libs "ezlogger" "tinyOpt")
add_subdirectory("${CMAKE_SOURCE_DIR}/src")

set(app "main")
# --- Release模式 ---
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SRT_OUTPUT_DIR  "${CMAKE_SOURCE_DIR}/release")
# --- Debug模式 ---
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(SRT_OUTPUT_DIR  "${CMAKE_SOURCE_DIR}/debug/${app}/")
else()
    message(FATAL_ERROR "Unknown build type")
endif()
# --- Set output dir ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SRT_OUTPUT_DIR}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${SRT_OUTPUT_DIR}")
endforeach()

# --- build ---
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    if(ENABLE_RELEASE_PDB)#需要在add_executable前设置
        add_compile_options("$<$<CONFIG:Release>:/Zi>")
        add_link_options("$<$<CONFIG:Release>:/DEBUG>")
    endif()
    add_executable(${app} "${app}.cpp")
    foreach(lib IN LISTS libs)
        target_link_libraries(${app} PRIVATE ${lib})
    endforeach()
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(${app} "${app}.cpp")
    foreach(lib IN LISTS libs)
        target_link_libraries(${app} PRIVATE ${lib})
    endforeach()
endif()

message("OutputDir:${SRT_OUTPUT_DIR}")



