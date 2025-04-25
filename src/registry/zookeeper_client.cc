#include "registry/zookeeper_client.h"
#include <stdexcept>
#include <thread>
#include <chrono>

namespace xrpc {

ZookeeperClient::ZookeeperClient() : zk_handle_(nullptr), is_connected_(false), running_(false) {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
}

ZookeeperClient::~ZookeeperClient() {
    running_ = false;
    {
        std::lock_guard lock(cache_mutex_);
        watchers_.clear();
    }
    if (zk_handle_) {
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
    }
}

void ZookeeperClient::Start() {
    std::lock_guard lock(mutex_);
    std::string host = config_.Get("zookeeper_ip", "127.0.0.1") + ":" +
                       config_.Get("zookeeper_port", "2181");
    int timeout_ms = std::stoi(config_.Get("zookeeper_timeout_ms", "6000"));

    zk_handle_ = zookeeper_init(host.c_str(), WatcherCallback, timeout_ms, nullptr, this, 0);
    if (!zk_handle_) {
        XRPC_LOG_ERROR("Failed to initialize ZooKeeper client");
        throw std::runtime_error("Failed to initialize ZooKeeper client");
    }

    int max_retries = 5;
    for (int i = 0; i < max_retries && !is_connected_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (zoo_state(zk_handle_) == ZOO_CONNECTED_STATE) {
            is_connected_ = true;
            break;
        }
    }

    if (!is_connected_) {
        XRPC_LOG_ERROR("Failed to connect to ZooKeeper after {} retries", max_retries);
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
        throw std::runtime_error("Failed to connect to ZooKeeper");
    }

    XRPC_LOG_INFO("Connected to ZooKeeper: {}", host);
    running_ = true;
    std::thread([this]() { Heartbeat(); }).detach();
}

void ZookeeperClient::Register(const std::string& path, const std::string& data, bool ephemeral) {
    std::lock_guard lock(mutex_);
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    size_t last_slash = path.rfind('/');
    if (last_slash != std::string::npos) {
        std::string parent = path.substr(0, last_slash);
        if (!parent.empty()) {
            int ret = zoo_create(zk_handle_, parent.c_str(), nullptr, 0, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
            if (ret != ZOK && ret != ZNODEEXISTS) {
                XRPC_LOG_ERROR("Failed to create parent node {}: {}", parent, zerror(ret));
                throw std::runtime_error("Failed to create parent node: " + std::string(zerror(ret)));
            }
        }
    }

    struct Stat stat;
    int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);
    if (ret == ZOK) {
        XRPC_LOG_DEBUG("Node {} already exists, updating data", path);
        ret = zoo_set(zk_handle_, path.c_str(), data.c_str(), data.size(), stat.version);
        if (ret != ZOK) {
            XRPC_LOG_ERROR("Failed to update node {}: {}", path, zerror(ret));
            throw std::runtime_error("Failed to update node: " + std::string(zerror(ret)));
        }
    } else {
        int flags = ephemeral ? ZOO_EPHEMERAL : 0;
        ret = zoo_create(zk_handle_, path.c_str(), data.c_str(), data.size(),
                         &ZOO_OPEN_ACL_UNSAFE, flags, nullptr, 0);
        if (ret != ZOK) {
            XRPC_LOG_ERROR("Failed to create node {}: {}", path, zerror(ret));
            throw std::runtime_error("Failed to create node: " + std::string(zerror(ret)));
        }
    }

    {
        std::lock_guard lock(cache_mutex_);
        cache_[path] = data;
    }

    XRPC_LOG_INFO("Registered node {} with data: {}", path, data);
}

std::string ZookeeperClient::Discover(const std::string& path) {
    std::lock_guard lock(mutex_);
    {
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            XRPC_LOG_DEBUG("Cache hit for node {}: {}", path, it->second);
            return it->second;
        }
    }

    std::string data = GetNodeData(path);
    {
        std::lock_guard lock(cache_mutex_);
        cache_[path] = data;
    }
    return data;
}

void ZookeeperClient::Delete(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (!zk_handle_) {
        throw std::runtime_error("ZookeeperClient::Delete - ZooKeeper client not started");
    }

    int exists = zoo_exists(zk_handle_, path.c_str(), 0, nullptr);
    if (exists == ZNONODE) {
        return;
    }

    if (exists != ZOK) {
        throw std::runtime_error("ZookeeperClient::Delete - Error checking node existence: " + std::string(zerror(exists)));
    }

    int rc = zoo_delete(zk_handle_, path.c_str(), -1);
    if (rc != ZOK) {
        throw std::runtime_error("ZookeeperClient::Delete - Failed to delete node: " + std::string(zerror(rc)));
    }

    {
        std::lock_guard lock(cache_mutex_);
        cache_.erase(path);
        watchers_.erase(path);
    }
}

void ZookeeperClient::Watch(const std::string& path, std::function<void(std::string)> callback) {
    std::lock_guard lock(mutex_);
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    {
        std::lock_guard lock(cache_mutex_);
        watchers_[path] = callback;
    }
    RegisterWatcher(path);
}

void ZookeeperClient::RegisterWatcher(const std::string& path) {
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected for watcher on {}", path);
        return;
    }

    struct Stat stat;
    int ret = zoo_wexists(zk_handle_, path.c_str(), WatcherCallback, this, &stat);
    if (ret != ZOK && ret != ZNONODE) {
        XRPC_LOG_ERROR("Failed to set existence watch on {}: {}", path, zerror(ret));
        throw std::runtime_error("Failed to set watch: " + std::string(zerror(ret)));
    }

    XRPC_LOG_DEBUG("Set watch on node {}", path);
}

void ZookeeperClient::Heartbeat() {
    while (running_ && is_connected_ && zk_handle_) {
        std::vector<std::string> paths;
        {
            std::lock_guard lock(cache_mutex_);
            for (const auto& pair : cache_) {
                paths.push_back(pair.first);
            }
        }

        for (const auto& path : paths) {
            struct Stat stat;
            int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);
            if (ret != ZOK) {
                XRPC_LOG_WARN("Node {} no longer exists: {}", path, zerror(ret));
                {
                    std::lock_guard lock(cache_mutex_);
                    cache_.erase(path);
                    watchers_.erase(path);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void ZookeeperClient::WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context) {
    ZookeeperClient* client = static_cast<ZookeeperClient*>(context);
    if (!client || !client->zk_handle_) {
        XRPC_LOG_ERROR("Invalid client or zk_handle in WatcherCallback");
        return;
    }

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            client->is_connected_ = true;
            XRPC_LOG_INFO("ZooKeeper session connected");
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            client->is_connected_ = false;
            XRPC_LOG_ERROR("ZooKeeper session expired");
        } else if (state == ZOO_CONNECTING_STATE) {
            client->is_connected_ = false;
            XRPC_LOG_WARN("ZooKeeper session connecting");
        }
    } else if (path != nullptr) {
        std::string node_path(path);
        std::function<void(std::string)> callback;
        {
            std::lock_guard lock(client->cache_mutex_);
            auto it = client->watchers_.find(node_path);
            if (it != client->watchers_.end()) {
                callback = it->second;
            }
        }

        if (!callback) {
            XRPC_LOG_DEBUG("No watcher found for node {}", node_path);
            return;
        }

        if (type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT) {
            try {
                std::string data = client->GetNodeData(node_path);
                XRPC_LOG_DEBUG("Node {} updated, data: {}", node_path, data);
                {
                    std::lock_guard lock(client->cache_mutex_);
                    client->cache_[node_path] = data;
                }
                callback(data);
                // 重新注册 Watcher
                client->RegisterWatcher(node_path);
            } catch (const std::exception& e) {
                XRPC_LOG_ERROR("Failed to handle node event: {}", e.what());
            }
        } else if (type == ZOO_DELETED_EVENT) {
            XRPC_LOG_DEBUG("Node {} deleted", node_path);
            {
                std::lock_guard lock(client->cache_mutex_);
                client->cache_.erase(node_path);
                client->watchers_.erase(node_path);
            }
            callback("");
            // 重新注册 Watcher 以监控未来创建
            client->RegisterWatcher(node_path);
        }
    }
}

std::string ZookeeperClient::GetNodeData(const std::string& path) {
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    char buffer[512];
    int buffer_len = sizeof(buffer);
    struct Stat stat;
    int ret = zoo_get(zk_handle_, path.c_str(), 0, buffer, &buffer_len, &stat);
    if (ret != ZOK) {
        XRPC_LOG_ERROR("Failed to get node {}: {}", path, zerror(ret));
        throw std::runtime_error("Failed to get node: " + std::string(zerror(ret)));
    }

    return std::string(buffer, buffer_len);
}

} // namespace xrpc