#ifndef XRPC_SERVER_H
#define XRPC_SERVER_H

#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include "registry/zookeeper_client.h"
#include "transport/muduo_transport.h"
#include <google/protobuf/service.h>
#include <memory>
#include <string>
#include <map>

namespace xrpc {

class XrpcServer {
public:
    XrpcServer();
    ~XrpcServer();

    // 添加服务
    void AddService(google::protobuf::Service* service);

    // 启动服务器
    void Start();

private:
    void RegisterServices();
    void OnRequest(const std::string& address, const std::string& data);

    std::unique_ptr<MuduoTransport> transport_;
    ZookeeperClient zk_client_;
    XrpcConfig config_;
    std::map<std::string, std::unique_ptr<google::protobuf::Service>> services_;
};

} // namespace xrpc

#endif // XRPC_SERVER_H
