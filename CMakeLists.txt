cmake_minimum_required(VERSION 3.10)
project(XRPC VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(ZLIB REQUIRED)
find_library(MUDUO_BASE muduo_base HINTS /usr/local/lib /usr/lib)
find_library(MUDUO_NET muduo_net HINTS /usr/local/lib /usr/lib)
find_library(ZOOKEEPER_MT zookeeper_mt)

# 检查 Muduo 是否找到
if(NOT MUDUO_BASE OR NOT MUDUO_NET)
    message(FATAL_ERROR "Muduo libraries not found. Please install Muduo.")
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含目录
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR}/protos) # Protobuf 生成文件

# 添加子目录
add_subdirectory(protos)
add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tests)

# 安装配置文件
install(FILES configs/xrpc.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/xrpc)