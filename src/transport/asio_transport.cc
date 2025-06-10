#include "transport/asio_transport.h"
#include "core/common/xrpc_logger.h"
#include <boost/asio.hpp>
#include <stdexcept>
#include <thread>
#include <memory>

namespace xrpc {

AsioTransport::AsioTransport() : io_context_(new boost::asio::io_context), response_received_(false) {
    // 创建 io_context::work，防止 io_context 提前退出
    work_ = std::make_unique<boost::asio::io_context::work>(*io_context_);
    // 启动单独线程运行 io_context
    io_context_thread_ = std::thread([this]() { io_context_->run(); });
}

AsioTransport::~AsioTransport() {
    Stop();
    if (io_context_thread_.joinable()) {
        io_context_thread_.join();
    }
}

void AsioTransport::Connect(const std::string& ip, int port) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!client_socket_ || !client_socket_->is_open()) {
        client_socket_ = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
        
        boost::system::error_code ec;
        client_socket_->connect(endpoint, ec);
        if (ec) {
            XRPC_LOG_ERROR("Failed to connect to {}:{}: {}", ip, port, ec.message());
            throw std::runtime_error("Connection failed");
        }
        XRPC_LOG_INFO("Connected to {}:{}", ip, port);
    }
}

void AsioTransport::StartServer(const std::string& ip, int port, std::function<void(const std::string&, std::string&)> callback) {
    server_callback_ = callback;
    server_acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(
        *io_context_, 
        boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ip), port)
    );
    DoAccept();
    XRPC_LOG_INFO("Server started at {}:{}", ip, port);
}

bool AsioTransport::Send(const std::string& data, std::string& response) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!client_socket_ || !client_socket_->is_open()) {
        XRPC_LOG_ERROR("Client socket not connected");
        return false;
    }

    response_received_ = false;
    
    boost::system::error_code ec;
    boost::asio::write(*client_socket_, boost::asio::buffer(data), ec);
    if (ec) {
        XRPC_LOG_ERROR("Failed to send data: {}", ec.message());
        return false;
    }
    XRPC_LOG_DEBUG("Sent {} bytes", data.size());

    DoClientRead();
    while (!response_received_ && !ec) {
        io_context_->poll_one();
    }
    if (!response_received_) {
        XRPC_LOG_ERROR("No response received");
        return false;
    }

    response = response_;
    return true;
}

void AsioTransport::SendAsync(const std::string& data, std::function<void(const std::string&, bool)> callback) {
    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (!client_socket_ || !client_socket_->is_open()) {
        XRPC_LOG_ERROR("Client socket not connected");
        callback("", false);
        return;
    }

    // 为每次异步调用分配独立的缓冲区
    auto read_buffer = std::make_shared<std::array<char, 8192>>();
    boost::asio::async_write(
        *client_socket_,
        boost::asio::buffer(data),
        [this, callback, read_buffer, data](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
            if (ec) {
                XRPC_LOG_ERROR("Failed to send async data: {}", ec.message());
                callback("", false);
                return;
            }
            XRPC_LOG_DEBUG("Sent {} bytes async", data.size());
            DoClientAsyncRead(read_buffer, callback);
        }
    );
}

void AsioTransport::Run() {
    // io_context 已由线程运行，无需显式调用
}

void AsioTransport::Stop() {
    boost::system::error_code ec;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (client_socket_ && client_socket_->is_open()) {
            client_socket_->close(ec);
            if (ec) {
                XRPC_LOG_ERROR("Failed to close client socket: {}", ec.message());
            }
        }
    }
    if (server_acceptor_ && server_acceptor_->is_open()) {
        server_acceptor_->cancel(ec);
        if (ec) {
            XRPC_LOG_ERROR("Failed to cancel acceptor: {}", ec.message());
        }
        server_acceptor_->close(ec);
        if (ec) {
            XRPC_LOG_ERROR("Failed to close server acceptor: {}", ec.message());
        }
    }
    for (auto& socket : server_sockets_) {
        if (socket && socket->is_open()) {
            socket->cancel(ec);
            if (ec) {
                XRPC_LOG_ERROR("Failed to cancel server socket: {}", ec.message());
            }
            socket->close(ec);
            if (ec) {
                XRPC_LOG_ERROR("Failed to close server socket: {}", ec.message());
            }
        }
    }
    server_sockets_.clear();
    work_.reset();
    io_context_->stop();
}

void AsioTransport::DoClientRead() {
    client_socket_->async_read_some(
        boost::asio::buffer(read_buffer_, sizeof(read_buffer_)),
        [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            HandleClientRead(ec, bytes_transferred);
        }
    );
}

void AsioTransport::HandleClientRead(const boost::system::error_code& ec, std::size_t bytes_transferred) {
    if (!ec) {
        response_.assign(read_buffer_, bytes_transferred);
        response_received_ = true;
        XRPC_LOG_DEBUG("Received {} bytes", bytes_transferred);
    } else {
        XRPC_LOG_ERROR("Read error: {}", ec.message());
    }
}

void AsioTransport::DoClientAsyncRead(std::shared_ptr<std::array<char, 8192>> read_buffer,
                                     std::function<void(const std::string&, bool)> callback) {
    client_socket_->async_read_some(
        boost::asio::buffer(*read_buffer),
        [this, read_buffer, callback](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            HandleClientAsyncRead(read_buffer, callback, ec, bytes_transferred);
        }
    );
}

void AsioTransport::HandleClientAsyncRead(std::shared_ptr<std::array<char, 8192>> read_buffer,
                                         std::function<void(const std::string&, bool)> callback,
                                         const boost::system::error_code& ec,
                                         std::size_t bytes_transferred) {
    if (!ec) {
        std::string response(read_buffer->data(), bytes_transferred);
        XRPC_LOG_DEBUG("Received {} bytes async", bytes_transferred);
        callback(response, true);
    } else {
        XRPC_LOG_ERROR("Async read error: {}", ec.message());
        callback("", false);
    }
}

void AsioTransport::DoAccept() {
    if (!server_acceptor_ || !server_acceptor_->is_open()) {
        return;
    }
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
    server_acceptor_->async_accept(
        *socket,
        [this, socket](const boost::system::error_code& ec) {
            HandleAccept(socket, ec);
        }
    );
}

void AsioTransport::HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const boost::system::error_code& ec) {
    if (!ec) {
        XRPC_LOG_INFO("Client connected: {}", socket->remote_endpoint().address().to_string());
        server_sockets_.insert(socket);
        DoServerRead(socket);
    } else {
        XRPC_LOG_ERROR("Accept error: {}", ec.message());
    }
    DoAccept();
}

void AsioTransport::DoServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    auto read_buffer = std::make_shared<std::array<char, 8192>>(); // 每个连接独立的缓冲区
    socket->async_read_some(
        boost::asio::buffer(*read_buffer),
        [this, socket, read_buffer](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            HandleServerRead(socket, read_buffer, ec, bytes_transferred);
        }
    );
}

void AsioTransport::HandleServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                                    std::shared_ptr<std::array<char, 8192>> read_buffer,
                                    const boost::system::error_code& ec, 
                                    std::size_t bytes_transferred) {
    if (!ec) {
        std::string request(read_buffer->data(), bytes_transferred);
        std::string response;
        if (server_callback_) {
            server_callback_(request, response);
        }
        if (!response.empty()) {
            boost::asio::write(*socket, boost::asio::buffer(response));
            XRPC_LOG_DEBUG("Sent {} bytes to {}", response.size(), socket->remote_endpoint().address().to_string());
        }
        DoServerRead(socket); // 继续读取下一条消息
    } else {
        XRPC_LOG_INFO("Client disconnected: {}", socket->remote_endpoint().address().to_string());
        server_sockets_.erase(socket);
    }
}

} // namespace xrpc