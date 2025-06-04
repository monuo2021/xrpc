#include <gtest/gtest.h>
#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "user_service.pb.h"
#include <thread>
#include <chrono>
#include "registry/zookeeper_client.h"

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
        server_ = std::make_unique<XrpcServer>(config_file_);
        server_->RegisterService(&service_);
        server_thread_ = std::thread([this]() { server_->Start(); });

        // 动态等待服务注册
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
    }

    std::string config_file_;
    MockUserService service_;
    std::unique_ptr<XrpcServer> server_;
    std::thread server_thread_;
};

TEST_F(ServerTest, RegisterAndStart) {
    // 服务已通过 SetUp 注册和启动
    EXPECT_TRUE(true);
}

} // namespace xrpc