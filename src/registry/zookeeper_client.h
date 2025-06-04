#ifndef XRPC_REGISTRY_ZOOKEEPER_CLIENT_H
#define XRPC_REGISTRY_ZOOKEEPER_CLIENT_H

#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include <zookeeper/zookeeper.h>
#include <string>
#include <functional>
#include <mutex>
#include <map>
#include <atomic>
#include <vector>
#include <thread>
#include <memory>

namespace xrpc {

class ZookeeperClient {
public:
    ZookeeperClient();
    ~ZookeeperClient();

    void Start();
    void Stop(); // 新增方法
    void Register(const std::string& path, const std::string& data, bool ephemeral = false);
    std::string Discover(const std::string& path);
    std::vector<std::pair<std::string, std::string>> DiscoverService(const std::string& service);
    std::vector<std::string> FindInstancesByMethod(const std::string& service, const std::string& method);
    void Delete(const std::string& path);
    void Watch(const std::string& path, std::function<void(std::string)> callback);

private:
    void Heartbeat();
    std::string GetNodeData(const std::string& path);
    void RegisterWatcher(const std::string& path);
    static void WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context);

    zhandle_t* zk_handle_;
    std::atomic<bool> is_connected_;
    std::atomic<bool> running_;
    XrpcConfig config_;
    std::mutex cache_mutex_;
    std::mutex mutex_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> service_cache_;
    std::map<std::string, std::function<void(std::string)>> watchers_;
    std::thread heartbeat_thread_;
};

} // namespace xrpc

#endif // XRPC_REGISTRY_ZOOKEEPER_CLIENT_H