#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "registry/zookeeper_client.h"
#include "example/user_service.pb.h"
#include <gtest/gtest.h>

namespace xrpc {

class XrpcChannelTest : public ::testing::Test {
protected:
    void SetUp() override {
        zk_.Start();
    }

    void TearDown() override {
        zk_.Delete("/UserService/Login");
    }

    ZookeeperClient zk_;
};

TEST_F(XrpcChannelTest, CallLogin) {
    zk_.Register("/UserService/Login", "127.0.0.1:8080", true);
    XrpcChannel channel("UserService", &zk_);
    example::UserService_Stub stub(&channel);

    example::LoginRequest req;
    req.set_username("test");
    req.set_password("pass");

    example::LoginResponse resp;
    XrpcController ctrl;

    stub.Login(&ctrl, &req, &resp, nullptr);
    EXPECT_FALSE(ctrl.Failed()) << "Error: " << ctrl.ErrorText();
}

} // namespace xrpc
