#include "registry/zookeeper_client.h"
#include <stdexcept>
#include <thread>
#include <chrono>

namespace xrpc {

// 构造函数，初始化 zk 句柄、连接状态、运行标志，并加载配置文件
ZookeeperClient::ZookeeperClient() : zk_handle_(nullptr), is_connected_(false), running_(false) {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
}

// 析构函数，清理资源，关闭 Zookeeper 连接并清空 watcher 列表。
ZookeeperClient::~ZookeeperClient() {
    // 停止心跳线程
    running_ = false;
    {
        // 加锁以确保线程安全，清理 watcher 列表
        std::lock_guard lock(cache_mutex_);
        watchers_.clear();
    }
    // 如果 Zookeeper 句柄存在，则关闭连接
    if (zk_handle_) {
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
    }
}

// 启动 ZooKeeper 客户端，尝试多次连接直到成功或达到最大重试次数，并启动心跳线程。
void ZookeeperClient::Start() {
    std::lock_guard lock(mutex_);

    // 从配置中读取 host 和 timeout
    std::string host = config_.Get("zookeeper_ip", "127.0.0.1") + ":" +
                       config_.Get("zookeeper_port", "2181");
    int timeout_ms = std::stoi(config_.Get("zookeeper_timeout_ms", "6000"));

    // 初始化连接，设置回调函数 WatcherCallback
    zk_handle_ = zookeeper_init(host.c_str(), WatcherCallback, timeout_ms, nullptr, this, 0);
    if (!zk_handle_) {
        XRPC_LOG_ERROR("Failed to initialize ZooKeeper client");
        throw std::runtime_error("Failed to initialize ZooKeeper client");
    }

    // 尝试等待连接建立，最多重试5次
    int max_retries = 5;
    for (int i = 0; i < max_retries && !is_connected_; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (zoo_state(zk_handle_) == ZOO_CONNECTED_STATE) {
            is_connected_ = true;
            break;
        }
    }

    // 如果连接失败，抛出异常
    if (!is_connected_) {
        XRPC_LOG_ERROR("Failed to connect to ZooKeeper after {} retries", max_retries);
        zookeeper_close(zk_handle_);
        zk_handle_ = nullptr;
        throw std::runtime_error("Failed to connect to ZooKeeper");
    }

    // 连接成功，记录日志。设置运行状态为 true
    XRPC_LOG_INFO("Connected to ZooKeeper: {}", host);
    running_ = true;

    // 启动后台线程维持心跳
    std::thread([this]() { Heartbeat(); }).detach();
}

// 在指定路径创建或更新节点，并可选设置节点为临时节点。
void ZookeeperClient::Register(const std::string& path, const std::string& data, bool ephemeral) {
    std::lock_guard lock(mutex_);
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    // 尝试创建父节点
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

    // 如果节点已存在，更新数据；否则创建节点
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
        // 节点不存在，创建新节点
        int flags = ephemeral ? ZOO_EPHEMERAL : 0;  // 设置是否为临时节点
        ret = zoo_create(zk_handle_, path.c_str(), data.c_str(), data.size(),
                         &ZOO_OPEN_ACL_UNSAFE, flags, nullptr, 0);
        if (ret != ZOK) {
            XRPC_LOG_ERROR("Failed to create node {}: {}", path, zerror(ret));
            throw std::runtime_error("Failed to create node: " + std::string(zerror(ret)));
        }
    }

    {
        // 加锁以确保缓存线程安全，更新缓存
        std::lock_guard lock(cache_mutex_);
        cache_[path] = data;
    }

    XRPC_LOG_INFO("Registered node {} with data: {}", path, data);
}

// 从缓存或 Zookeeper 获取指定路径的节点数据。获取指定路径的节点数据，首先尝试从缓存中获取，如果缓存中不存在，则从Zookeeper中获取。
std::string ZookeeperClient::Discover(const std::string& path) {
    std::lock_guard lock(mutex_);
    {
        // 检查缓存中是否已有数据
        std::lock_guard lock(cache_mutex_);
        auto it = cache_.find(path);
        if (it != cache_.end()) {
            // 缓存命中，记录日志并返回数据
            XRPC_LOG_DEBUG("Cache hit for node {}: {}", path, it->second);
            return it->second;
        }
    }

    // 缓存未命中，从 Zookeeper 获取数据
    std::string data = GetNodeData(path);
    {
        // 加锁以确保缓存线程安全，更新缓存
        std::lock_guard lock(cache_mutex_);
        cache_[path] = data;
    }
    return data;
}

// 删除指定路径的节点，并清理缓存和 watcher。
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
        // 加锁以确保缓存线程安全，清理缓存和 watcher
        std::lock_guard lock(cache_mutex_);
        cache_.erase(path);
        watchers_.erase(path);
    }
}

// 为指定路径的节点设置 watcher，监控节点变化。
void ZookeeperClient::Watch(const std::string& path, std::function<void(std::string)> callback) {
    std::lock_guard lock(mutex_);
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    {
        // 加锁以确保缓存线程安全，保存 watcher 回调
        std::lock_guard lock(cache_mutex_);
        watchers_[path] = callback;
    }
    // 注册 watcher
    RegisterWatcher(path);
}

// 为指定路径的节点设置存在性 watcher。
void ZookeeperClient::RegisterWatcher(const std::string& path) {
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected for watcher on {}", path);
        return;
    }

    // 设置存在性 watcher
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
            // 加锁以确保缓存线程安全，获取缓存中的所有路径
            std::lock_guard lock(cache_mutex_);
            for (const auto& pair : cache_) {
                paths.push_back(pair.first);
            }
        }

        // 检查每个节点是否存在
        for (const auto& path : paths) {
            struct Stat stat;
            int ret = zoo_exists(zk_handle_, path.c_str(), 0, &stat);
            // 节点不存在，清理缓存和 watcher
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

// 处理 Zookeeper 事件，如连接状态变化、节点创建、修改或删除。
void ZookeeperClient::WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context) {
    ZookeeperClient* client = static_cast<ZookeeperClient*>(context);
    if (!client || !client->zk_handle_) {
        XRPC_LOG_ERROR("Invalid client or zk_handle in WatcherCallback");
        return;
    }

    // 处理会话事件
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            // 会话连接成功
            client->is_connected_ = true;
            XRPC_LOG_INFO("ZooKeeper session connected");
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            // 会话过期
            client->is_connected_ = false;
            XRPC_LOG_ERROR("ZooKeeper session expired");
        } else if (state == ZOO_CONNECTING_STATE) {
            // 会话正在连接
            client->is_connected_ = false;
            XRPC_LOG_WARN("ZooKeeper session connecting");
        }
    } else if (path != nullptr) {
        // 处理节点相关事件
        std::string node_path(path);
        std::function<void(std::string)> callback;
        {
            // 加锁以查找 watcher 回调
            std::lock_guard lock(client->cache_mutex_);
            auto it = client->watchers_.find(node_path);
            if (it != client->watchers_.end()) {
                callback = it->second;
            }
        }

        // 没有找到 watcher，记录日志并返回
        if (!callback) {
            XRPC_LOG_DEBUG("No watcher found for node {}", node_path);
            return;
        }

        // 节点创建或数据变更
        if (type == ZOO_CREATED_EVENT || type == ZOO_CHANGED_EVENT) {
            try {
                // 获取节点数据
                std::string data = client->GetNodeData(node_path);
                XRPC_LOG_DEBUG("Node {} updated, data: {}", node_path, data);
                {
                    // 更新缓存
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
            // 节点删除
            XRPC_LOG_DEBUG("Node {} deleted", node_path);
            {
                // 清理缓存和 watcher
                std::lock_guard lock(client->cache_mutex_);
                client->cache_.erase(node_path);
                client->watchers_.erase(node_path);
            }
            // 调用 watcher 回调，传递空数据
            callback("");
            // 重新注册 Watcher 以监控未来创建
            client->RegisterWatcher(node_path);
        }
    }
}

// 从 Zookeeper 获取指定路径的节点数据。
std::string ZookeeperClient::GetNodeData(const std::string& path) {
    // 检查 Zookeeper 连接状态
    if (!is_connected_ || !zk_handle_) {
        XRPC_LOG_ERROR("ZooKeeper not connected");
        throw std::runtime_error("ZooKeeper not connected");
    }

    // 定义缓冲区并获取节点数据
    char buffer[512];
    int buffer_len = sizeof(buffer);
    struct Stat stat;
    int ret = zoo_get(zk_handle_, path.c_str(), 0, buffer, &buffer_len, &stat);
    if (ret != ZOK) {
        XRPC_LOG_ERROR("Failed to get node {}: {}", path, zerror(ret));
        throw std::runtime_error("Failed to get node: " + std::string(zerror(ret)));
    }

    // 返回节点数据
    return std::string(buffer, buffer_len);
}

} // namespace xrpc