
## XRPC 使用指南

### 环境准备

确保安装以下依赖：

- CMake 3.10+
- C++11 编译器（g++ 或 clang++）
- Boost.Asio
- Protocol Buffers
- ZooKeeper C 客户端
- spdlog（可选，用于日志）

运行环境要求：

- ZooKeeper 服务（默认 `127.0.0.1:2181`）。
- 配置文件 `configs/xrpc.conf`。

---

### 构建项目

1. 克隆仓库：

   ```bash
   git clone <repository-url>
   cd XRPC
   ```

2. 创建构建目录并编译：

   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

3. 安装（可选）：

   ```bash
   make install
   ```

---
### 配置

编辑 `configs/xrpc.conf`：

```ini
# ZooKeeper 设置
zookeeper_ip=127.0.0.1
zookeeper_port=2181
zookeeper_timeout_ms=6000

# 服务端设置
server_ip=0.0.0.0
server_port=8080

# 日志设置
log_level=debug
log_file=xrpc.log
```

---

### 运行示例

#### 启动服务端

1. 实现服务（参考 `examples/server/user_server.cc`）：

   ```cpp
   #include "core/server/xrpc_server.h"
   #include "user_service.pb.h"

   class UserServiceImpl : public example::UserService {
   public:
       void Login(google::protobuf::RpcController* controller,
                  const example::LoginRequest* request,
                  example::LoginResponse* response,
                  google::protobuf::Closure* done) override {
           if (request->username() == "test_user" && request->password() == "test_pass") {
               response->set_success(true);
               response->set_token("mock_token");
           } else {
               response->set_success(false);
               controller->SetFailed("Invalid credentials");
           }
           if (done) done->Run();
       }
   };

   int main() {
       xrpc::InitLoggerFromConfig("../configs/xrpc.conf");
       xrpc::XrpcServer server("../configs/xrpc.conf");
       UserServiceImpl service;
       server.RegisterService(&service);
       server.Start();
       return 0;
   }
   ```

2. 运行服务端：

   ```bash
   ./bin/user_server
   ```

---

#### 运行客户端

1. 实现客户端（参考 `examples/client/user_client.cc`）：

   ```cpp
   #include "core/channel/xrpc_channel.h"
   #include "core/controller/xrpc_controller.h"
   #include "user_service.pb.h"

   int main() {
       xrpc::XrpcChannel channel("../configs/xrpc.conf");
       xrpc::XrpcController controller;
       example::UserService_Stub stub(&channel);
       example::LoginRequest request;
       request.set_username("test_user");
       request.set_password("test_pass");
       example::LoginResponse response;
       stub.Login(&controller, &request, &response, nullptr);
       if (!controller.Failed() && response.success()) {
           std::cout << "Login succeeded, token: " << response.token() << std::endl;
       } else {
           std::cout << "Login failed: " << controller.ErrorText() << std::endl;
       }
       return 0;
   }
   ```

2. 运行客户端：

   ```bash
   ./bin/user_client --sync
   ```

3. 异步调用示例：

   ```bash
   ./bin/user_client --async --threads 5
   ```

---

### 测试

运行单元测试：

```bash
cd build
make test
```

测试覆盖：
- 配置加载和日志初始化
- 编解码（压缩/非压缩）
- 服务注册与发现
- 同步/异步调用
- 请求取消

---

### 调试

- 设置 `log_level=debug` 在 `xrpc.conf` 中启用详细日志。
- 使用 ZooKeeper 客户端工具（如 `zkCli.sh`）检查服务注册：

  ```bash
  ls /UserService
  get /UserService/0.0.0.0:8080
  ```
