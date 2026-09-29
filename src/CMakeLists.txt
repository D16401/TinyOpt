#父目录必须已经定义libs
if(DEFINED MY_VAR)
    message(FATAL_ERROR "变量libs未被定义")
endif()
foreach(lib IN LISTS libs)
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${lib}")
        message(FATAL_ERROR "库目录不存在: ${lib}")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${lib}/CMakeLists.txt")
        message(FATAL_ERROR
            "找不到库目录CMakeLists.txt: ${CMAKE_CURRENT_SOURCE_DIR}/${lib} ")
    endif()
    add_subdirectory(${lib})
endforeach()

