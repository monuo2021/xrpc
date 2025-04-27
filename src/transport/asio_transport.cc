#include "transport/asio_transport.h"
#include <stdexcept>
#include <sstream>

namespace xrpc {

AsioTransport::AsioTransport() 
    : io_context_(new boost::asio::io_context()),
      response_received_(false),
      work_guard_(new boost::asio::io_context::work(*io_context_)) {
    XRPC_LOG_INFO("AsioTransport initialized");
    io_thread_ = std::thread([this]() { io_context_->run(); });
}

AsioTransport::~AsioTransport() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (acceptor_) {
            boost::system::error_code ec;
            acceptor_->close(ec);
        }
        if (client_socket_) {
            boost::system::error_code ec;
            client_socket_->close(ec);
        }
        for (auto& conn : connections_) {
            boost::system::error_code ec;
            conn.second->close(ec);
        }
        connections_.clear();
    }
    work_guard_.reset();
    io_context_->stop();
    if (io_thread_.joinable()) {
        io_thread_.join();
    }
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
}

void AsioTransport::DoAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
            std::string addr;
            try {
                addr = socket->remote_endpoint().address().to_string() + ":" + 
                       std::to_string(socket->remote_endpoint().port());
            } catch (const std::exception& e) {
                XRPC_LOG_ERROR("Failed to get remote endpoint: {}", e.what());
                return;
            }
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
        } else {
            XRPC_LOG_ERROR("Accept failed: {}", error.message());
        }
        DoAccept();
    });
}

void AsioTransport::HandleRead(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                               const boost::system::error_code& error,
                               size_t bytes_transferred,
                               std::vector<char>& buffer) {
    if (!error) {
        std::string addr;
        try {
            addr = socket->remote_endpoint().address().to_string() + ":" + 
                   std::to_string(socket->remote_endpoint().port());
        } catch (const std::exception& e) {
            XRPC_LOG_ERROR("Failed to get remote endpoint: {}", e.what());
            return;
        }

        std::string data(buffer.data(), bytes_transferred);
        XRPC_LOG_DEBUG("Received request from {}: {} bytes", addr, bytes_transferred);
        if (request_callback_) {
            request_callback_(addr, data);
        }

        std::vector<char> new_buffer(8192);
        socket->async_read_some(
            boost::asio::buffer(new_buffer),
            [this, socket, new_buffer](const boost::system::error_code& error, size_t bytes_transferred) mutable {
                HandleRead(socket, error, bytes_transferred, new_buffer);
            });
    } else {
        std::string addr = "unknown";
        try {
            addr = socket->remote_endpoint().address().to_string() + ":" + 
                   std::to_string(socket->remote_endpoint().port());
        } catch (const std::exception& e) {
            XRPC_LOG_ERROR("Failed to get remote endpoint on close: {}", e.what());
        }
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
                std::string addr;
                try {
                    addr = client_socket_->remote_endpoint().address().to_string() + ":" + 
                           std::to_string(client_socket_->remote_endpoint().port());
                } catch (const std::exception& e) {
                    XRPC_LOG_ERROR("Failed to get remote endpoint: {}", e.what());
                    return;
                }
                XRPC_LOG_DEBUG("Sent request to {}: {} bytes", addr, data.size());
            }
        });

    std::vector<char> buffer(8192);
    client_socket_->async_read_some(
        boost::asio::buffer(buffer),
        [this, buffer](const boost::system::error_code& error, size_t bytes_transferred) mutable {
            HandleClientRead(client_socket_, error, bytes_transferred, buffer);
        });

    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait_for(lock, std::chrono::seconds(5), [this] { return response_received_; });
    if (!response_received_) {
        boost::system::error_code ec;
        client_socket_->close(ec);
        throw std::runtime_error("Request timeout to " + address);
    }

    return response_data_;
}

void AsioTransport::HandleClientConnect(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                                       const boost::system::error_code& error) {
    if (!error) {
        std::string addr;
        try {
            addr = socket->remote_endpoint().address().to_string() + ":" + 
                   std::to_string(socket->remote_endpoint().port());
        } catch (const std::exception& e) {
            XRPC_LOG_ERROR("Failed to get remote endpoint: {}", e.what());
            return;
        }
        XRPC_LOG_INFO("Client connected to {}", addr);
    } else {
        XRPC_LOG_ERROR("Client connection failed: {}", error.message());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response_received_ = true;
            response_data_ = "Connection failed: " + error.message();
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
        XRPC_LOG_DEBUG("Received response: {} bytesGhosts of the Abyss", bytes_transferred);
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
            response_received_ = true;
            response_data_ = "Read failed: " + error.message();
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