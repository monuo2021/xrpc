#include "core/channel/xrpc_channel.h"
#include "core/common/xrpc_logger.h"
#include "xrpc.pb.h"
#include <stdexcept>
#include <sstream>

namespace xrpc {

XrpcChannel::XrpcChannel(const std::string& config_file) : zk_client_(new ZookeeperClient), transport_(new AsioTransport) {
    config_.Load(config_file);
    Init();
}

XrpcChannel::~XrpcChannel() {
    // transport_ 和 zk_client_ 由 unique_ptr 自动释放
}

void XrpcChannel::Init() {
    // 初始化 ZooKeeper
    zk_client_->Start(); // 从 XrpcConfig 获取配置
}

std::string XrpcChannel::GetServiceAddress(const std::string& service_name, const std::string& method_name) {
    // 通过服务名和方法名查找实例
    std::vector<std::string> instances = zk_client_->FindInstancesByMethod(service_name, method_name);
    if (instances.empty()) {
        XRPC_LOG_ERROR("No instances found for service {} method {}", service_name, method_name);
        throw std::runtime_error("Service instance not found");
    }

    // 选择第一个实例（可扩展为负载均衡）
    std::string address = instances[0];
    XRPC_LOG_DEBUG("Discovered service {} method {} at {}", service_name, method_name, address);

    // 解析地址为 ip:port
    size_t colon_pos = address.find(':');
    if (colon_pos == std::string::npos) {
        XRPC_LOG_ERROR("Invalid address format: {}", address);
        throw std::runtime_error("Invalid address format");
    }

    std::string ip = address.substr(0, colon_pos);
    int port = std::stoi(address.substr(colon_pos + 1));
    return address;
}

bool XrpcChannel::SendRequest(const std::string& data, std::string& response) {
    bool success = transport_->Send(data, response);
    if (!success) {
        XRPC_LOG_ERROR("Failed to send request");
        return false;
    }
    return true;
}

void XrpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                            google::protobuf::RpcController* controller,
                            const google::protobuf::Message* request,
                            google::protobuf::Message* response,
                            google::protobuf::Closure* done) {
    try {
        // 获取服务和方法名
        std::string service_name = method->service()->name();
        std::string method_name = method->name();

        // 获取服务地址并连接
        std::string address = GetServiceAddress(service_name, method_name);
        size_t colon_pos = address.find(':');
        std::string server_ip = address.substr(0, colon_pos);
        int server_port = std::stoi(address.substr(colon_pos + 1));
        transport_->Connect(server_ip, server_port);

        // 构造 RpcHeader
        RpcHeader header;
        header.set_service_name(service_name);
        header.set_method_name(method_name);
        header.set_request_id(rand()); // 简单生成请求 ID
        header.set_compressed(false); // 默认不压缩

        // 序列化请求
        std::string data = codec_.Encode(header, *request);

        // 发送请求并接收响应
        std::string response_data;
        if (!SendRequest(data, response_data)) {
            controller->SetFailed("Failed to send request");
            if (done) done->Run();
            return;
        }

        // 解码响应
        if (!codec_.DecodeResponse(response_data, *response)) {
            controller->SetFailed("Failed to decode response");
            if (done) done->Run();
            return;
        }

        XRPC_LOG_INFO("Successfully called {}.{}", service_name, method_name);
        if (done) done->Run();
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("CallMethod failed: {}", e.what());
        controller->SetFailed(e.what());
        if (done) done->Run();
    }
}

} // namespace xrpc