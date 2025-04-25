#ifndef XRPC_CHANNEL_H
#define XRPC_CHANNEL_H

#include "core/common/xrpc_config.h"
#include "core/common/xrpc_logger.h"
#include "registry/zookeeper_client.h"
#include "transport/muduo_transport.h"
#include <google/protobuf/service.h>
#include <string>
#include <memory>
#include <mutex>

namespace xrpc {

class XrpcChannel : public google::protobuf::RpcChannel {
public:
    XrpcChannel(const std::string& service_name, ZookeeperClient* zk_client);
    ~XrpcChannel() override;

    // 实现 RpcChannel 的 CallMethod
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done) override;

private:
    std::string GetServiceAddress(const std::string& method_name);
    void UpdateServiceAddress(const std::string& method_name, const std::string& new_address);

    std::string service_name_;
    ZookeeperClient* zk_client_;
    std::unique_ptr<MuduoTransport> transport_;
    XrpcConfig config_;
    std::mutex mutex_;
    std::map<std::string, std::string> address_cache_; // 方法名 -> 地址
};

} // namespace xrpc

#endif // XRPC_CHANNEL_H
