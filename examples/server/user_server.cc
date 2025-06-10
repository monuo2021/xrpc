#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <csignal>

// 用户数据库，模拟存储用户凭据
class UserDatabase {
public:
    UserDatabase() {
        // 初始化用户数据
        users_["test_user"] = "test_pass";
        users_["admin"] = "admin123";
    }

    bool Validate(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = users_.find(username);
        return it != users_.end() && it->second == password;
    }

private:
    std::unordered_map<std::string, std::string> users_;
    std::mutex mutex_;
};

// UserService 实现
class UserServiceImpl : public example::UserService {
public:
    UserServiceImpl() : db_() {}

    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        // 1. 验证请求参数
        XRPC_LOG_INFO("Received Login request for user: {}", request->username());
        if (request->username().empty() || request->password().empty()) {
            response->set_success(false);
            response->set_error_message("Username or password empty");
            controller->SetFailed("Invalid input");
            if (done) done->Run();
            return;
        }

        // 2. 模拟数据库验证（线程安全）
        if (db_.Validate(request->username(), request->password())) {
            response->set_success(true);
            response->set_token("token_" + request->username() + "_" + std::to_string(rand()));
            XRPC_LOG_INFO("Login successful for user: {}", request->username());
        } else {
            response->set_success(false);
            response->set_error_message("Invalid credentials");
            controller->SetFailed("Invalid credentials");
            XRPC_LOG_ERROR("Login failed for user: {}", request->username());
        }

        // 3. 执行回调，完成请求
        if (done) done->Run();
    }

private:
    UserDatabase db_;
};

// 全局标志和条件变量，用于优雅退出
static std::condition_variable exit_cv;
static std::mutex exit_mutex;
static bool running = true;

void SignalHandler(int /*sig*/) {
    {
        std::lock_guard<std::mutex> lock(exit_mutex);
        running = false;
    }
    exit_cv.notify_one();
}

int main(int argc, char* argv[]) {
    // 1. 注册信号处理，捕获 Ctrl+C
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 2. 初始化日志系统
    try {
        xrpc::InitLoggerFromConfig("../configs/xrpc.conf");
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    // 3. 创建并注册 UserService
    UserServiceImpl service;
    xrpc::XrpcServer server("../configs/xrpc.conf");
    server.RegisterService(&service);
    XRPC_LOG_INFO("UserService registered");

    // 4. 启动服务器，监听请求
    try {
        server.Start();
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("Server failed to start: {}", e.what());
        return 1;
    }

    // 5. 阻塞主线程，直到收到终止信号
    {
        std::unique_lock<std::mutex> lock(exit_mutex);
        exit_cv.wait(lock, [] { return !running; });
    }
    XRPC_LOG_INFO("Server shutting down");

    return 0;
}