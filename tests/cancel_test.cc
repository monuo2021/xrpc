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

class SlowMockUserService : public example::UserService {
public:
    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        // 模拟长时间处理
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (dynamic_cast<XrpcController*>(controller)->IsCanceled()) {
            controller->SetFailed("Request canceled");
            if (done) done->Run();
            return;
        }
        response->set_success(true);
        response->set_token("mock_token");
        if (done) done->Run();
    }
};

class CancelTest : public ::testing::Test {
public:
    // 异步调用回调
    void OnAsyncCallback() {
        std::lock_guard<std::mutex> lock(mtx_);
        async_callback_called_ = true;
        cv_.notify_one();
    }

    // 取消通知回调
    void OnCancelCallback() {
        std::lock_guard<std::mutex> lock(mtx_);
        cancel_callback_called_ = true;
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
    SlowMockUserService mock_service_;
    std::unique_ptr<XrpcServer> server_;
    std::thread server_thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool async_callback_called_ = false;
    bool cancel_callback_called_ = false;
};

TEST_F(CancelTest, CancelBeforeAsyncCall) {
    std::unique_ptr<XrpcChannel> channel = std::make_unique<XrpcChannel>(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(channel.get());

    example::LoginRequest request;
    request.set_username("test_user");
    request.set_password("test_pass");

    example::LoginResponse response;
    async_callback_called_ = false;

    controller.StartCancel();
    ASSERT_TRUE(controller.IsCanceled());

    stub.Login(&controller, &request, &response,
               google::protobuf::NewCallback(static_cast<CancelTest*>(this), &CancelTest::OnAsyncCallback));

    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return async_callback_called_; });
    }

    ASSERT_TRUE(async_callback_called_) << "Callback not called";
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorText(), "Request was canceled before sending");
    EXPECT_FALSE(response.success());

    channel.reset();
}

TEST_F(CancelTest, CancelDuringAsyncCall) {
    std::unique_ptr<XrpcChannel> channel = std::make_unique<XrpcChannel>(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(channel.get());

    example::LoginRequest request;
    request.set_username("test_user");
    request.set_password("test_pass");

    example::LoginResponse response;
    async_callback_called_ = false;

    stub.Login(&controller, &request, &response,
               google::protobuf::NewCallback(static_cast<CancelTest*>(this), &CancelTest::OnAsyncCallback));

    // 等待请求开始处理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    controller.StartCancel();
    ASSERT_TRUE(controller.IsCanceled());

    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return async_callback_called_; });
    }

    ASSERT_TRUE(async_callback_called_) << "Callback not called";
    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorText(), "Request was canceled");
    EXPECT_FALSE(response.success());

    channel.reset();
}

TEST_F(CancelTest, NotifyOnCancel) {
    XrpcController controller;
    cancel_callback_called_ = false;

    controller.NotifyOnCancel(google::protobuf::NewCallback(static_cast<CancelTest*>(this), &CancelTest::OnCancelCallback));

    controller.StartCancel();
    ASSERT_TRUE(controller.IsCanceled());

    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return cancel_callback_called_; });
    }

    EXPECT_TRUE(cancel_callback_called_);
}

} // namespace xrpc