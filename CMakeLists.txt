cmake_minimum_required(VERSION 3.12)
project(timekeeper VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件库不需要编译，只需定义一个接口目标
add_library(timekeeper INTERFACE)

# 配置头文件路径
target_include_directories(timekeeper
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 安装规则
install(TARGETS timekeeper
    EXPORT timekeeper-targets
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT timekeeper-targets
    FILE timekeeper-config.cmake
    NAMESPACE timekeeper::
    DESTINATION lib/cmake/timekeeper
)

# 构建示例
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
