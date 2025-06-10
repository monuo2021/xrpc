
# XRPC

XRPC 是一个轻量级 RPC 框架，专为学习和原型开发设计，结合了 gRPC 的现代特性和 KRPC 的简洁性。它支持服务注册与发现（基于 ZooKeeper）、长连接（基于 Boost.Asio）、异步调用和请求取消。

## 特性

- **基于 Protobuf**：使用 Protocol Buffers 定义服务和消息。
- **服务发现**：通过 ZooKeeper 实现动态服务注册和发现。
- **长连接**：基于 Boost.Asio，支持持久化 TCP 连接。
- **异步调用**：支持非阻塞 RPC 调用，带回调机制。
- **请求取消**：支持客户端取消请求，服务端响应取消状态。
- **可扩展**：支持元数据、压缩和拦截器。

## 前置条件

- CMake 3.10+
- C++11 编译器（g++ 或 clang++）
- Boost.Asio
- Protocol Buffers
- ZooKeeper C 客户端
- spdlog（可选，用于日志）

## 构建说明

1. 克隆仓库：

   ```bash
   git clone <repository-url>
   cd XRPC
   ```

2. 构建项目：

   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

3. 安装（可选）：

   ```bash
   make install
   ```

## 配置

编辑 `configs/xrpc.conf` 设置 ZooKeeper 地址、日志级别等：

```ini
zookeeper_ip=127.0.0.1
zookeeper_port=2181
zookeeper_timeout_ms=6000
server_ip=0.0.0.0
server_port=8080
log_level=debug
log_file=xrpc.log
```

## 示例

### 运行服务端

```bash
./bin/user_server
```

### 运行客户端

同步调用：

```bash
./bin/user_client --sync
```

异步调用（多线程）：

```bash
./bin/user_client --async --threads 5
```

## 目录结构

```
XRPC/
├── CMakeLists.txt              # 顶级 CMake 配置文件
├── LICENSE                     # MIT 许可证
├── README.md                   # 项目说明
├── docs/                       # 文档目录
│   ├── api.md                  # API 参考
│   ├── design.md               # 设计原理
│   └── tutorial.md             # 使用指南
├── protos/                     # Protobuf 定义
│   ├── xrpc.proto              # XRPC 协议
│   ├── example/                # 示例服务
│   │   └── user_service.proto
│   └── CMakeLists.txt
├── src/                        # 核心实现
│   ├── core/                   # 核心模块
│   │   ├── channel/
│   │   ├── controller/
│   │   ├── server/
│   │   ├── codec/
│   │   └── common/
│   ├── registry/               # 服务注册与发现
│   ├── transport/              # 网络通信
│   └── CMakeLists.txt
├── examples/                   # 示例代码
│   ├── client/                 # 客户端示例
│   │   └── user_client.cc
│   ├── server/                 # 服务端示例
│   │   └── user_server.cc
│   └── CMakeLists.txt
├── tests/                      # 单元测试
│   ├── channel_test.cc
│   ├── controller_test.cc
│   ├── config_codec_test.cc
│   ├── server_test.cc
│   ├── registry_test.cc
│   ├── user_service_test.cc
│   ├── async_channel_test.cc
│   ├── cancel_test.cc
│   ├── main.cc
│   └── CMakeLists.txt
├── scripts/                    # 辅助脚本
│   └── run_tests.sh
└── configs/                    # 配置文件
    └── xrpc.conf
```

## 测试

运行单元测试：

```bash
cd build
make test
```

## 文档

- **API 参考**：`docs/api.md`
- **设计原理**：`docs/design.md`
- **使用指南**：`docs/tutorial.md`

## 许可证

MIT License（见 `LICENSE` 文件）



---
