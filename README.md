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
│   │       ├── xrpc_config.cc
│   │       ├── xrpc_logger.h
│   │       └── xrpc_logger.cc
│   ├── registry/               # 服务注册与发现
│   │   ├── zookeeper_client.h
│   │   └── zookeeper_client.cc
│   ├── transport/              # 网络通信
│   │   ├── asio_transport.h
│   │   └── asio_transport.cc
│   └── CMakeLists.txt          # 核心模块编译规则
├── examples/                   # 示例代码
│   ├── client/                 # 客户端示例
│   │   └── user_client.cc
│   ├── server/                 # 服务端示例
│   │   └── user_server.cc
│   └── CMakeLists.txt
├── tests/                      # 单元测试
│   ├── channel_test.cc         # Channel 测试
│   ├── controller_test.cc      # Controller 测试
│   ├── config_codec_test.cc    # codec 测试
│   ├── server_test.cc          # Server 测试
│   ├── registry_test.cc        # Registry 测试
│   ├── main.cc                 # 测试主函数
│   ├── user_service_test.cc    # user_service 测试
│   └── CMakeLists.txt
├── scripts/                    # 辅助脚本
│   └── run_tests.sh            # 运行测试
└── configs/                    # 配置文件
    └── xrpc.conf               # 默认配置（ZooKeeper 地址、日志级别等）
```

# XRPC

XRPC is a lightweight RPC framework designed for learning and prototyping, bridging the gap between educational projects and production-grade systems. It supports service registration/discovery (ZooKeeper), long-lived connections, asynchronous calls, and extensible protocols.

## Features
- **Protobuf-based**: Uses Protocol Buffers for service definitions and communication.
- **Service Discovery**: Integrates with ZooKeeper for dynamic service registration and discovery.
- **Long Connections**: Supports persistent TCP connections via Boost.Asio.
- **Asynchronous Calls**: Allows non-blocking RPC calls with callbacks.
- **Extensible**: Supports metadata, compression, and interceptors.

## Prerequisites
- CMake 3.10+
- C++11 compiler (g++, clang++)
- Boost.Asio network library
- Protocol Buffers
- ZooKeeper C client
- spdlog (optional, for logging)

## Build Instructions
```bash
mkdir build && cd build
cmake ..
make
```

## Configuration
Edit configs/xrpc.conf to set ZooKeeper address, log level, etc.

## Examples

Run the server:
```bash
./bin/user_server --config=configs/xrpc.conf
```

Run the client:
```bash
./bin/user_client --config=configs/xrpc.conf
```

## Directory Structure

- protos/: Protocol Buffer definitions.
- src/: Core implementation (channel, server, registry, transport).
- examples/: Client and server examples.
- tests/: Unit tests.
- docs/: API, design, and tutorial documentation.
- configs/: Configuration files.