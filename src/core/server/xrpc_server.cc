#include "core/server/xrpc_server.h"
#include "core/common/xrpc_logger.h"
#include "core/controller/xrpc_controller.h"
#include "xrpc.pb.h"
#include <stdexcept>

namespace xrpc {

XrpcServer::XrpcServer(const std::string& config_file) : zk_client_(new ZookeeperClient), transport_(new AsioTransport) {
    config_.Load(config_file);
    Init();
}

XrpcServer::~XrpcServer() {
    // transport_ 和 zk_client_ 由 unique_ptr 自动释放
}

void XrpcServer::Init() {
    // 初始化 ZooKeeper
    zk_client_->Start(); // 从 XrpcConfig 获取配置

    // 获取服务器地址
    server_ip_ = config_.Get("server_ip", "0.0.0.0");
    server_port_ = std::stoi(config_.Get("server_port", "8080"));

    // 初始化 Asio 服务器
    transport_->StartServer(server_ip_, server_port_, [this](const std::string& data, std::string& response) {
        OnMessage(data, response);
    });
}

void XrpcServer::RegisterService(google::protobuf::Service* service) {
    std::string service_name = service->GetDescriptor()->name();
    services_[service_name] = service;

    // 注册到 ZooKeeper
    std::string path = "/" + service_name + "/" + server_ip_ + ":" + std::to_string(server_port_);
    std::string data = "methods=";
    for (int i = 0; i < service->GetDescriptor()->method_count(); ++i) {
        if (i > 0) data += ",";
        data += service->GetDescriptor()->method(i)->name();
    }
    zk_client_->Register(path, data, true);
    XRPC_LOG_INFO("Registered service {} at {}", service_name, path);
}

void XrpcServer::Start() {
    XRPC_LOG_INFO("XrpcServer started at {}:{}", server_ip_, server_port_);
    transport_->Run();
}

void XrpcServer::OnMessage(const std::string& data, std::string& response) {
    try {
        // 解码请求
        RpcHeader header;
        std::string args;
        if (!codec_.Decode(data, header, args)) {
            XRPC_LOG_ERROR("Failed to decode request");
            return;
        }

        // 查找服务
        auto it = services_.find(header.service_name());
        if (it == services_.end()) {
            XRPC_LOG_ERROR("Service {} not found", header.service_name());
            return;
        }

        // 查找方法
        google::protobuf::Service* service = it->second;
        const google::protobuf::MethodDescriptor* method_desc = nullptr;
        for (int i = 0; i < service->GetDescriptor()->method_count(); ++i) {
            if (service->GetDescriptor()->method(i)->name() == header.method_name()) {
                method_desc = service->GetDescriptor()->method(i);
                break;
            }
        }

        if (!method_desc) {
            XRPC_LOG_ERROR("Method {}.{} not found", header.service_name(), header.method_name());
            return;
        }

        // 创建请求和响应
        std::unique_ptr<google::protobuf::Message> request(service->GetRequestPrototype(method_desc).New());
        std::unique_ptr<google::protobuf::Message> response_msg(service->GetResponsePrototype(method_desc).New());

        if (!request->ParseFromString(args)) {
            XRPC_LOG_ERROR("Failed to parse request for {}.{}", header.service_name(), header.method_name());
            return;
        }

        // 调用服务方法
        ServiceDescriptor desc{header.service_name(), header.method_name(), method_desc};
        CallServiceMethod(desc, request.get(), response_msg.get());

        // 编码响应
        response = codec_.EncodeResponse(*response_msg);
        XRPC_LOG_INFO("Processed request for {}.{}", header.service_name(), header.method_name());
    } catch (const std::exception& e) {
        XRPC_LOG_ERROR("OnMessage failed: {}", e.what());
    }
}

void XrpcServer::CallServiceMethod(const ServiceDescriptor& desc,
                                  google::protobuf::Message* request,
                                  google::protobuf::Message* response) {
    auto it = services_.find(desc.service_name);
    if (it == services_.end()) {
        throw std::runtime_error("Service not found");
    }

    google::protobuf::Service* service = it->second;
    XrpcController controller;
    service->CallMethod(desc.method_descriptor, &controller, request, response, nullptr);

    if (controller.Failed()) {
        XRPC_LOG_ERROR("Service call failed: {}", controller.ErrorText());
        throw std::runtime_error(controller.ErrorText());
    }
}

} // namespace xrpc