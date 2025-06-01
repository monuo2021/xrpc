cmake_minimum_required(VERSION 3.10)
project(XRPC VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_library(ZOOKEEPER_MT zookeeper_mt)

# 检查 Boost 是否找到
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost libraries not found. Please install Boost.")
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 包含目录
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_BINARY_DIR}/protos) # Protobuf 生成文件
include_directories(${Boost_INCLUDE_DIRS})

# 添加子目录
add_subdirectory(protos)
add_subdirectory(src)
add_subdirectory(examples)
add_subdirectory(tests)

# 安装配置文件
install(FILES configs/xrpc.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/etc/xrpc)