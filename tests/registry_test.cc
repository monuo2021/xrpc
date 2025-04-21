#include "registry/zookeeper_client.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <condition_variable>  // 添加缺失的头文件
#include <mutex>              // 添加mutex头文件

namespace xrpc {

class ZookeeperClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        InitLoggerFromConfig("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
        zk_.Start();
    }

    ZookeeperClient zk_;
};


TEST_F(ZookeeperClientTest, RegisterAndDiscover) {
    std::string path = "/UserService/127.0.0.1:8080";
    std::string data = "methods=Login";

    // 注册临时节点
    ASSERT_NO_THROW(zk_.Register(path, data, true));
    
    // 发现服务
    std::string discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, data);

    // 重复注册（幂等）
    ASSERT_NO_THROW(zk_.Register(path, data, true));

    // 更新数据
    std::string new_data = "methods=Login,Logout";
    ASSERT_NO_THROW(zk_.Register(path, new_data, true));
    discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, DiscoverNonExistent) {
    std::string path = "/NonExistentService/127.0.0.1:8080";
    EXPECT_THROW(zk_.Discover(path), std::runtime_error);
}

TEST_F(ZookeeperClientTest, WatchNode) {
    std::string path = "/UserService/127.0.0.1:8080";
    std::string data = "methods=Login";
    std::string received_data;

    // 设置 Watch
    zk_.Watch(path, [&received_data](std::string new_data) {
        received_data = new_data;
    });

    // 注册节点
    zk_.Register(path, data, true);

    // 等待 Watch 触发
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_EQ(received_data, data);

    // 更新节点
    std::string new_data = "methods=Login,Logout";
    zk_.Register(path, new_data, true);

    // 等待 Watch 触发
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_EQ(received_data, new_data);
}

} // namespace xrpc