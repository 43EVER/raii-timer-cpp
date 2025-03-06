# 查找源文件
file(GLOB EXAMPLE_SOURCES "*.cpp")

# 为每个源文件创建一个可执行目标
foreach(source_file ${EXAMPLE_SOURCES})
    get_filename_component(example_name ${source_file} NAME_WE)
    add_executable(${example_name} ${source_file})
    target_link_libraries(${example_name} PRIVATE timekeeper pthread)
endforeach()
