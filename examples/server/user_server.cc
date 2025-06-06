#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
#include <iostream>
#include <string>
#include <getopt.h>

namespace xrpc {
// UserService 实现类
class UserServiceImpl : public example::UserService {
public:
    // 实现 Login 方法，模拟用户认证
    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        // XRPC 服务端流程：6. 处理请求
        // - 检查用户名和密码，模拟认证逻辑
        if (request->username() == "test_user" && request->password() == "test_pass") {
            response->set_success(true);
            response->set_token("mock_token");
        } else {
            response->set_success(false);
            response->set_error_message("Invalid credentials");
            controller->SetFailed("Invalid credentials");
        }
        // XRPC 服务端流程：7. 通知调用完成
        if (done) {
            done->Run();
        }
        // - done->Run() 触发响应编码和发送
    }
};
} // namespace xrpc

void PrintUsage(const char* program) {
    std::cerr << "Usage: " << program << " [--config <config_file>]\n"
              << "  --config   Path to xrpc.conf (default: ../configs/xrpc.conf)\n";
}

int main(int argc, char* argv[]) {
    // 默认参数
    std::string config_file = "../configs/xrpc.conf";

    // 解析命令行参数
    static struct option long_options[] = {
        {"config", required_argument, nullptr, 'c'},
        {nullptr, 0, nullptr, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "c:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_file = optarg; break;
            default: PrintUsage(argv[0]); return 1;
        }
    }

    // XRPC 服务端流程：1. 初始化日志
    try {
        xrpc::InitLoggerFromConfig(config_file);
    } catch (const std::runtime_error& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    // XRPC 服务端流程：2. 创建服务实现
    xrpc::UserServiceImpl service;
    // - UserServiceImpl 继承 Protobuf 生成的 UserService，定义 Login 逻辑

    // XRPC 服务端流程：3. 创建 XrpcServer，加载配置
    xrpc::XrpcServer server(config_file);
    // - XrpcServer 构造函数加载 xrpc.conf，初始化 ZooKeeper 和 AsioTransport
    // - 从配置读取 server_ip 和 server_port（如 0.0.0.0:8080）

    // XRPC 服务端流程：4. 注册服务
    server.RegisterService(&service);
    // - RegisterService 将 UserService 注册到 ZooKeeper
    // - 路径：/UserService/0.0.0.0:8080，数据：methods=Login
    // - 服务信息存储在 services_ 映射中

    // XRPC 服务端流程：5. 启动服务器
    server.Start();
    // - Start 调用 AsioTransport::Run，监听端口
    // - 接收请求后，XrpcCodec 解码，查找服务和方法，调用 UserServiceImpl::Login
    // - 响应通过 XrpcCodec 编码后发送

    // 服务器运行直到手动终止
    return 0;
}