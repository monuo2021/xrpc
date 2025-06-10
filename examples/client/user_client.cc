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
#include <chrono>
#include <atomic>
#include <memory>

// 异步调用回调管理
class AsyncCallback {
public:
    AsyncCallback(xrpc::XrpcController* controller)
        : controller_(controller), called_(false) {}

    void OnCallback() {
        std::lock_guard<std::mutex> lock(mutex_);
        error_text_ = controller_->ErrorText();
        called_ = true;
        cv_.notify_one();
    }

    bool Wait(int timeout_ms = 8000) { // 延长超时时间至 8 秒
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return called_; });
    }

    const std::string& GetErrorText() const { return error_text_; }

private:
    xrpc::XrpcController* controller_;
    std::string error_text_;
    bool called_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// 客户端逻辑
class UserClient {
public:
    UserClient(const std::string& config_file) : config_file_(config_file) {}

    bool SyncLogin(const std::string& username, const std::string& password, example::LoginResponse& response) {
        xrpc::XrpcChannel channel(config_file_);
        example::UserService_Stub stub(&channel);
        xrpc::XrpcController controller;
        example::LoginRequest request;
        request.set_username(username);
        request.set_password(password);

        XRPC_LOG_INFO("Sending sync Login request for user: {}", username);
        std::cout << "[INFO] Sending sync Login request for user: " << username << std::endl;

        stub.Login(&controller, &request, &response, nullptr);

        if (controller.Failed()) {
            XRPC_LOG_ERROR("Sync Login failed for user {}: {}", username, controller.ErrorText());
            std::cout << "[ERROR] Sync Login failed for user " << username << ": " << controller.ErrorText() << std::endl;
            return false;
        }

        if (!response.success()) {
            XRPC_LOG_ERROR("Sync Login failed for user {}: {}", username, response.error_message());
            std::cout << "[ERROR] Sync Login failed for user " << username << ": " << response.error_message() << std::endl;
            return false;
        }

        XRPC_LOG_INFO("Sync Login succeeded for user: {}", username);
        std::cout << "[INFO] Sync Login succeeded for user: " << username << ", token: " << response.token() << std::endl;
        return true;
    }

    bool AsyncLogin(const std::string& username, const std::string& password, example::LoginResponse& response) {
        const int max_retries = 2;
        for (int attempt = 1; attempt <= max_retries; ++attempt) {
            // 为每次异步调用创建独立的 channel 和 stub
            xrpc::XrpcChannel channel(config_file_);
            example::UserService_Stub stub(&channel);
            xrpc::XrpcController controller;
            example::LoginRequest request;
            request.set_username(username);
            request.set_password(password);

            // 使用智能指针管理 AsyncCallback
            auto callback = std::make_unique<AsyncCallback>(&controller);
            XRPC_LOG_INFO("Sending async Login request for user: {} (attempt {}/{})", username, attempt, max_retries);
            std::cout << "[INFO] Sending async Login request for user: " << username << " (attempt " << attempt << "/" << max_retries << ")" << std::endl;

            stub.Login(&controller, &request, &response,
                       google::protobuf::NewCallback(callback.get(), &AsyncCallback::OnCallback));

            if (!callback->Wait()) {
                XRPC_LOG_ERROR("Async Login timeout for user: {} (attempt {}/{})", username, attempt, max_retries);
                std::cout << "[ERROR] Async Login timeout for user: " << username << " (attempt " << attempt << "/" << max_retries << ")" << std::endl;
                if (attempt == max_retries) return false;
                continue;
            }

            if (!callback->GetErrorText().empty()) {
                XRPC_LOG_ERROR("Async Login failed for user {}: {} (attempt {}/{})", username, callback->GetErrorText(), attempt, max_retries);
                std::cout << "[ERROR] Async Login failed for user " << username << ": " << callback->GetErrorText() << " (attempt " << attempt << "/" << max_retries << ")" << std::endl;
                if (attempt == max_retries) return false;
                continue;
            }

            if (!response.success()) {
                XRPC_LOG_ERROR("Async Login failed for user {}: {} (attempt {}/{})", username, response.error_message(), attempt, max_retries);
                std::cout << "[ERROR] Async Login failed for user " << username << ": " << response.error_message() << " (attempt " << attempt << "/" << max_retries << ")" << std::endl;
                if (attempt == max_retries) return false;
                continue;
            }

            XRPC_LOG_INFO("Async Login succeeded for user: {}", username);
            std::cout << "[INFO] Async Login succeeded for user: " << username << ", token: " << response.token() << std::endl;
            return true;
        }
        return false;
    }

private:
    std::string config_file_;
};

void PrintUsage() {
    std::cerr << "Usage: ./user_client [--sync | --async | --help] [--threads N]\n"
              << "  --sync      : Use synchronous calls\n"
              << "  --async     : Use asynchronous calls\n"
              << "  --threads N : Number of concurrent threads (default: 1, max: 10)\n"
              << "  --help      : Show this help message\n";
}

int main(int argc, char* argv[]) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
    bool use_sync = false;
    bool use_async = false;
    int thread_count = 1;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--sync") {
            use_sync = true;
        } else if (arg == "--async") {
            use_async = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            thread_count = std::stoi(argv[++i]);
            if (thread_count < 1) {
                PrintUsage();
                return 1;
            }
        } else if (arg == "--help") {
            PrintUsage();
            return 0;
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (!use_sync && !use_async) {
        PrintUsage();
        return 1;
    }

    // 初始化日志
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
    std::atomic<int> success_count(0);
    std::atomic<int> fail_count(0);
    const int requests_per_thread = 1;

    auto start_time = std::chrono::high_resolution_clock::now();

    // 多线程发送请求
    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([&client, &test_users, use_sync, use_async, i, &success_count, &fail_count]() {
            size_t user_idx = i % test_users.size();
            const auto& user = test_users[user_idx];
            example::LoginResponse response;
            bool success;
            if (use_sync) {
                success = client.SyncLogin(user.first, user.second, response);
            } else if (use_async) {
                success = client.AsyncLogin(user.first, user.second, response);
            }
            if (success) {
                success_count++;
            } else {
                fail_count++;
            }
        });
    }

    // 等待线程完成
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end_time - start_time;

    // 输出统计结果
    std::string stats = "Total requests: " + std::to_string(thread_count * requests_per_thread) + "\n" +
                        "Success count: " + std::to_string(success_count) + "\n" +
                        "Fail count: " + std::to_string(fail_count) + "\n" +
                        "Elapsed time: " + std::to_string(elapsed.count()) + " seconds\n" +
                        "QPS: " + std::to_string((thread_count * requests_per_thread) / elapsed.count());
        
    XRPC_LOG_INFO("Total requests: {}", std::to_string(thread_count * requests_per_thread));
    XRPC_LOG_INFO("Success count: {}", std::to_string(success_count));
    XRPC_LOG_INFO("Fail count: {}", std::to_string(fail_count));
    XRPC_LOG_INFO("Elapsed time: {} seconds", std::to_string(elapsed.count()));
    XRPC_LOG_INFO("QPS: {}", std::to_string((thread_count * requests_per_thread) / elapsed.count()));
    std::cout << "[INFO]\n" << stats << std::endl;

    return 0;
}