#ifndef XRPC_CHANNEL_H
#define XRPC_CHANNEL_H

#include "core/common/xrpc_common.h"
#include "core/common/xrpc_config.h"
#include "core/codec/xrpc_codec.h"
#include "registry/zookeeper_client.h"
#include "transport/asio_transport.h"
#include <google/protobuf/service.h>
#include <memory>
#include <string>
#include <mutex>

namespace xrpc {

class XrpcChannel : public google::protobuf::RpcChannel {
public:
    XrpcChannel(const std::string& config_file);
    ~XrpcChannel();

    // 实现 RpcChannel 的 CallMethod
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                   google::protobuf::RpcController* controller,
                   const google::protobuf::Message* request,
                   google::protobuf::Message* response,
                   google::protobuf::Closure* done) override;

private:
    // 初始化 ZooKeeper
    void Init();

    // 获取服务地址
    std::string GetServiceAddress(const std::string& service_name, const std::string& method_name);

    // 发送请求并接收响应（同步）
    bool SendRequest(const std::string& data, std::string& response);

    // 发送请求并接收响应（异步）
    void SendRequestAsync(const std::string& data,
                         google::protobuf::RpcController* controller,
                         google::protobuf::Message* response,
                         google::protobuf::Closure* done);

    XrpcConfig config_;
    XrpcCodec codec_;
    std::unique_ptr<ZookeeperClient> zk_client_;
    std::unique_ptr<AsioTransport> transport_;
    std::mutex mutex_;
};

} // namespace xrpc

#endif // XRPC_CHANNEL_H