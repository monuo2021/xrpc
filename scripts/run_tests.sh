#!/bin/bash
set -e

# 确保 ZooKeeper 正在运行
if ! nc -z 127.0.0.1 2181; then
    echo "Error: ZooKeeper is not running on 127.0.0.1:2181"
    exit 1
fi

# 创建构建目录
mkdir -p build
cd build

# 运行 CMake 和 Make
echo -e "Configuring build...\n"
cmake ..
echo -e "\nBuilding project...\n"
make

# 运行测试
echo -e "\nRunning tests...\n"
./bin/xrpc_tests

echo -e "\nTests completed."

echo -e "Delete the build directory\n"
cd .. && rm -rf build