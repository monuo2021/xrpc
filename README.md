```
XRPC/
├── CMakeLists.txt              # 顶级 CMake 配置文件
├── LICENSE                     # 开源许可
├── README.md                   # 项目说明，包含使用指南
├── docs/                       # 文档目录
│   ├── api.md                  # API 文档
│   ├── design.md               # 设计文档
│   └── tutorial.md             # 使用教程
├── protos/                     # Protobuf 定义
│   ├── xrpc.proto              # XRPC 协议（类似 Krpcheader.proto）
│   ├── example/                # 示例服务定义
│   │   └── user_service.proto  # 用户服务（如 Login）
│   └── CMakeLists.txt          # Protobuf 编译规则
├── src/                        # 核心实现
│   ├── core/                   # 核心模块
│   │   ├── channel/            # 客户端通信
│   │   │   ├── xrpc_channel.h
│   │   │   └── xrpc_channel.cc
│   │   ├── controller/         # 状态和错误管理
│   │   │   ├── xrpc_controller.h
│   │   │   └── xrpc_controller.cc
│   │   ├── server/             # 服务端实现
│   │   │   ├── xrpc_server.h
│   │   │   └── xrpc_server.cc
│   │   ├── codec/              # 协议编解码
│   │   │   ├── xrpc_codec.h
│   │   │   └── xrpc_codec.cc
│   │   └── common/             # 公共工具
│   │       ├── xrpc_common.h
│   │       ├── xrpc_config.h
│   │       └── xrpc_logger.h
│   ├── registry/               # 服务注册与发现
│   │   ├── zookeeper_client.h
│   │   └── zookeeper_client.cc
│   ├── transport/              # 网络通信
│   │   ├── muduo_transport.h
│   │   └── muduo_transport.cc
│   └── CMakeLists.txt          # 核心模块编译规则
├── examples/                   # 示例代码
│   ├── client/                 # 客户端示例
│   │   ├── user_client.cc
│   │   └── CMakeLists.txt
│   ├── server/                 # 服务端示例
│   │   ├── user_server.cc
│   │   └── CMakeLists.txt
│   └── CMakeLists.txt
├── tests/                      # 单元测试
│   ├── channel_test.cc         # Channel 测试
│   ├── controller_test.cc      # Controller 测试
│   ├── server_test.cc          # Server 测试
│   ├── registry_test.cc        # Registry 测试
│   └── CMakeLists.txt
├── scripts/                    # 辅助脚本
│   ├── generate_protos.sh      # 生成 Protobuf 代码
│   └── run_tests.sh            # 运行测试
└── configs/                    # 配置文件
    └── xrpc.conf               # 默认配置（ZooKeeper 地址、日志级别等）
```

# XRPC

XRPC is a lightweight RPC framework for learning and prototyping, bridging the gap between educational projects and production-grade systems. It supports service discovery (ZooKeeper), long-lived connections, and asynchronous calls.

## Features
- Protobuf-based protocol
- ZooKeeper service registry
- Muduo-based networking
- Synchronous and asynchronous calls
- Error handling and cancellation

## Installation
1. Install dependencies:
   ```bash
   sudo apt-get install libprotobuf-dev protobuf-compiler libzookeeper-mt-dev
   git clone https://github.com/chenshuo/muduo && cd muduo && ./build.sh && sudo ./install.sh
   ```

2. Build XRPC:

    ```bash
    mkdir build && cd build
    cmake ..
    make
    ```

## Usage

Run the server:
```bash
./bin/user_server
```

Run the client:
```bash
./bin/user_client
```