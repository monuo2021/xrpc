#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
#include <unordered_map>
#include <mutex>
#include <string>
#include <thread>
#include <condition_variable>
#include <csignal>
#include <iostream>

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

    // 本地登录方法，处理实际业务逻辑
    bool LocalLogin(const std::string& username, const std::string& password) {
        return db_.Validate(username, password);
    }

    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        XRPC_LOG_INFO("Received Login request for user: {}", request->username());
        std::cout << "[INFO] Received Login request for user: " << request->username() << std::endl;

        // 验证请求参数
        if (request->username().empty() || request->password().empty()) {
            response->set_success(false);
            response->set_error_message("Username or password empty");
            controller->SetFailed("Invalid input");
            XRPC_LOG_ERROR("Login failed for user {}: Invalid input", request->username());
            std::cout << "[ERROR] Login failed for user " << request->username() << ": Invalid input" << std::endl;
            if (done) done->Run();
            return;
        }

        // 调用本地业务逻辑
        bool login_result = LocalLogin(request->username(), request->password());
        if (login_result) {
            response->set_success(true);
            response->set_token("token_" + request->username() + "_" + std::to_string(rand()));
            XRPC_LOG_INFO("Login successful for user: {}", request->username());
            std::cout << "[INFO] Login successful for user: " << request->username() << std::endl;
        } else {
            response->set_success(false);
            response->set_error_message("Invalid credentials");
            controller->SetFailed("Invalid credentials");
            XRPC_LOG_ERROR("Login failed for user: {}", request->username());
            std::cout << "[ERROR] Login failed for user " << request->username() << ": Invalid credentials" << std::endl;
        }

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
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
    // 注册信号处理
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 初始化日志系统
    try {
        xrpc::InitLoggerFromConfig("../configs/xrpc.conf");
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    // 创建并注册服务
    UserServiceImpl service;
    xrpc::XrpcServer server("../configs/xrpc.conf");
    server.RegisterService(&service);
    XRPC_LOG_INFO("UserService registered");
    std::cout << "[INFO] UserService registered" << std::endl;

    // 启动服务器
    try {
        server.Start();
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("Server failed to start: {}", e.what());
        std::cerr << "[ERROR] Server failed to start: " << e.what() << std::endl;
        return 1;
    }

    // 阻塞直到终止信号
    {
        std::unique_lock<std::mutex> lock(exit_mutex);
        exit_cv.wait(lock, [] { return !running; });
    }
    XRPC_LOG_INFO("Server shutting down");
    std::cout << "[INFO] Server shutting down" << std::endl;

    return 0;
}