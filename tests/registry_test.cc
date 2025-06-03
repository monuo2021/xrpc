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
        zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
        zk_ = std::make_unique<ZookeeperClient>();
        ASSERT_NO_THROW(zk_->Start());
    }

    void TearDown() override {
        try {
            zk_->Delete("/UserService/127.0.0.1:8080");
            zk_->Delete("/UserService/192.168.1.2:8081");
            zk_->Delete("/UserService/127.0.0.1:8081");
            zk_->Delete("/NonExistentService/127.0.0.1:9999");
        } catch (const std::exception& e) {
            XRPC_LOG_WARN("Failed to clean up node in TearDown: {}", e.what());
        }
        zk_.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::unique_ptr<ZookeeperClient> zk_;
};

TEST_F(ZookeeperClientTest, RegisterAndDiscover) {
    std::string path = "/UserService/127.0.0.1:8080";
    std::string data = "methods=Login";

    ASSERT_NO_THROW(zk_->Register(path, data, true));
    std::string discovered = zk_->Discover(path);
    EXPECT_EQ(discovered, data);

    ASSERT_NO_THROW(zk_->Register(path, data, true));

    std::string new_path = "/UserService/192.168.1.2:8081";
    std::string new_data = "methods=Login";
    ASSERT_NO_THROW(zk_->Register(new_path, new_data, true));
    discovered = zk_->Discover(new_path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, DiscoverService) {
    std::string path1 = "/UserService/127.0.0.1:8080";
    std::string data1 = "methods=Login";
    std::string path2 = "/UserService/192.168.1.2:8081";
    std::string data2 = "methods=Login,Register";

    ASSERT_NO_THROW(zk_->Register(path1, data1, true));
    ASSERT_NO_THROW(zk_->Register(path2, data2, true));

    auto instances = zk_->DiscoverService("UserService");
    ASSERT_EQ(instances.size(), 2);
    EXPECT_TRUE(std::find_if(instances.begin(), instances.end(),
                             [&path1, &data1](const auto& p) { return p.first == path1 && p.second == data1; }) != instances.end());
    EXPECT_TRUE(std::find_if(instances.begin(), instances.end(),
                             [&path2, &data2](const auto& p) { return p.first == path2 && p.second == data2; }) != instances.end());
}

TEST_F(ZookeeperClientTest, FindInstancesByMethod) {
    std::string path1 = "/UserService/127.0.0.1:8080";
    std::string data1 = "methods=Login";
    std::string path2 = "/UserService/192.168.1.2:8081";
    std::string data2 = "methods=Login,Register";

    ASSERT_NO_THROW(zk_->Register(path1, data1, true));
    ASSERT_NO_THROW(zk_->Register(path2, data2, true));

    auto instances = zk_->FindInstancesByMethod("UserService", "Login");
    ASSERT_EQ(instances.size(), 2);
    EXPECT_TRUE(std::find(instances.begin(), instances.end(), "127.0.0.1:8080") != instances.end());
    EXPECT_TRUE(std::find(instances.begin(), instances.end(), "192.168.1.2:8081") != instances.end());

    instances = zk_->FindInstancesByMethod("UserService", "Register");
    ASSERT_EQ(instances.size(), 1);
    EXPECT_EQ(instances[0], "192.168.1.2:8081");
}

TEST_F(ZookeeperClientTest, DiscoverNonExistent) {
    std::string path = "/NonExistentService/127.0.0.1:9999";
    EXPECT_THROW(zk_->Discover(path), std::runtime_error);
    EXPECT_TRUE(zk_->DiscoverService("NonExistentService").empty());
}

TEST_F(ZookeeperClientTest, DeleteNode) {
    std::string path = "/UserService/127.0.0.1:8080";
    std::string data = "methods=Login";

    ASSERT_NO_THROW(zk_->Register(path, data, true));
    ASSERT_NO_THROW(zk_->Delete(path));
    EXPECT_THROW(zk_->Discover(path), std::runtime_error);
    ASSERT_NO_THROW(zk_->Delete(path));

    std::string new_path = "/UserService/192.0.0.1:8081";
    std::string new_data = "methods=Login";
    ASSERT_NO_THROW(zk_->Register(new_path, new_data, true));
    std::string discovered = zk_->Discover(new_path);
    EXPECT_EQ(discovered, new_data);
}

TEST_F(ZookeeperClientTest, WatchNode) {
    std::string path = "/UserService/127.0.0.1:8080";
    std::string data = "methods=Login";
    std::vector<std::string> received_data;
    bool node_deleted = false;

    std::mutex mtx;
    std::condition_variable cv;
    int event_count = 0;

    zk_->Watch(path, [&](std::string new_data) {
        std::lock_guard lock(mtx);
        received_data.push_back(new_data);
        event_count++;
        if (new_data.empty()) {
            node_deleted = true;
        }
        XRPC_LOG_DEBUG("Watcher triggered for {}: data={}", path, new_data);
        cv.notify_one();
    });

    ASSERT_NO_THROW(zk_->Register(path, data, true));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 1; });
    }
    ASSERT_EQ(received_data.size(), 1);
    EXPECT_EQ(received_data[0], data);

    std::string new_data = "methods=Login,Register";
    ASSERT_NO_THROW(zk_->Register(path, new_data, true));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 2; });
    }
    ASSERT_EQ(received_data.size(), 2);
    EXPECT_EQ(received_data[1], new_data);

    ASSERT_NO_THROW(zk_->Delete(path));
    {
        std::unique_lock lock(mtx);
        cv.wait_for(lock, std::chrono::milliseconds(5000), [&]{ return event_count >= 3; });
    }
    ASSERT_EQ(received_data.size(), 3);
    EXPECT_TRUE(node_deleted);
    EXPECT_EQ(received_data[2], "");
}

TEST_F(ZookeeperClientTest, HeartbeatNodeCleanup) {
    std::string path = "/UserService/127.0.0.1:8081";
    std::string data = "methods=Other";

    ASSERT_NO_THROW(zk_->Register(path, data, true));
    std::string discovered = zk_->Discover(path);
    EXPECT_EQ(discovered, data);

    ASSERT_NO_THROW(zk_->Delete(path));
    std::this_thread::sleep_for(std::chrono::seconds(12));
    EXPECT_THROW(zk_->Discover(path), std::runtime_error);
    auto instances = zk_->DiscoverService("UserService");
    EXPECT_TRUE(instances.empty());
}

TEST_F(ZookeeperClientTest, WatchNonExistentNode) {
    std::string path = "/NonExistentService/127.0.0.1:9999";
    std::string data = "methods=Login";
    std::vector<std::string> received_data;
    bool callback_called = false;
    bool node_deleted = false;

    std::mutex mtx;
    std::condition_variable cv;
    int event_count = 0;

    ASSERT_NO_THROW(zk_->Watch(path, [&](std::string new_data) {
        std::lock_guard<std::mutex> lock(mtx);
        received_data.push_back(new_data);
        event_count++;
        if (new_data.empty()) {
            node_deleted = true;
        } else {
            callback_called = true;
        }
        XRPC_LOG_DEBUG("Watcher triggered for {}: data={}", path, new_data);
        cv.notify_one();
    }));

    ASSERT_NO_THROW(zk_->Register(path, data, true));
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&]{ return callback_called; })) {
            XRPC_LOG_ERROR("Timeout waiting for watcher callback");
            FAIL() << "Timeout waiting for watcher callback";
        }
    }

    EXPECT_EQ(received_data.size(), 1);
    EXPECT_EQ(received_data[0], data);

    ASSERT_NO_THROW(zk_->Delete(path));
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5), [&]{ return node_deleted; })) {
            XRPC_LOG_ERROR("Timeout waiting for node deletion callback");
            FAIL() << "Timeout waiting for node deletion callback";
        }
    }

    EXPECT_EQ(received_data.size(), 2);
    EXPECT_EQ(received_data[1], "");
}

} // namespace xrpc