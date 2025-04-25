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
        zk_.Start();
    }

    ZookeeperClient zk_;
};

TEST_F(ZookeeperClientTest, RegisterAndDiscover) {
    std::string path = "/UserService/Login";
    std::string data = "127.0.0.1:8080";

    // 注册临时节点
    ASSERT_NO_THROW(zk_.Register(path, data, true));
    
    // 发现服务
    std::string discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, data);

    // 重复注册（幂等）
    ASSERT_NO_THROW(zk_.Register(path, data, true));

    // 更新数据
    std::string new_data = "192.168.1.2:8081";
    ASSERT_NO_THROW(zk_.Register(path, new_data, true));
    discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, DiscoverNonExistent) {
    std::string path = "/NonExistentService/UnknownMethod";
    EXPECT_THROW(zk_.Discover(path), std::runtime_error);
}

TEST_F(ZookeeperClientTest, WatchNode) {
    std::string path = "/UserService/Login";
    std::string data = "127.0.0.1:8080";
    std::string received_data;

    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    zk_.Watch(path, [&](std::string new_data) {
        received_data = new_data;
        std::lock_guard lock(mtx);
        ready = true;
        cv.notify_one();
    });
    zk_.Register(path, data, true);
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return ready; });
    }
    EXPECT_EQ(received_data, data);

    // 更新节点
    std::string new_data = "192.168.1.2:8081";
    zk_.Register(path, new_data, true);

    // 等待 Watch 触发
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_EQ(received_data, new_data);
}

} // namespace xrpc