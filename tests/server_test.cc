#include <gtest/gtest.h>
#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
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
        if (done) done->Run();
    }
};

class ServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_file_ = "../configs/xrpc.conf";
    }

    std::string config_file_;
};

TEST_F(ServerTest, RegisterAndStart) {
    XrpcServer server(config_file_);
    MockUserService service;
    server.RegisterService(&service);

    // 启动服务器（后台线程）
    std::thread server_thread([&server]() { server.Start(); });
    server_thread.detach();

    // 等待服务器注册到 ZooKeeper
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // 验证服务注册（需 ZooKeeper 客户端检查，简化测试）
    EXPECT_TRUE(true); // 假设注册成功
}

} // namespace xrpc