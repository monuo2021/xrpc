#include "core/channel/xrpc_channel.h"
#include "core/controller/xrpc_controller.h"
#include "core/server/xrpc_server.h"
#include "registry/zookeeper_client.h"
#include "user_service.pb.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

namespace xrpc {

class MockUserService : public example::UserService {
public:
    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        response->set_success(true);
        response->set_token("mock_token");
        done->Run();
    }
};

class XrpcChannelTest : public ::testing::Test {
protected:
    void SetUp() override {
        zk_.Start();
        zk_.Register("/UserService/Login", "127.0.0.1:8080", true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待 ZooKeeper 注册
        server_ = std::make_unique<XrpcServer>();
        service_ = std::make_unique<MockUserService>();
        server_->AddService(service_.get());
        server_->Start();
    }

    void TearDown() override {
        server_.reset();
        zk_.Delete("/UserService/Login");
    }

    ZookeeperClient zk_;
    std::unique_ptr<XrpcServer> server_;
    std::unique_ptr<MockUserService> service_;
};

TEST_F(XrpcChannelTest, CallLogin) {
    XrpcChannel channel("UserService", &zk_);
    example::UserService_Stub stub(&channel);

    example::LoginRequest req;
    req.set_username("test");
    req.set_password("pass");

    example::LoginResponse resp;
    XrpcController ctrl;

    stub.Login(&ctrl, &req, &resp, nullptr);
    EXPECT_FALSE(ctrl.Failed()) << "Error: " << ctrl.ErrorText();
    EXPECT_TRUE(resp.success());
    EXPECT_EQ(resp.token(), "mock_token");
}

} // namespace xrpc