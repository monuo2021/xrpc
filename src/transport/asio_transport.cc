#include "transport/asio_transport.h"
#include "core/common/xrpc_logger.h"
#include <boost/asio.hpp>
#include <stdexcept>

namespace xrpc {

AsioTransport::AsioTransport() : io_context_(new boost::asio::io_context), response_received_(false) {}

AsioTransport::~AsioTransport() {
    if (client_socket_) client_socket_->close();
    if (server_acceptor_) server_acceptor_->close();
}

void AsioTransport::Connect(const std::string& ip, int port) {
    client_socket_ = std::make_unique<boost::asio::ip::tcp::socket>(*io_context_);
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    
    boost::system::error_code ec;
    client_socket_->connect(endpoint, ec);
    if (ec) {
        XRPC_LOG_ERROR("Failed to connect to {}:{}: {}", ip, port, ec.message());
        throw std::runtime_error("Connection failed");
    }
    XRPC_LOG_INFO("Connected to {}:{}", ip, port);
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
    if (!client_socket_ || !client_socket_->is_open()) {
        XRPC_LOG_ERROR("Client socket not connected");
        return false;
    }

    response_received_ = false;
    
    // 发送数据
    boost::system::error_code ec;
    boost::asio::write(*client_socket_, boost::asio::buffer(data), ec);
    if (ec) {
        XRPC_LOG_ERROR("Failed to send data: {}", ec.message());
        return false;
    }
    XRPC_LOG_DEBUG("Sent {} bytes", data.size());

    // 接收响应
    DoClientRead();
    io_context_->run_one();
    if (!response_received_) {
        XRPC_LOG_ERROR("No response received");
        return false;
    }

    response = response_;
    return true;
}

void AsioTransport::Run() {
    io_context_->run();
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

void AsioTransport::DoAccept() {
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
        DoServerRead(socket);
    } else {
        XRPC_LOG_ERROR("Accept error: {}", ec.message());
    }
    DoAccept();
}

void AsioTransport::DoServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    socket->async_read_some(
        boost::asio::buffer(read_buffer_, sizeof(read_buffer_)),
        [this, socket](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            HandleServerRead(socket, ec, bytes_transferred);
        }
    );
}

void AsioTransport::HandleServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                                    const boost::system::error_code& ec, 
                                    std::size_t bytes_transferred) {
    if (!ec) {
        std::string request(read_buffer_, bytes_transferred);
        std::string response;
        if (server_callback_) {
            server_callback_(request, response);
        }
        if (!response.empty()) {
            boost::asio::write(*socket, boost::asio::buffer(response));
            XRPC_LOG_DEBUG("Sent {} bytes to {}", response.size(), socket->remote_endpoint().address().to_string());
        }
        DoServerRead(socket);
    } else {
        XRPC_LOG_INFO("Client disconnected: {}", socket->remote_endpoint().address().to_string());
    }
}

} // namespace xrpc