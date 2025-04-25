#include "core/channel/xrpc_channel.h"
#include "core/codec/xrpc_codec.h"
#include <google/protobuf/descriptor.h>
#include <stdexcept>

namespace xrpc {

XrpcChannel::XrpcChannel(const std::string& service_name, ZookeeperClient* zk_client)
    : service_name_(service_name), zk_client_(zk_client) {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
    transport_ = std::make_unique<MuduoTransport>();
    XRPC_LOG_INFO("XrpcChannel initialized for service: {}", service_name_);
}

XrpcChannel::~XrpcChannel() = default;

void XrpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                             google::protobuf::RpcController* controller,
                             const google::protobuf::Message* request,
                             google::protobuf::Message* response,
                             google::protobuf::Closure* done) {
    std::string method_name = method->name();
    std::string path = "/" + service_name_ + "/" + method_name;

    // 获取服务地址
    std::string address;
    try {
        address = GetServiceAddress(method_name);
    } catch (const std::exception& e) {
        controller->SetFailed("Service discovery failed: " + std::string(e.what()));
        XRPC_LOG_ERROR("Failed to discover service for {}: {}", path, e.what());
        if (done) done->Run();
        return;
    }

    // 设置 Watcher，更新地址
    zk_client_->Watch(path, [this, method_name](const std::string& new_address) {
        UpdateServiceAddress(method_name, new_address);
    });

    // 序列化请求
    XrpcCodec codec;
    RpcHeader header;
    header.set_service_name(service_name_);
    header.set_method_name(method_name);
    header.set_request_id(std::hash<std::string>{}(path + std::to_string(time(nullptr))));
    header.set_compressed(true);

    std::string encoded;
    try {
        encoded = codec.Encode(header, *request);
    } catch (const std::exception& e) {
        controller->SetFailed("Serialization failed: " + std::string(e.what()));
        XRPC_LOG_ERROR("Failed to serialize request: {}", e.what());
        if (done) done->Run();
        return;
    }

    // 发送请求并接收响应
    std::string response_data;
    try {
        response_data = transport_->SendRequest(address, encoded);
    } catch (const std::exception& e) {
        controller->SetFailed("Transport failed: " + std::string(e.what()));
        XRPC_LOG_ERROR("Transport failed for {}: {}", address, e.what());
        if (done) done->Run();
        return;
    }

    // 解码响应
    try {
        if (!codec.DecodeResponse(response_data, *response)) {
            controller->SetFailed("Failed to decode response");
            XRPC_LOG_ERROR("Failed to decode response for {}", path);
            if (done) done->Run();
            return;
        }
    } catch (const std::exception& e) {
        controller->SetFailed("Response parsing failed: " + std::string(e.what()));
        XRPC_LOG_ERROR("Response parsing failed: {}", e.what());
        if (done) done->Run();
        return;
    }

    XRPC_LOG_DEBUG("Successfully called {}: response parsed", path);
    if (done) done->Run();
}

std::string XrpcChannel::GetServiceAddress(const std::string& method_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string path = "/" + service_name_ + "/" + method_name;
    auto it = address_cache_.find(method_name);
    if (it != address_cache_.end()) {
        XRPC_LOG_DEBUG("Cache hit for method {}: {}", method_name, it->second);
        return it->second;
    }

    std::string address = zk_client_->Discover(path);
    address_cache_[method_name] = address;
    XRPC_LOG_INFO("Discovered address for {}: {}", path, address);
    return address;
}

void XrpcChannel::UpdateServiceAddress(const std::string& method_name, const std::string& new_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (new_address.empty()) {
        address_cache_.erase(method_name);
        XRPC_LOG_WARN("Service address for {} removed", method_name);
    } else {
        address_cache_[method_name] = new_address;
        XRPC_LOG_INFO("Updated service address for {}: {}", method_name, new_address);
    }
}

} // namespace xrpc
