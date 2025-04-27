#include "transport/asio_transport.h"
#include <stdexcept>
#include <sstream>
#include <thread>

namespace xrpc {

AsioTransport::AsioTransport() 
    : io_context_(new boost::asio::io_context()), response_received_(false) {
    XRPC_LOG_INFO("AsioTransport initialized");
}

AsioTransport::~AsioTransport() {
    if (acceptor_) {
        acceptor_->close();
    }
    if (client_socket_) {
        client_socket_->close();
    }
    io_context_->stop();
    XRPC_LOG_INFO("AsioTransport destroyed");
}

void AsioTransport::StartServer(const std::string& address) {
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid address format: " + address);
    }
    std::string ip = address.substr(0, colon);
    uint16_t port = std::stoi(address.substr(colon + 1));

    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(*io_context_, endpoint);
    DoAccept();
    XRPC_LOG_INFO("Server started at {}", address);

    // 启动事件循环（在新线程中）
    std::thread([this]() { io_context_->run(); }).detach();
}

void AsioTransport::DoAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
            std::string addr = socket->remote_endpoint().address().to_string() + ":" + 
                               std::to_string(socket->remote_endpoint().port());
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connections_[addr] = socket;
            }
            XRPC_LOG_INFO("Connection established: {}", addr);
            std::vector<char> buffer(8192);
            socket->async_read_some(
                boost::asio::buffer(buffer),
                [this, socket, buffer](const boost::system::error_code& error, size_t bytes_transferred) mutable {
                    HandleRead(socket, error, bytes_transferred, buffer);
                });
        }
        DoAccept(); // 继续接受新连接
    });
}

void AsioTransport::HandleRead(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                               const boost::system::error_code& error,
                               size_t bytes_transferred,
                               std::vector<char>& buffer) {
    if (!error) {
        std::string addr = socket->remote_endpoint().address().to_string() + ":" + 
                           std::to_string(socket->remote_endpoint().port());
        std::string data(buffer.data(), bytes_transferred);
        XRPC_LOG_DEBUG("Received request from {}: {} bytes", addr, bytes_transferred);
        if (request_callback_) {
            request_callback_(addr, data);
        }
        // 继续读取
        socket->async_read_some(
            boost::asio::buffer(buffer),
            [this, socket, buffer](const boost::system::error_code& error, size_t bytes_transferred) mutable {
                HandleRead(socket, error, bytes_transferred, buffer);
            });
    } else {
        std::string addr = socket->remote_endpoint().address().to_string() + ":" + 
                           std::to_string(socket->remote_endpoint().port());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.erase(addr);
        }
        XRPC_LOG_INFO("Connection closed: {}", addr);
    }
}

void AsioTransport::SetRequestCallback(std::function<void(const std::string&, const std::string&)> callback) {
    request_callback_ = callback;
}

std::string AsioTransport::SendRequest(const std::string& address, const std::string& data) {
    size_t colon = address.find(':');
    if (colon == std::string::npos) {
        throw std::runtime_error("Invalid address format: " + address);
    }
    std::string ip = address.substr(0, colon);
    uint16_t port = std::stoi(address.substr(colon + 1));

    client_socket_ = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    client_socket_->async_connect(
        endpoint,
        [this, &data](const boost::system::error_code& error) {
            HandleClientConnect(client_socket_, error);
            if (!error) {
                boost::asio::write(*client_socket_, boost::asio::buffer(data));
                XRPC_LOG_DEBUG("Sent request to {}: {} bytes", 
                               client_socket_->remote_endpoint().address().to_string() + ":" + 
                               std::to_string(client_socket_->remote_endpoint().port()), 
                               data.size());
            }
        });

    std::vector<char> buffer(8192);
    client_socket_->async_read_some(
        boost::asio::buffer(buffer),
        [this, buffer](const boost::system::error_code& error, size_t bytes_transferred) mutable {
            HandleClientRead(client_socket_, error, bytes_transferred, buffer);
        });

    // 同步等待响应
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_for(lock, std::chrono::seconds(5), [this] { return response_received_; });
    if (!response_received_) {
        client_socket_->close();
        throw std::runtime_error("Request timeout to " + address);
    }

    return response_data_;
}

void AsioTransport::HandleClientConnect(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                                       const boost::system::error_code& error) {
    if (!error) {
        XRPC_LOG_INFO("Client connected to {}", 
                      socket->remote_endpoint().address().to_string() + ":" + 
                      std::to_string(socket->remote_endpoint().port()));
    } else {
        XRPC_LOG_ERROR("Client connection failed: {}", error.message());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_received_ = true; // 触发超时
        }
        cond_.notify_one();
    }
}

void AsioTransport::HandleClientRead(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                                    const boost::system::error_code& error,
                                    size_t bytes_transferred,
                                    std::vector<char>& buffer) {
    if (!error) {
        std::string data(buffer.data(), bytes_transferred);
        XRPC_LOG_DEBUG("Received response: {} bytes", bytes_transferred);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_data_ = data;
            response_received_ = true;
        }
        cond_.notify_one();
    } else {
        XRPC_LOG_ERROR("Client read failed: {}", error.message());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_received_ = true; // 触发超时
        }
        cond_.notify_one();
    }
}

void AsioTransport::SendResponse(const std::string& address, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(address);
    if (it != connections_.end()) {
        boost::asio::write(*it->second, boost::asio::buffer(data));
        XRPC_LOG_DEBUG("Sent response to {}: {} bytes", address, data.size());
    } else {
        XRPC_LOG_ERROR("No connection for {}", address);
    }
}

} // namespace xrpc