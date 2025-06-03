#include <gtest/gtest.h>
#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "user_service.pb.h"
#include "core/common/xrpc_logger.h"

namespace xrpc {

class ChannelTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_file_ = "../configs/xrpc.conf";
    }

    std::string config_file_;
};

TEST_F(ChannelTest, CallMethodSuccess) {
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

TEST_F(ChannelTest, CallMethodInvalidService) {
    XrpcChannel channel(config_file_);
    XrpcController controller;
    example::UserService_Stub stub(&channel);

    // 使用不存在的服务名
    const google::protobuf::MethodDescriptor* method = example::UserService::descriptor()->method(0);
    google::protobuf::Message* request = new example::LoginRequest;
    google::protobuf::Message* response = new example::LoginResponse;

    channel.CallMethod(method, &controller, request, response, nullptr);

    EXPECT_TRUE(controller.Failed());
    EXPECT_EQ(controller.ErrorText(), "Service instance not found");

    delete request;
    delete response;
}

} // namespace xrpc