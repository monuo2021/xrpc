#include <gtest/gtest.h>
#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "core/server/xrpc_server.h"
#include "user_service.pb.h"
#include "core/common/xrpc_logger.h"
#include <google/protobuf/stubs/callback.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include "registry/zookeeper_client.h"

namespace xrpc {

class MockUserService : public example::UserService {
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
            response->set_token("");
            controller->SetFailed("Invalid credentials");
        }
        if (done) done->Run();
    }
};

class AsyncChannelTest : public ::testing::Test {
public:
    // 回调处理函数
    void OnCallback() {
        std::lock_guard<std::mutex> lock(mtx_);
        callback_called_ = true;
        cv_.notify_one();
    }

protected:
    void SetUp() override {
        zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
        config_file_ = "../configs/xrpc.conf";
        server_ = std::make_unique<XrpcServer>(config_file_);
        server_->RegisterService(&mock_service_);
        server_thread_ = std::thread([this]() { server_->Start(); });

        ZookeeperClient zk;
        zk.Start();
        int retries = 5;
        bool registered = false;
        while (retries-- > 0) {
            auto instances = zk.FindInstancesByMethod("UserService", "Login");
            if (!instances.empty()) {
                registered = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ASSERT_TRUE(registered) << "Service not registered in ZooKeeper";
    }

    void TearDown() override {
        server_.reset();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        ZookeeperClient zk;
        zk.Start();
        zk.Delete("/UserService/0.0.0.0:8080");
        zk.Stop();
    }

    std::string config_file_;
    MockUserService mock_service_;
    std::unique_ptr<XrpcServer> server_;
    std::thread server_thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool callback_called_ = false;
};

TEST_F(AsyncChannelTest, AsyncLoginSuccess) {
    std::unique_ptr<XrpcChannel> channel = std::make_unique<XrpcChannel>(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(channel.get());

    example::LoginRequest request;
    request.set_username("test_user");
    request.set_password("test_pass");

    example::LoginResponse response;
    callback_called_ = false;

    stub.Login(&controller, &request, &response,
               google::protobuf::NewCallback(static_cast<AsyncChannelTest*>(this), &AsyncChannelTest::OnCallback));

    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return callback_called_; });
    }

    ASSERT_TRUE(callback_called_) << "Async callback not called";
    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_TRUE(response.success());
    EXPECT_EQ(response.token(), "mock_token");

    channel.reset();
}

TEST_F(AsyncChannelTest, AsyncLoginFailure) {
    std::unique_ptr<XrpcChannel> channel = std::make_unique<XrpcChannel>(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(channel.get());

    example::LoginRequest request;
    request.set_username("wrong_user");
    request.set_password("wrong_pass");

    example::LoginResponse response;
    callback_called_ = false;

    stub.Login(&controller, &request, &response,
               google::protobuf::NewCallback(static_cast<AsyncChannelTest*>(this), &AsyncChannelTest::OnCallback));

    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return callback_called_; });
    }

    ASSERT_TRUE(callback_called_) << "Async callback not called";
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorText(), "Invalid credentials");
    EXPECT_FALSE(response.success());
    EXPECT_TRUE(response.token().empty());

    channel.reset();
}

} // namespace xrpc