#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
#include <google/protobuf/stubs/callback.h>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <string>

// 异步调用回调管理
class AsyncCallback {
public:
    AsyncCallback(example::LoginResponse* response, xrpc::XrpcController* controller)
        : response_(response), controller_(controller), called_(false) {}

    void OnCallback() {
        std::lock_guard<std::mutex> lock(mutex_);
        error_text_ = controller_->ErrorText();
        called_ = true;
        cv_.notify_one();
    }

    bool Wait(int timeout_ms = 5000) { // 增加超时时间
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return called_; });
    }

    const std::string& GetErrorText() const { return error_text_; }

private:
    example::LoginResponse* response_;
    xrpc::XrpcController* controller_;
    std::string error_text_;
    bool called_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// 客户端逻辑
class UserClient {
public:
    UserClient(const std::string& config_file) : channel_(config_file), stub_(&channel_) {}

    bool SyncLogin(const std::string& username, const std::string& password, example::LoginResponse& response) {
        xrpc::XrpcController controller;
        example::LoginRequest request;
        request.set_username(username);
        request.set_password(password);

        XRPC_LOG_INFO("Sending sync Login request for user: {}", username);
        stub_.Login(&controller, &request, &response, nullptr);

        if (controller.Failed()) {
            XRPC_LOG_ERROR("Sync Login failed for user {}: {}", username, controller.ErrorText());
            return false;
        }
        XRPC_LOG_INFO("Sync Login succeeded for user: {}", username);
        return true;
    }

    bool AsyncLogin(const std::string& username, const std::string& password, example::LoginResponse& response) {
        const int max_retries = 2; // 重试次数
        for (int attempt = 1; attempt <= max_retries; ++attempt) {
            xrpc::XrpcController controller;
            example::LoginRequest request;
            request.set_username(username);
            request.set_password(password);

            AsyncCallback callback(&response, &controller);
            XRPC_LOG_INFO("Sending async Login request for user: {} (attempt {}/{})", username, attempt, max_retries);
            stub_.Login(&controller, &request, &response,
                        google::protobuf::NewCallback(&callback, &AsyncCallback::OnCallback));

            if (!callback.Wait()) {
                XRPC_LOG_ERROR("Async Login timeout for user: {} (attempt {}/{})", username, attempt, max_retries);
                if (attempt == max_retries) {
                    return false;
                }
                continue;
            }

            if (!callback.GetErrorText().empty()) {
                XRPC_LOG_ERROR("Async Login failed for user {}: {} (attempt {}/{})", username, callback.GetErrorText(), attempt, max_retries);
                return false;
            }

            XRPC_LOG_INFO("Async Login succeeded for user: {}", username);
            return true;
        }
        return false;
    }

private:
    xrpc::XrpcChannel channel_;
    example::UserService_Stub stub_;
};

void PrintUsage() {
    std::cerr << "Usage: ./user_client [--sync | --async | --help]\n"
              << "  --sync  : Use synchronous calls\n"
              << "  --async : Use asynchronous calls\n"
              << "  --help  : Show this help message\n";
}

int main(int argc, char* argv[]) {
    bool use_sync = false;
    bool use_async = false;
    if (argc != 2) {
        PrintUsage();
        return 1;
    }
    std::string arg(argv[1]);
    if (arg == "--sync") {
        use_sync = true;
    } else if (arg == "--async") {
        use_async = true;
    } else if (arg == "--help") {
        PrintUsage();
        return 0;
    } else {
        PrintUsage();
        return 1;
    }

    try {
        xrpc::InitLoggerFromConfig("../configs/xrpc.conf");
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize logger: " << e.what() << std::endl;
        return 1;
    }

    UserClient client("../configs/xrpc.conf");

    std::vector<std::pair<std::string, std::string>> test_users = {
        {"test_user", "test_pass"},
        {"admin", "admin123"},
        {"invalid_user", "123"}
    };
    std::vector<std::thread> threads;

    for (const auto& user : test_users) {
        threads.emplace_back([&client, &user, use_sync]() {
            example::LoginResponse response;
            bool success;
            if (use_sync) {
                success = client.SyncLogin(user.first, user.second, response);
            } else {
                success = client.AsyncLogin(user.first, user.second, response);
            }
            if (!success) {
                std::cout << "Login failed for user " << user.first << ": "
                          << response.error_message() << std::endl;
            } else {
                std::cout << "Login succeeded for user " << user.first
                          << ", token: " << response.token() << std::endl;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return 0;
}