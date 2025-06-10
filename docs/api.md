## XRPC API 参考

### 概述

XRPC 是一个轻量级 RPC 框架，支持服务注册与发现（基于 ZooKeeper）、长连接（基于 Boost.Asio）、异步调用和请求取消。以下是核心类的 API 参考。

---

### 类：`xrpc::XrpcServer`

#### 构造函数

```cpp
XrpcServer(const std::string& config_file);
```

- **描述**：初始化 XRPC 服务端，加载配置文件并设置 ZooKeeper 和网络传输。
- **参数**：
  - `config_file`：配置文件路径（例如 `configs/xrpc.conf`）。
- **异常**：若配置文件加载失败或 ZooKeeper 初始化失败，抛出 `std::runtime_error`。

#### 方法

```cpp
void RegisterService(google::protobuf::Service* service);
```

- **描述**：注册一个 Protobuf 服务到服务器。
- **参数**：
  - `service`：继承自 `google::protobuf::Service` 的服务实现。
- **备注**：服务信息会注册到 ZooKeeper。

```cpp
void Start();
```

- **描述**：启动服务器，开始监听请求。
- **备注**：调用后服务器进入异步事件循环。

---

### 类：`xrpc::XrpcChannel`

#### 构造函数

```cpp
XrpcChannel(const std::string& config_file);
```

- **描述**：初始化 XRPC 客户端通道，加载配置文件并连接 ZooKeeper。
- **参数**：
  - `config_file`：配置文件路径。
- **异常**：若配置文件或 ZooKeeper 初始化失败，抛出 `std::runtime_error`。

#### 方法

```cpp
void CallMethod(const google::protobuf::MethodDescriptor* method,
                google::protobuf::RpcController* controller,
                const google::protobuf::Message* request,
                google::protobuf::Message* response,
                google::protobuf::Closure* done);
```

- **描述**：调用远程服务方法，支持同步和异步调用。
- **参数**：
  - `method`：方法描述符。
  - `controller`：控制请求状态（如取消、错误）。
  - `request`：请求消息（Protobuf 格式）。
  - `response`：响应消息（Protobuf 格式）。
  - `done`：异步回调，若为 `nullptr` 则为同步调用。
- **备注**：通过 ZooKeeper 发现服务地址，使用 `AsioTransport` 发送请求。

---

### 类：`xrpc::XrpcController`

#### 构造函数

```cpp
XrpcController();
```

- **描述**：初始化 RPC 控制器，用于管理请求状态。

#### 方法

```cpp
void Reset();
```

- **描述**：重置控制器状态，清除错误和取消标志。

```cpp
bool Failed() const;
```

- **描述**：检查请求是否失败。
- **返回值**：`true` 表示失败，`false` 表示成功。

```cpp
std::string ErrorText() const;
```

- **描述**：获取失败原因。
- **返回值**：错误消息字符串。

```cpp
void SetFailed(const std::string& reason);
```

- **描述**：设置请求失败状态和原因。
- **参数**：
  - `reason`：失败原因。

```cpp
void StartCancel();
```

- **描述**：启动请求取消。

```cpp
bool IsCanceled() const;
```

- **描述**：检查请求是否被取消。
- **返回值**：`true` 表示已取消。

```cpp
void NotifyOnCancel(google::protobuf::Closure* callback);
```

- **描述**：设置取消时的回调函数。
- **参数**：
  - `callback`：取消时执行的回调。

---

### 类：`xrpc::XrpcCodec`

#### 方法

```cpp
std::string Encode(const RpcHeader& header, const google::protobuf::Message& args);
```

- **描述**：编码请求消息（头部 + 参数）。
- **参数**：
  - `header`：RPC 头部，包含服务名、方法名等。
  - `args`：请求参数（Protobuf 消息）。
- **返回值**：编码后的二进制字符串。
- **备注**：支持可选压缩（zlib）。

```cpp
bool Decode(const std::string& data, RpcHeader& header, std::string& args);
```

- **描述**：解码请求消息。
- **参数**：
  - `data`：输入二进制数据。
  - `header`：解析出的 RPC 头部。
  - `args`：解析出的参数字符串。
- **返回值**：`true` 表示解码成功。

```cpp
std::string EncodeResponse(const RpcHeader& header, const google::protobuf::Message& response);
```

- **描述**：编码响应消息。
- **参数**：
  - `header`：响应头部。
  - `response`：响应消息（Protobuf 格式）。
- **返回值**：编码后的二进制字符串。

```cpp
bool DecodeResponse(const std::string& data, RpcHeader& header, google::protobuf::Message& response);
```

- **描述**：解码响应消息。
- **参数**：
  - `data`：输入二进制数据。
  - `header`：解析出的响应头部。
  - `response`：解析出的响应消息。
- **返回值**：`true` 表示解码成功。

---

### 类：`xrpc::ZookeeperClient`

#### 构造函数

```cpp
ZookeeperClient();
```
- **描述**：初始化 ZooKeeper 客户端，加载配置。

#### 方法

```cpp
void Start();
```

- **描述**：启动 ZooKeeper 客户端，连接到服务器。

```cpp
void Stop();
```

- **描述**：停止客户端，断开连接并清理资源。

```cpp
void Register(const std::string& path, const std::string& data, bool ephemeral = false);
```

- **描述**：注册服务节点到 ZooKeeper。
- **参数**：
  - `path`：节点路径（例如 `/UserService/127.0.0.1:8080`）。
  - `data`：节点数据（如 `methods=Login`）。
  - `ephemeral`：是否为临时节点。

```cpp
std::string Discover(const std::string& path);
```

- **描述**：发现指定节点的数据。
- **返回值**：节点数据。

```cpp
std::vector<std::pair<std::string, std::string>> DiscoverService(const std::string& service);
```

- **描述**：发现指定服务的所有实例。
- **参数**：
  - `service`：服务名（如 `UserService`）。
- **返回值**：节点路径和数据的键值对列表。

```cpp
std::vector<std::string> FindInstancesByMethod(const std::string& service, const std::string& method);
```

- **描述**：根据方法名查找支持该方法的服务实例。
- **返回值**：实例地址列表（如 `127.0.0.1:8080`）。

```cpp
void Watch(const std::string& path, std::function<void(std::string)> callback);
```

- **描述**：设置节点变化的监听回调。
- **参数**：
  - `path`：监听的节点路径。
  - `callback`：节点变化时的回调函数。

---

### 示例代码
#### 服务端

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

#### 客户端

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
    }
    return 0;
}
```

