#include "transport/muduo_transport.h"
#include <muduo/base/Logging.h>
#include <stdexcept>
#include <sstream>
#include <thread>

namespace xrpc {

MuduoTransport::MuduoTransport() 
    : loop_(new muduo::net::EventLoop()), response_received_(false) {
    XRPC_LOG_INFO("MuduoTransport initialized");
}

MuduoTransport::~MuduoTransport() {
    if (client_) {
        client_->disconnect();
        client_.reset();
    }
    if (server_) {
        server_.reset(); // TcpServer 自动停止，依赖 EventLoop
    }
    loop_->quit();
    loop_.reset();
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

    // 启动事件循环（在新线程中）
    std::thread([this]() { loop_->loop(); }).detach();
}

void MuduoTransport::SetRequestCallback(std::function<void(const std::string&, const std::string&)> callback) {
    request_callback_ = callback;
}

std::string MuduoTransport::SendRequest(const std::string& address, const std::string& data) {
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid address format: " + address);
    }
    std::string ip = address.substr(0, colon);
    uint16_t port = std::stoi(address.substr(colon + 1));

    muduo::net::InetAddress addr(ip, port);
    client_ = std::make_unique<muduo::net::TcpClient>(loop_.get(), addr, "XrpcClient");
    client_->setConnectionCallback(std::bind(&MuduoTransport::OnClientConnection, this, std::placeholders::_1));
    client_->setMessageCallback(std::bind(&MuduoTransport::OnClientMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    client_->connect();

    // 发送请求
    {
        std::lock_guard<std::mutex> lock(mutex_);
        response_received_ = false;
        response_data_.clear();
    }

    // 等待连接
    client_->connection()->send(data);
    XRPC_LOG_DEBUG("Sent request to {}: {} bytes", address, data.size());

    // 同步等待响应
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_for(lock, std::chrono::seconds(5), [this] { return response_received_; });
    if (!response_received_) {
        throw std::runtime_error("Request timeout to " + address);
    }

    return response_data_;
}

void MuduoTransport::SendResponse(const std::string& address, const std::string& data) {
    // 需跟踪连接，当前为占位实现
    XRPC_LOG_DEBUG("Sending response to {}: {} bytes", address, data.size());
    // 实际需查找连接并发送
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

void MuduoTransport::OnClientConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (conn->connected()) {
        XRPC_LOG_INFO("Client connected to {}", conn->peerAddress().toIpPort());
    } else {
        XRPC_LOG_INFO("Client disconnected from {}", conn->peerAddress().toIpPort());
    }
}

void MuduoTransport::OnClientMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time) {
    std::string data = buf->retrieveAllAsString();
    XRPC_LOG_DEBUG("Received response: {} bytes", data.size());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        response_data_ = data;
        response_received_ = true;
    }
    cond_.notify_one();
}

} // namespace xrpc
