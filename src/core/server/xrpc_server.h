#ifndef XRPC_SERVER_H
#define XRPC_SERVER_H

#include "core/common/xrpc_common.h"
#include "core/common/xrpc_config.h"
#include "core/codec/xrpc_codec.h"
#include "registry/zookeeper_client.h"
#include "transport/asio_transport.h"
#include <google/protobuf/service.h>
#include <map>
#include <memory>
#include <string>

namespace xrpc {

class XrpcServer {
public:
    XrpcServer(const std::string& config_file);
    ~XrpcServer();

    // 注册服务
    void RegisterService(google::protobuf::Service* service);

    // 启动服务器
    void Start();

private:
    // 初始化 ZooKeeper 和 Asio
    void Init();

    // 处理接收到的请求
    void OnMessage(const std::string& data, std::string& response);

    // 调用服务方法
    void CallServiceMethod(const ServiceDescriptor& desc,
                          google::protobuf::Message* request,
                          google::protobuf::Message* response);

    XrpcConfig config_;
    XrpcCodec codec_;
    std::unique_ptr<ZookeeperClient> zk_client_;
    std::unique_ptr<AsioTransport> transport_;
    std::map<std::string, google::protobuf::Service*> services_;
    std::string server_ip_;
    int server_port_;
};

} // namespace xrpc

#endif // XRPC_SERVER_H