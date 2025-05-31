#include "registry/zookeeper_client.h"
#include <stdexcept>
#include <thread>
#include <chrono>
#include <zookeeper/zookeeper.h>

namespace xrpc {

// 构造函数，初始化 ZooKeeper 客户端
// - 初始化 ZooKeeper 句柄、连接状态和运行标志
// - 加载配置文件，路径固定为 "/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf"
ZookeeperClient::ZookeeperClient() : zk_handle_(nullptr), is_connected_(false), running_(false) {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
}

// 析构函数，清理 ZooKeeper 客户端资源
// - 设置运行标志为 false，停止心跳线程
// - 清空 watchers_ 和 service_cache_，确保无内存泄漏
// - 关闭 ZooKeeper 连接并释放句柄
ZookeeperClient::~ZookeeperClient() {
    running_ = false;
    {
        std::lock_guard lock(cache_mutex_);
        watchers_.clear();
        service_cache_.clear();
    }
    if (zk_handle_) {
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
    }
}

// 启动 ZooKeeper 客户端
// - 从配置文件获取 ZooKeeper 地址和超时时间
// - 初始化 ZooKeeper 连接，设置 Watcher 回调
// - 尝试多次连接直到成功或达到最大重试次数
// - 成功连接后启动心跳线程
void ZookeeperClient::Start() {
    std::lock_guard lock(mutex_); // 加锁保护初始化过程
    // 获取 ZooKeeper 主机地址，默认为 "127.0.0.1:2181"
    std::string host = config_.Get("zookeeper_ip", "127.0.0.1") + ":" +
                       config_.Get("zookeeper_port", "2181");
    // 获取连接超时时间，默认为 6000ms
    int timeout_ms = std::stoi(config_.Get("zookeeper_timeout_ms", "6000"));

    // 初始化 ZooKeeper 客户端，绑定 WatcherCallback
    zk_handle_ = zookeeper_init(host.c_str(), WatcherCallback, timeout_ms, nullptr, this, 0);
    if (!zk_handle_) {
        XRPC_LOG_ERROR("Failed to initialize ZooKeeper client");
        throw std::runtime_error("Failed to initialize ZooKeeper client");
    }

    // 最多重试 5 次，每次间隔 1 秒，检查连接状态
    int max_retries = 5;
    for (int i = 0; i < max_retries && !is_connected_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (zoo_state(zk_handle_) == ZOO_CONNECTED_STATE) {
            is_connected_ = true;
            break;
        }
    }

    // 如果连接失败，记录错误并清理资源
    if (!is_connected_) {
        XRPC_LOG_ERROR("Failed to connect to ZooKeeper after {} retries", max_retries);
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
        throw std::runtime_error("Failed to connect to ZooKeeper");
    }

    XRPC_LOG_INFO("Connected to ZooKeeper: {}", host);
    running_ = true;
    // 启动心跳线程，定期检查服务实例状态
    std::thread([this]() { Heartbeat(); }).detach();
}

// 在 ZooKeeper 中注册或更新节点
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
//   - data: 节点数据（如 "methods=getUser"）
//   - ephemeral: 是否为临时节点（true 为临时节点，断开连接后自动删除）
// - 创建父节点（如果不存在）
// - 如果节点存在，更新数据；否则，创建新节点
// - 更新本地服务缓存 (service_cache_)
void ZookeeperClient::Register(const std::string& path, const std::string& data, bool ephemeral) {
    std::lock_guard lock(mutex_); // 加锁保护 ZooKeeper 操作
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    // 创建父节点（如 "/UserService"）
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

    // 检查节点是否存在
    struct Stat stat;
    int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);
    if (ret == ZOK) {
        // 节点存在，更新数据
        XRPC_LOG_DEBUG("Node {} already exists, updating data", path);
        ret = zoo_set(zk_handle_, path.c_str(), data.c_str(), data.size(), stat.version);
        if (ret != ZOK) {
            XRPC_LOG_ERROR("Failed to update node {}: {}", path, zerror(ret));
            throw std::runtime_error("Failed to update node: " + std::string(zerror(ret)));
        }
    } else {
        // 节点不存在，创建新节点
        int flags = ephemeral ? ZOO_EPHEMERAL : 0;
        ret = zoo_create(zk_handle_, path.c_str(), data.c_str(), data.size(),
                         &ZOO_OPEN_ACL_UNSAFE, flags, nullptr, 0);
        if (ret != ZOK) {
            XRPC_LOG_ERROR("Failed to create node {}: {}", path, zerror(ret));
            throw std::runtime_error("Failed to create node: " + std::string(zerror(ret)));
        }
    }

    // 更新本地服务缓存
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1); // 提取服务名，如 "UserService"
        auto& instances = service_cache_[service];
        auto it = std::find_if(instances.begin(), instances.end(),
                               [&path](const auto& p) { return p.first == path; });
        if (it != instances.end()) {
            it->second = data; // 更新现有实例数据
        } else {
            instances.emplace_back(path, data); // 添加新实例
        }
    }

    XRPC_LOG_INFO("Registered node {} with data: {}", path, data);
}

// 从缓存或 ZooKeeper 获取指定路径的节点数据
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
// 返回值：
//   - 节点数据（如 "methods=getUser"）
// - 优先从 service_cache_ 获取数据，若缓存未命中则查询 ZooKeeper
// - 更新 service_cache_ 以保持缓存一致性
std::string ZookeeperClient::Discover(const std::string& path) {
    std::lock_guard lock(mutex_); // 加锁保护 ZooKeeper 操作
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1); // 提取服务名
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            for (const auto& instance : it->second) {
                if (instance.first == path) {
                    XRPC_LOG_DEBUG("Cache hit for node {}: {}", path, instance.second);
                    return instance.second; // 缓存命中，直接返回
                }
            }
        }
    }

    // 缓存未命中，从 ZooKeeper 获取数据
    std::string data = GetNodeData(path);
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1);
        auto& instances = service_cache_[service];
        auto it = std::find_if(instances.begin(), instances.end(),
                               [&path](const auto& p) { return p.first == path; });
        if (it != instances.end()) {
            it->second = data; // 更新缓存中的实例数据
        } else {
            instances.emplace_back(path, data); // 添加新实例到缓存
        }
    }
    return data;
}

// 获取指定服务的所有实例列表
// 参数：
//   - service: 服务名（如 "UserService"）
// 返回值：
//   - 包含所有实例路径和数据的向量（如 {"/UserService/instance1", "methods=getUser"}）
// - 优先从 service_cache_ 获取，若缓存未命中则查询 ZooKeeper 的子节点
std::vector<std::pair<std::string, std::string>> ZookeeperClient::DiscoverService(const std::string& service) {
    std::lock_guard lock(mutex_); // 加锁保护 ZooKeeper 操作
    {
        std::lock_guard lock(cache_mutex_);
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            XRPC_LOG_DEBUG("Cache hit for service {}", service);
            return it->second; // 缓存命中，直接返回
        }
    }

    // 缓存未命中，查询 ZooKeeper
    String_vector children;
    std::string service_path = "/" + service;
    int ret = zoo_get_children(zk_handle_, service_path.c_str(), 0, &children);
    if (ret != ZOK) {
        XRPC_LOG_ERROR("Failed to get children for {}: {}", service, zerror(ret));
        throw std::runtime_error("Failed to get children: " + std::string(zerror(ret)));
    }

    // 遍历子节点，获取每个实例的数据
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
    deallocate_String_vector(&children); // 释放 ZooKeeper 子节点列表

    // 更新缓存
    {
        std::lock_guard lock(cache_mutex_);
        service_cache_[service] = instances;
    }
    return instances;
}

// 根据服务名和方法名查找支持特定方法的实例
// 参数：
//   - service: 服务名（如 "UserService"）
//   - method: 方法名（如 "getUser"）
// 返回值：
//   - 支持该方法的实例名称列表（如 {"instance1"}）
// - 通过 DiscoverService 获取实例列表，检查每个实例的数据是否包含指定方法
std::vector<std::string> ZookeeperClient::FindInstancesByMethod(const std::string& service, const std::string& method) {
    auto instances = DiscoverService(service); // 获取服务的所有实例
    std::vector<std::string> matching_instances;
    for (const auto& [path, data] : instances) {
        // 检查数据是否包含 "methods=method" 或 ",method" 或 "methods=method,"
        if (data.find("methods=" + method) != std::string::npos ||
            data.find("methods=" + method + ",") != std::string::npos ||
            data.find("," + method) != std::string::npos) {
            matching_instances.push_back(path.substr(path.rfind('/') + 1)); // 提取实例名
        }
    }
    return matching_instances;
}

// 删除指定路径的节点，并清理缓存和 watcher
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
// - 如果节点不存在，直接返回
// - 删除节点后，更新 service_cache_ 和 watchers_
void ZookeeperClient::Delete(const std::string& path) {
    std::lock_guard lock(mutex_); // 加锁保护 ZooKeeper 操作
    if (!zk_handle_) {
        throw std::runtime_error("ZookeeperClient::Delete - ZooKeeper client not started");
    }

    // 检查节点是否存在
    int exists = zoo_exists(zk_handle_, path.c_str(), 0, nullptr);
    if (exists == ZNONODE) {
        return; // 节点不存在，无需删除
    }

    if (exists != ZOK) {
        throw std::runtime_error("ZookeeperClient::Delete - Error checking node existence: " + std::string(zerror(exists)));
    }

    // 删除节点
    int rc = zoo_delete(zk_handle_, path.c_str(), -1);
    if (rc != ZOK) {
        throw std::runtime_error("ZookeeperClient::Delete - Failed to delete node: " + std::string(zerror(rc)));
    }

    // 更新缓存和 watcher
    {
        std::lock_guard lock(cache_mutex_);
        std::string service = path.substr(1, path.find('/', 1) - 1); // 提取服务名
        auto it = service_cache_.find(service);
        if (it != service_cache_.end()) {
            auto& instances = it->second;
            // 从实例列表中移除该路径
            instances.erase(std::remove_if(instances.begin(), instances.end(),
                                           [&path](const auto& p) { return p.first == path; }),
                            instances.end());
            // 如果服务实例列表为空，删除服务缓存
            if (instances.empty()) {
                service_cache_.erase(service);
            }
        }
        watchers_.erase(path); // 移除对应 watcher
    }
}

// 为指定路径的节点设置 watcher
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
//   - callback: 节点数据变更时的回调函数
// - 将回调存储到 watchers_ 并注册存在性 watcher
void ZookeeperClient::Watch(const std::string& path, std::function<void(std::string)> callback) {
    std::lock_guard lock(mutex_); // 加锁保护 ZooKeeper 操作
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    {
        std::lock_guard lock(cache_mutex_);
        watchers_[path] = callback; // 存储回调函数
    }
    RegisterWatcher(path); // 注册 watcher
}

// 为指定路径的节点设置存在性 watcher
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
// - 使用 zoo_wexists 设置节点存在性监控，触发事件时调用 WatcherCallback
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

// 心跳线程，定期检查缓存中的服务实例是否存在
// - 遍历 service_cache_ 中的服务，获取子节点列表
// - 移除不存在的实例，更新缓存
void ZookeeperClient::Heartbeat() {
    while (running_ && is_connected_ && zk_handle_) {
        std::vector<std::string> services;
        {
            std::lock_guard lock(cache_mutex_);
            for (const auto& pair : service_cache_) {
                services.push_back(pair.first); // 收集所有服务名
            }
        }

        // 检查每个服务的子节点
        for (const auto& service : services) {
            String_vector children;
            std::string service_path = "/" + service;
            int ret = zoo_get_children(zk_handle_, service_path.c_str(), 0, &children);
            if (ret != ZOK) {
                XRPC_LOG_WARN("Failed to get children for {}: {}", service, zerror(ret));
                continue;
            }

            // 构建当前子节点路径列表
            std::vector<std::string> current_paths;
            for (int i = 0; i < children.count; ++i) {
                current_paths.push_back(service_path + "/" + children.data[i]);
            }
            deallocate_String_vector(&children); // 释放子节点列表

            // 更新缓存，移除不存在的实例
            std::lock_guard lock(cache_mutex_);
            auto it = service_cache_.find(service);
            if (it != service_cache_.end()) {
                auto& instances = it->second;
                instances.erase(std::remove_if(instances.begin(), instances.end(),
                                               [&current_paths](const auto& p) {
                                                   return std::find(current_paths.begin(), current_paths.end(), p.first) == current_paths.end();
                                               }),
                                instances.end());
                // 如果服务实例列表为空，删除服务缓存
                if (instances.empty()) {
                    service_cache_.erase(service);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(10)); // 每 10 秒检查一次
    }
}

// ZooKeeper 事件回调函数，处理会话事件和节点事件
// 参数：
//   - zh: ZooKeeper 句柄
//   - type: 事件类型（如 ZOO_SESSION_EVENT, ZOO_CREATED_EVENT）
//   - state: 连接状态（如 ZOO_CONNECTED_STATE）
//   - path: 事件相关的节点路径
//   - context: 上下文，指向 ZookeeperClient 实例
// - 处理会话事件（连接、断开、过期）
// - 处理节点事件（创建、变更、删除、子节点变更）
void ZookeeperClient::WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context) {
    ZookeeperClient* client = static_cast<ZookeeperClient*>(context);
    if (!client || !client->zk_handle_ || !client->running_) {
        XRPC_LOG_ERROR("Invalid client state in WatcherCallback");
        return;
    }

    // 处理会话事件
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            client->is_connected_ = true;
            XRPC_LOG_INFO("ZooKeeper session connected");
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            client->is_connected_ = false;
            XRPC_LOG_ERROR("ZooKeeper session expired");
            {
                std::lock_guard lock(client->cache_mutex_);
                client->service_cache_.clear(); // 会话过期，清空缓存
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
                callback = it->second; // 获取注册的回调函数
            }
        }

        if (!callback) {
            XRPC_LOG_DEBUG("No watcher found for node {}", node_path);
            return;
        }

        // 处理节点创建或变更事件
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
                        it->second = data; // 更新缓存中的实例数据
                    } else {
                        instances.emplace_back(node_path, data); // 添加新实例
                    }
                }
                callback(data); // 调用回调函数
                client->RegisterWatcher(node_path); // 重新注册 watcher
            } catch (const std::exception& e) {
                XRPC_LOG_ERROR("Failed to handle node event: {}", e.what());
            }
        } else if (type == ZOO_DELETED_EVENT) {
            // 处理节点删除事件
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
                        client->service_cache_.erase(service); // 删除空服务缓存
                    }
                }
                client->watchers_.erase(node_path); // 移除 watcher
            }
            callback(""); // 通知节点删除
            client->RegisterWatcher(node_path); // 重新注册 watcher 以监控未来创建
        } else if (type == ZOO_CHILD_EVENT) {
            // 处理子节点变更事件
            XRPC_LOG_DEBUG("Child event for {}", node_path);
            std::string service = node_path.substr(1, node_path.find('/', 1) - 1);
            {
                std::lock_guard lock(client->cache_mutex_);
                client->service_cache_.erase(service); // 清除服务缓存，触发下次查询刷新
            }
        }
    }
}

// 从 ZooKeeper 获取指定路径的节点数据
// 参数：
//   - path: 节点路径（如 "/UserService/instance1"）
// 返回值：
//   - 节点数据（如 "methods=getUser"）
// - 检查连接状态，使用 zoo_get 获取数据
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