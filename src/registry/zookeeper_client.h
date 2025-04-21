#ifndef XRPC_REGISTRY_ZOOKEEPER_CLIENT_H
#define XRPC_REGISTRY_ZOOKEEPER_CLIENT_H

#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include <zookeeper/zookeeper.h>
#include <string>
#include <functional>
#include <mutex>
#include <map>

namespace xrpc {

class ZookeeperClient {
public:
    ZookeeperClient();
    ~ZookeeperClient();

    void Start();
    void Register(const std::string& path, const std::string& data, bool ephemeral = false);
    std::string Discover(const std::string& path);
    void Watch(const std::string& path, std::function<void(std::string)> callback);

private:
    void Heartbeat();
    std::string GetNodeData(const std::string& path);
    static void WatcherCallback(zhandle_t* zh, int type, int state, const char* path, void* context);

    zhandle_t* zk_handle_;
    bool is_connected_;
    XrpcConfig config_;
    std::mutex cache_mutex_;
    std::map<std::string, std::string> cache_;
    std::map<std::string, std::function<void(std::string)>> watchers_; // 存储路径和回调
};

} // namespace xrpc

#endif // XRPC_REGISTRY_ZOOKEEPER_CLIENT_H