#include "registry/zookeeper_client.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <zookeeper/zookeeper.h>

namespace xrpc {

ZookeeperClient::ZookeeperClient() : zk_handle_(nullptr), is_connected_(false), running_(false) {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
}

ZookeeperClient::~ZookeeperClient() {
    Stop();
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

    int max_retries = 3;
    int retry_interval_ms = 500;
    for (int i = 0; i < max_retries && !is_connected_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
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
    heartbeat_thread_ = std::thread([this]() { Heartbeat(); });
}

void ZookeeperClient::Stop() {
    running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    {
        std::lock_guard lock(cache_mutex_);
        watchers_.clear();
        service_cache_.clear();
    }
    if (zk_handle_) {
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
    }
    XRPC_LOG_DEBUG("ZookeeperClient stopped");
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
        std::string service = path.substr(1, path.find('/', 1) - 1);
        auto& instances = service_cache_[service];
        auto it = std::find_if(instances.begin(), instances.end(),
                               [&path](const auto& p) { return p.first == path; });
        if (it != instances.end()) {
            it->second = data;
        } else {
            instances.emplace_back(path, data);
        }
    }

    XRPC_LOG_INFO("Registered node {} with data: {}", path, data);
}

std::string ZookeeperClient::Discover(const std::string& path) {
    std::lock_guard lock(mutex_);
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1);
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            for (const auto& instance : it->second) {
                if (instance.first == path) {
                    XRPC_LOG_DEBUG("Cache hit for node {}: {}", path, instance.second);
                    return instance.second;
                }
            }
        }
    }

    std::string data = GetNodeData(path);
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1);
        auto& instances = service_cache_[service];
        auto it = std::find_if(instances.begin(), instances.end(),
                               [&path](const auto& p) { return p.first == path; });
        if (it != instances.end()) {
            it->second = data;
        } else {
            instances.emplace_back(path, data);
        }
    }
    return data;
}

std::vector<std::pair<std::string, std::string>> ZookeeperClient::DiscoverService(const std::string& service) {
    std::lock_guard lock(mutex_);
    {
        std::lock_guard lock(cache_mutex_);
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            XRPC_LOG_DEBUG("Cache hit for service {}", service);
            return it->second;
        }
    }

    String_vector children;
    std::string service_path = "/" + service;
    int ret = zoo_get_children(zk_handle_, service_path.c_str(), 0, &children);
    if (ret != ZOK) {
        XRPC_LOG_ERROR("Failed to get children for {}: {}", service, zerror(ret));
        throw std::runtime_error("Failed to get children: " + std::string(zerror(ret)));
    }

    std::vector<std::pair<std::string, std::string>> instances;
    for (int i = 0; i < children.count; ++i) {
        std::string path = service_path + "/" + children.data[i];
        try {
            std::string data = GetNodeData(path);
            instances.emplace_back(path, data);
        } catch (const std::exception& e) {
            XRPC_LOG_WARN("Failed to get data for {}: {}", path, e.what());
        }
    }
    deallocate_String_vector(&children);

    {
        std::lock_guard lock(cache_mutex_);
        service_cache_[service] = instances;
    }
    return instances;
}

std::vector<std::string> ZookeeperClient::FindInstancesByMethod(const std::string& service, const std::string& method) {
    auto instances = DiscoverService(service);
    std::vector<std::string> matching_instances;
    for (const auto& [path, data] : instances) {
        if (data.find("methods=" + method) != std::string::npos ||
            data.find("methods=" + method + ",") != std::string::npos ||
            data.find("," + method) != std::string::npos) {
            matching_instances.push_back(path.substr(path.rfind('/') + 1));
        }
    }
    return matching_instances;
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
        std::string service = path.substr(1, path.find('/', 1) - 1);
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            auto& instances = it->second;
            instances.erase(std::remove_if(instances.begin(), instances.end(),
                                           [&path](const auto& p) { return p.first == path; }),
                            instances.end());
            if (instances.empty()) {
                service_cache_.erase(service);
            }
        }
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
        std::vector<std::string> services;
        {
            std::lock_guard lock(cache_mutex_);
            for (const auto& pair : service_cache_) {
                services.push_back(pair.first);
            }
        }

        for (const auto& service : services) {
            String_vector children;
            std::string service_path = "/" + service;
            int ret = zoo_get_children(zk_handle_, service_path.c_str(), 0, &children);
            if (ret != ZOK) {
                XRPC_LOG_WARN("Failed to get children for {}: {}", service, zerror(ret));
                continue;
            }

            std::vector<std::string> current_paths;
            for (int i = 0; i < children.count; ++i) {
                current_paths.push_back(service_path + "/" + children.data[i]);
            }
            deallocate_String_vector(&children);

            std::lock_guard lock(cache_mutex_);
            auto it = service_cache_.find(service);
            if (it != service_cache_.end()) {
                auto& instances = it->second;
                instances.erase(std::remove_if(instances.begin(), instances.end(),
                                               [current_paths](const auto& p) {
                                                   return std::find(current_paths.begin(), current_paths.end(), p.first) == current_paths.end();
                                               }),
                                instances.end());
                if (instances.empty()) {
                    service_cache_.erase(service);
                }
            }
        }

        // 缩短心跳间隔以优化测试
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void ZookeeperClient::WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context) {
    ZookeeperClient* client = static_cast<ZookeeperClient*>(context);
    if (!client || !client->zk_handle_ || !client->running_) {
        XRPC_LOG_DEBUG("Skipping WatcherCallback due to invalid client state");
        return;
    }

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            client->is_connected_ = true;
            XRPC_LOG_INFO("ZooKeeper session connected");
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            client->is_connected_ = false;
            XRPC_LOG_ERROR("ZooKeeper session expired");
            {
                std::lock_guard lock(client->cache_mutex_);
                client->service_cache_.clear();
            }
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
                    std::string service = node_path.substr(1, node_path.find('/', 1) - 1);
                    auto& instances = client->service_cache_[service];
                    auto it = std::find_if(instances.begin(), instances.end(),
                                           [&node_path](const auto& p) { return p.first == node_path; });
                    if (it != instances.end()) {
                        it->second = data;
                    } else {
                        instances.emplace_back(node_path, data);
                    }
                }
                callback(data);
                client->RegisterWatcher(node_path);
            } catch (const std::exception& e) {
                XRPC_LOG_ERROR("Failed to handle node event: {}", e.what());
            }
        } else if (type == ZOO_DELETED_EVENT) {
            XRPC_LOG_DEBUG("Node {} deleted", node_path);
            {
                std::lock_guard lock(client->cache_mutex_);
                std::string service = node_path.substr(1, node_path.find('/', 1) - 1);
                auto it = client->service_cache_.find(service);
                if (it != client->service_cache_.end()) {
                    auto& instances = it->second;
                    instances.erase(std::remove_if(instances.begin(), instances.end(),
                                                   [&node_path](const auto& p) { return p.first == node_path; }),
                                    instances.end());
                    if (instances.empty()) {
                        client->service_cache_.erase(service);
                    }
                }
                client->watchers_.erase(node_path);
            }
            callback("");
            client->RegisterWatcher(node_path);
        } else if (type == ZOO_CHILD_EVENT) {
            XRPC_LOG_DEBUG("Child event for {}", node_path);
            std::string service = node_path.substr(1, node_path.find('/', 1) - 1);
            {
                std::lock_guard lock(client->cache_mutex_);
                client->service_cache_.erase(service);
            }
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