#include "registry/zookeeper_client.h"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace xrpc {

class ZookeeperClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_NO_THROW(zk_.Start());
    }

    void TearDown() override {
        try {
            zk_.Delete("/UserService/Login");
            zk_.Delete("/UserService/Other");
            zk_.Delete("/NonExistentService/UnknownMethod");
        } catch (const std::exception& e) {
        }
    }

    ZookeeperClient zk_;
};

TEST_F(ZookeeperClientTest, RegisterAndDiscover) {
    std::string path = "/UserService/Login";
    std::string data = "127.0.0.1:8080";

    ASSERT_NO_THROW(zk_.Register(path, data, true));
    std::string discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, data);

    ASSERT_NO_THROW(zk_.Register(path, data, true));

    std::string new_data = "192.168.1.2:8081";
    ASSERT_NO_THROW(zk_.Register(path, new_data, true));
    discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, new_data);

    discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, DiscoverNonExistent) {
    std::string path = "/NonExistentService/UnknownMethod";
    EXPECT_THROW(zk_.Discover(path), std::runtime_error);
}

TEST_F(ZookeeperClientTest, DeleteNode) {
    std::string path = "/UserService/Login";
    std::string data = "127.0.0.1:8080";

    ASSERT_NO_THROW(zk_.Register(path, data, true));
    ASSERT_NO_THROW(zk_.Delete(path));
    EXPECT_THROW(zk_.Discover(path), std::runtime_error);
    ASSERT_NO_THROW(zk_.Delete(path));

    std::string new_data = "192.168.1.2:8081";
    ASSERT_NO_THROW(zk_.Register(path, new_data, true));
    std::string discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, WatchNode) {
    std::string path = "/UserService/Login";
    std::string data = "127.0.0.1:8080";
    std::vector<std::string> received_data;
    bool node_deleted = false;

    std::mutex mtx;
    std::condition_variable cv;
    int event_count = 0;

    zk_.Watch(path, [&](std::string new_data) {
        std::lock_guard lock(mtx);
        received_data.push_back(new_data);
        event_count++;
        if (new_data.empty()) {
            node_deleted = true;
        }
        XRPC_LOG_DEBUG("Watcher triggered for {}: data={}", path, new_data);
        cv.notify_one();
    });

    // 注册节点
    ASSERT_NO_THROW(zk_.Register(path, data, true));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 1; });
    }
    ASSERT_EQ(received_data.size(), 1) << "Expected 1 event after register, got " << received_data.size();
    EXPECT_EQ(received_data[0], data);

    // 更新节点
    std::string new_data = "192.168.1.2:8081";
    ASSERT_NO_THROW(zk_.Register(path, new_data, true));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 2; });
    }
    ASSERT_EQ(received_data.size(), 2) << "Expected 2 events after update, got " << received_data.size();
    EXPECT_EQ(received_data[1], new_data);

    // 删除节点
    ASSERT_NO_THROW(zk_.Delete(path));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 3; });
    }
    ASSERT_EQ(received_data.size(), 3) << "Expected 3 events after delete, got " << received_data.size();
    EXPECT_TRUE(node_deleted);
    EXPECT_EQ(received_data[2], "");
}

TEST_F(ZookeeperClientTest, HeartbeatNodeCleanup) {
    std::string path = "/UserService/Other";
    std::string data = "127.0.0.1:8081";

    ASSERT_NO_THROW(zk_.Register(path, data, true));
    std::string discovered = zk_.Discover(path);
    EXPECT_EQ(discovered, data);

    ASSERT_NO_THROW(zk_.Delete(path));
    std::this_thread::sleep_for(std::chrono::seconds(12));
    EXPECT_THROW(zk_.Discover(path), std::runtime_error);
}

TEST_F(ZookeeperClientTest, WatchNonExistentNode) {
    XRPC_LOG_DEBUG("Starting WatchNonExistentNode test");
    std::string path = "/NonExistentService/UnknownMethod";
    std::string data = "127.0.0.1:8080";
    std::string received_data;
    bool callback_called = false;

    std::mutex mtx;
    std::condition_variable cv;

    XRPC_LOG_DEBUG("Setting up watcher for {}", path);
    ASSERT_NO_THROW(zk_.Watch(path, [&](std::string new_data) {
        XRPC_LOG_DEBUG("Watcher callback entered");
        std::lock_guard<std::mutex> lock(mtx);
        received_data = new_data;
        callback_called = true;
        cv.notify_one();
        XRPC_LOG_DEBUG("Watcher triggered for {}: data={}", path, new_data);
    }));

    XRPC_LOG_DEBUG("Registering node {}", path);
    ASSERT_NO_THROW(zk_.Register(path, data, true));
    
    {
        XRPC_LOG_DEBUG("Waiting for watcher callback");
        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::seconds(5), 
            [&]{ return callback_called; }) == false) {
            XRPC_LOG_ERROR("Timeout waiting for watcher callback");
            FAIL() << "Timeout waiting for watcher callback";
        }
    }
    
    EXPECT_EQ(received_data, data);
    
    XRPC_LOG_DEBUG("Cleaning up test node");
    ASSERT_NO_THROW(zk_.Delete(path));
    XRPC_LOG_DEBUG("WatchNonExistentNode test completed");
}

} // namespace xrpc