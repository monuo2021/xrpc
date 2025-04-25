#include "transport/muduo_transport.h"
#include <muduo/base/Logging.h>
#include <stdexcept>
#include <sstream>

namespace xrpc {

MuduoTransport::MuduoTransport() : loop_(new muduo::net::EventLoop()) {
    XRPC_LOG_INFO("MuduoTransport initialized");
}

MuduoTransport::~MuduoTransport() {
    if (server_) server_->stop();
    if (client_) client_->disconnect();
    loop_->quit();
    XRPC_LOG_INFO("MuduoTransport destroyed");
}

void MuduoTransport::StartServer(const std::string& address) {
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid address format: " + address);
    }
    std::string ip = address.substr(0, colon);
    uint16_t port = std::stoi(address.substr(colon + 1));

    muduo::net::InetAddress addr(ip, port);
    server_ = std::make_unique<muduo::net::TcpServer>(loop_.get(), addr, "XrpcServer");
    server_->setConnectionCallback(std::bind(&MuduoTransport::OnConnection, this, std::placeholders::_1));
    server_->setMessageCallback(std::bind(&MuduoTransport::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    server_->start();
    XRPC_LOG_INFO("Server started at {}", address);
}

void MuduoTransport::SetRequestCallback(std::function<void(const std::string&, const std::string&)> callback) {
    request_callback_ = callback;
}

std::string MuduoTransport::SendRequest(const std::string& address, const std::string& data) {
    // 简化实现：同步发送，实际需异步和连接池（第 9-10 天）
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid address format: " + address);
    }
    std::string ip = address.substr(0, colon);
    uint16_t port = std::stoi(address.substr(colon + 1));

    muduo::net::InetAddress addr(ip, port);
    client_ = std::make_unique<muduo::net::TcpClient>(loop_.get(), addr, "XrpcClient");
    client_->connect();

    // 模拟同步发送（待优化）
    XRPC_LOG_DEBUG("Sending request to {}: {} bytes", address, data.size());
    // 实际需实现连接池和异步回调
    throw std::runtime_error("SendRequest not fully implemented");
}

void MuduoTransport::SendResponse(const std::string& address, const std::string& data) {
    // 待实现：查找连接，发送响应
    XRPC_LOG_DEBUG("Sending response to {}: {} bytes", address, data.size());
}

void MuduoTransport::OnConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        XRPC_LOG_INFO("Connection established: {}", conn->peerAddress().toIpPort());
    } else {
        XRPC_LOG_INFO("Connection closed: {}", conn->peerAddress().toIpPort());
    }
}

void MuduoTransport::OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time) {
    std::string data = buf->retrieveAllAsString();
    std::string address = conn->peerAddress().toIpPort();
    XRPC_LOG_DEBUG("Received request from {}: {} bytes", address, data.size());
    if (request_callback_) {
        request_callback_(address, data);
    }
}

} // namespace xrpc
