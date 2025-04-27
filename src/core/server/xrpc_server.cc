#include "core/server/xrpc_server.h"
#include "core/codec/xrpc_codec.h"
#include <google/protobuf/descriptor.h>
#include <stdexcept>

namespace xrpc {

XrpcServer::XrpcServer() {
    config_.Load("/home/tan/program/CppWorkSpace/xrpc/configs/xrpc.conf");
    transport_ = std::make_unique<AsioTransport>();
    zk_client_.Start();
    XRPC_LOG_INFO("XrpcServer initialized");
}

XrpcServer::~XrpcServer() = default;

void XrpcServer::AddService(google::protobuf::Service* service) {
    std::string service_name = service->GetDescriptor()->name();
    services_[service_name] = std::unique_ptr<google::protobuf::Service>(service);
    XRPC_LOG_INFO("Added service: {}", service_name);
}

void XrpcServer::Start() {
    std::string ip = config_.Get("server_ip", "0.0.0.0");
    std::string port = config_.Get("server_port", "8080");
    std::string address = ip + ":" + port;

    RegisterServices();

    transport_->SetRequestCallback([this](const std::string& addr, const std::string& data) {
        OnRequest(addr, data);
    });

    try {
        transport_->StartServer(address);
        XRPC_LOG_INFO("XrpcServer started at {}", address);
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("Failed to start server: {}", e.what());
        throw;
    }
}

void XrpcServer::RegisterServices() {
    std::string ip = config_.Get("server_ip", "0.0.0.0");
    std::string port = config_.Get("server_port", "8080");
    std::string address = ip + ":" + port;

    for (const auto& pair : services_) {
        const auto& service = pair.second;
        std::string service_name = service->GetDescriptor()->name();
        const google::protobuf::ServiceDescriptor* desc = service->GetDescriptor();
        for (int i = 0; i < desc->method_count(); ++i) {
            std::string method_name = desc->method(i)->name();
            std::string path = "/" + service_name + "/" + method_name;
            try {
                zk_client_.Register(path, address, true);
                XRPC_LOG_INFO("Registered service path: {}", path);
            } catch (const std::exception& e) {
                XRPC_LOG_ERROR("Failed to register {}: {}", path, e.what());
                throw;
            }
        }
    }
}

void XrpcServer::OnRequest(const std::string& address, const std::string& data) {
    XrpcCodec codec;
    RpcHeader header;
    std::string args;
    if (!codec.Decode(data, header, args)) {
        XRPC_LOG_ERROR("Failed to decode request from {}", address);
        return;
    }

    std::string service_name = header.service_name();
    if (service_name.empty()) {
        XRPC_LOG_ERROR("Service name empty in request from {}", address);
        return;
    }

    auto it = services_.find(service_name);
    if (it == services_.end()) {
        XRPC_LOG_ERROR("Service {} not found from {}", service_name, address);
        return;
    }

    google::protobuf::Service* service = it->second.get();
    std::string method_name = header.method_name();
    if (method_name.empty()) {
        XRPC_LOG_ERROR("Method name empty for service {} from {}", service_name, address);
        return;
    }

    const google::protobuf::MethodDescriptor* method = service->GetDescriptor()->FindMethodByName(method_name);
    if (!method) {
        XRPC_LOG_ERROR("Method {} not found in service {} from {}", method_name, service_name, address);
        return;
    }

    std::unique_ptr<google::protobuf::Message> request(service->GetRequestPrototype(method).New());
    std::unique_ptr<google::protobuf::Message> response(service->GetResponsePrototype(method).New());
    if (!request->ParseFromString(args)) {
        XRPC_LOG_ERROR("Failed to parse request for {}.{} from {}", service_name, method_name, address);
        return;
    }

    google::protobuf::Closure* done = google::protobuf::NewPermanentCallback([]() {});
    service->CallMethod(method, nullptr, request.get(), response.get(), done);

    std::string response_data;
    try {
        response_data = codec.EncodeResponse(*response);
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("Failed to encode response for {}.{}: {}", service_name, method_name, e.what());
        return;
    }

    try {
        transport_->SendResponse(address, response_data);
        XRPC_LOG_DEBUG("Sent response for {}.{} to {}", service_name, method_name, address);
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("Failed to send response for {}.{}: {}", service_name, method_name, e.what());
    }
}

} // namespace xrpc