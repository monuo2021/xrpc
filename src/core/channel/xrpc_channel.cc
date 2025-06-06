#include "core/channel/xrpc_channel.h"
#include "core/common/xrpc_logger.h"
#include "core/controller/xrpc_controller.h"
#include "xrpc.pb.h"
#include <stdexcept>
#include <sstream>

namespace xrpc {

XrpcChannel::XrpcChannel(const std::string& config_file) : zk_client_(new ZookeeperClient), transport_(new AsioTransport) {
    config_.Load(config_file);
    Init();
}

XrpcChannel::~XrpcChannel() {
    std::lock_guard<std::mutex> lock(mutex_);
    transport_->Stop();
    zk_client_->Stop();
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

    return address;
}

bool XrpcChannel::SendRequest(const std::string& data, std::string& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool success = transport_->Send(data, response);
    if (!success) {
        XRPC_LOG_ERROR("Failed to send request");
        return false;
    }
    return true;
}

void XrpcChannel::SendRequestAsync(const std::string& data,
                                  google::protobuf::RpcController* controller,
                                  google::protobuf::Message* response,
                                  google::protobuf::Closure* done) {
    std::lock_guard<std::mutex> lock(mutex_);
    XrpcController* xrpc_controller = dynamic_cast<XrpcController*>(controller);
    if (!xrpc_controller) {
        XRPC_LOG_ERROR("Invalid controller type");
        if (done) done->Run();
        return;
    }

    transport_->SendAsync(data, [this, xrpc_controller, response, done](const std::string& response_data, bool success) {
        if (!success) {
            xrpc_controller->SetFailed("Failed to send async request");
            XRPC_LOG_ERROR("Failed to send async request");
            if (done) done->Run();
            return;
        }

        if (xrpc_controller->IsCanceled()) {
            xrpc_controller->SetFailed("Request was canceled");
            XRPC_LOG_INFO("Async request canceled");
            if (done) done->Run();
            return;
        }

        RpcHeader response_header;
        if (!codec_.DecodeResponse(response_data, response_header, *response)) {
            xrpc_controller->SetFailed("Failed to decode async response");
            XRPC_LOG_ERROR("Failed to decode async response");
            if (done) done->Run();
            return;
        }

        if (response_header.status() != 0 && response_header.has_error()) {
            xrpc_controller->SetFailed(response_header.error().message());
            XRPC_LOG_ERROR("Async request failed: {}", response_header.error().message());
            if (done) done->Run();
            return;
        }

        XRPC_LOG_INFO("Async request completed successfully");
        if (done) done->Run();
    });
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
        {
            std::lock_guard<std::mutex> lock(mutex_);
            transport_->Connect(server_ip, server_port);
        }

        // 构造 RpcHeader
        RpcHeader header;
        header.set_service_name(service_name);
        header.set_method_name(method_name);
        header.set_request_id(rand()); // 简单生成请求 ID
        header.set_compressed(false); // 默认不压缩
        header.set_cancelled(false);

        // 检查是否已取消
        XrpcController* xrpc_controller = dynamic_cast<XrpcController*>(controller);
        if (xrpc_controller && xrpc_controller->IsCanceled()) {
            xrpc_controller->SetFailed("Request was canceled before sending");
            XRPC_LOG_INFO("Request canceled before sending");
            if (done) done->Run();
            return;
        }

        // 序列化请求
        std::string data = codec_.Encode(header, *request);

        // 异步调用
        if (done) {
            SendRequestAsync(data, controller, response, done);
            return;
        }

        // 同步调用
        std::string response_data;
        if (!SendRequest(data, response_data)) {
            controller->SetFailed("Failed to send request");
            if (done) done->Run();
            return;
        }

        RpcHeader response_header;
        if (!codec_.DecodeResponse(response_data, response_header, *response)) {
            controller->SetFailed("Failed to decode response");
            if (done) done->Run();
            return;
        }

        if (response_header.status() != 0 && response_header.has_error()) {
            controller->SetFailed(response_header.error().message());
            XRPC_LOG_ERROR("Request failed: {}", response_header.error().message());
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