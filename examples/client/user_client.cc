#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "user_service.pb.h"
#include "core/common/xrpc_logger.h"
#include <google/protobuf/stubs/callback.h>
#include <iostream>
#include <string>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <getopt.h>

// 异步调用回调管理
class AsyncClient {
public:
    AsyncClient() : callback_called_(false) {}
    
    // 异步回调函数，通知调用完成
    void OnCallback() {
        std::lock_guard<std::mutex> lock(mtx_);
        callback_called_ = true;
        cv_.notify_one();
    }
    
    // 等待异步调用完成，超时 2 秒
    bool Wait() {
        std::unique_lock<std::mutex> lock(mtx_);
        return cv_.wait_for(lock, std::chrono::seconds(2), [this] { return callback_called_; });
    }
    
    bool IsCallbackCalled() const { return callback_called_; }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool callback_called_;
};

void PrintUsage(const char* program) {
    std::cerr << "Usage: " << program << " [--config <config_file>] [--async] [--username <username>] [--password <password>]\n"
              << "  --config   Path to xrpc.conf (default: ../configs/xrpc.conf)\n"
              << "  --async    Use async call (default: sync)\n"
              << "  --username Username for login (default: test_user)\n"
              << "  --password Password for login (default: test_pass)\n";
}

int main(int argc, char* argv[]) {
    // 默认参数
    std::string config_file = "../configs/xrpc.conf";
    bool use_async = false;
    std::string username = "test_user";
    std::string password = "test_pass";

    // 解析命令行参数
    static struct option long_options[] = {
        {"config", required_argument, nullptr, 'c'},
        {"async", no_argument, nullptr, 'a'},
        {"username", required_argument, nullptr, 'u'},
        {"password", required_argument, nullptr, 'p'},
        {nullptr, 0, nullptr, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "c:au:p:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_file = optarg; break;
            case 'a': use_async = true; break;
            case 'u': username = optarg; break;
            case 'p': password = optarg; break;
            default: PrintUsage(argv[0]); return 1;
        }
    }

    // XRPC 调用流程：1. 初始化日志
    try {
        xrpc::InitLoggerFromConfig(config_file);
    } catch (const std::runtime_error& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    // XRPC 调用流程：2. 创建 XrpcChannel，加载配置并初始化 ZooKeeper
    xrpc::XrpcChannel channel(config_file);
    // - XrpcChannel 构造函数加载 xrpc.conf，初始化 ZooKeeper 客户端
    // - ZooKeeper 用于服务发现，查找 UserService 的实例地址

    // XRPC 调用流程：3. 创建 XrpcController，管理请求状态和取消
    xrpc::XrpcController controller;
    // - XrpcController 跟踪请求是否失败、错误信息、是否取消

    // XRPC 调用流程：4. 创建服务 Stub，封装远程调用
    example::UserService_Stub stub(&channel);
    // - Stub 将 Protobuf 定义的 UserService 转换为 C++ 接口

    // XRPC 调用流程：5. 准备请求消息
    example::LoginRequest request;
    request.set_username(username);
    request.set_password(password);

    // XRPC 调用流程：6. 准备响应消息
    example::LoginResponse response;

    if (use_async) {
        // XRPC 异步调用流程
        AsyncClient async_client;

        // XRPC 调用流程：7a. 发起异步调用
        stub.Login(&controller, &request, &response,
                   google::protobuf::NewCallback(&async_client, &AsyncClient::OnCallback));
        // - stub.Login 调用 XrpcChannel::CallMethod，构造 RpcHeader
        // - XrpcCodec 编码请求（Varint32 长度 + RpcHeader + Args）
        // - AsioTransport 通过 Boost.Asio 异步发送请求
        // - ZooKeeper 提供服务地址（如 0.0.0.0:8080）

        // XRPC 调用流程：8a. 等待异步回调
        if (!async_client.Wait()) {
            std::cerr << "Async call timeout" << std::endl;
            return 1;
        }

        // XRPC 调用流程：9a. 检查结果
        if (controller.Failed()) {
            std::cerr << "Async login failed: " << controller.ErrorText() << std::endl;
            return 1;
        }
        std::cout << "Async login success: " << response.success()
                  << ", token: " << response.token() << std::endl;
    } else {
        // XRPC 同步调用流程
        // XRPC 调用流程：7b. 发起同步调用
        stub.Login(&controller, &request, &response, nullptr);
        // - stub.Login 调用 XrpcChannel::CallMethod，构造 RpcHeader
        // - XrpcCodec 编码请求
        // - AsioTransport 同步发送请求并等待响应
        // - XrpcCodec 解码响应，填充 response

        // XRPC 调用流程：8b. 检查结果
        if (controller.Failed()) {
            std::cerr << "Sync login failed: " << controller.ErrorText() << std::endl;
            return 1;
        }
        std::cout << "Sync login success: " << response.success()
                  << ", token: " << response.token() << std::endl;
    }

    // XRPC 调用流程：10. 清理资源（由智能指针自动管理）
    // - XrpcChannel 析构时关闭 ZooKeeper 和 AsioTransport
    return 0;
}