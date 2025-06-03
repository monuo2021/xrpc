#include <gtest/gtest.h>
#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "core/server/xrpc_server.h"
#include "user_service.pb.h"
#include "core/common/xrpc_logger.h"
#include <thread>
#include <chrono>

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

class UserServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_file_ = "../configs/xrpc.conf";

        // 启动服务器
        server_ = std::make_unique<XrpcServer>(config_file_);
        server_->RegisterService(&service_);
        server_thread_ = std::thread([this]() { server_->Start(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // 等待服务器启动
    }

    void TearDown() override {
        server_.reset();
        server_thread_.join();
    }

    std::string config_file_;
    MockUserService service_;
    std::unique_ptr<XrpcServer> server_;
    std::thread server_thread_;
};

TEST_F(UserServiceTest, LoginSuccess) {
    XrpcChannel channel(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(&channel);

    example::LoginRequest request;
    request.set_username("test_user");
    request.set_password("test_pass");

    example::LoginResponse response;
    stub.Login(&controller, &request, &response, nullptr);

    EXPECT_FALSE(controller.Failed()) << controller.ErrorText();
    EXPECT_TRUE(response.success());
    EXPECT_EQ(response.token(), "mock_token");
}

TEST_F(UserServiceTest, LoginFailure) {
    XrpcChannel channel(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(&channel);

    example::LoginRequest request;
    request.set_username("wrong_user");
    request.set_password("wrong_pass");

    example::LoginResponse response;
    stub.Login(&controller, &request, &response, nullptr);

    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorText(), "Invalid credentials");
    EXPECT_FALSE(response.success());
    EXPECT_TRUE(response.token().empty());
}

} // namespace xrpc