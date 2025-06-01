#include "transport/asio_transport.h"
#include "core/common/xrpc_logger.h"
#include <boost/asio.hpp>
#include <stdexcept>

namespace xrpc {

AsioTransport::AsioTransport() : io_context_(new boost::asio::io_context), response_received_(false) {}

AsioTransport::~AsioTransport() {
    if (client_socket_) client_socket_->close();
    if (acceptor_) acceptor_->close();
}

void AsioTransport::Connect(const std::string& ip, int port) {
    client_socket_ = std::make_unique<boost::asio::ip::tcp::socket>(*io_context_);
    boost::system::error_code ec;
    DoClientConnect(ip, port, ec);
    if (ec) {
        XRPC_LOG_ERROR("Failed to connect to {}:{}: {}", ip, port, ec.message());
        throw std::runtime_error("Connection failed: " + ec.message());
    }
    XRPC_LOG_INFO("Connected to {}:{}", ip, port);
}

void AsioTransport::DoClientConnect(const std::string& ip, int port, boost::system::error_code& ec) {
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);
    client_socket_->connect(endpoint, ec);
}

void AsioTransport::StartServer(const std::string& ip, int port, std::function<void(const std::string&, std::string&)> callback) {
    server_callback_ = callback;
    acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(*io_context_, 
        boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(ip), port));
    XRPC_LOG_INFO("Server started at {}:{}", ip, port);
    DoAccept();
}

void AsioTransport::DoAccept() {
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(*io_context_);
    acceptor_->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
        if (!ec) {
            XRPC_LOG_INFO("Client connected: {}", socket->remote_endpoint().address().to_string());
            DoServerRead(socket);
        }
        DoAccept(); // 继续接受下一个连接
    });
}

void AsioTransport::DoServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
    auto buffer = std::make_shared<boost::asio::streambuf>();
    boost::asio::async_read_until(*socket, *buffer, '\0', 
        [this, socket, buffer](const boost::system::error_code& ec, std::size_t bytes_transferred) {
            if (!ec) {
                std::string request((std::istreambuf_iterator<char>(buffer.get())), {});
                request.resize(bytes_transferred - 1); // 移除结束符
                XRPC_LOG_DEBUG("Received {} bytes", request.size());
                std::string response;
                if (server_callback_) {
                    server_callback_(request, response);
                }
                if (!response.empty()) {
                    response += '\0'; // 添加结束符
                    boost::asio::write(*socket, boost::asio::buffer(response));
                    XRPC_LOG_DEBUG("Sent {} bytes", response.size());
                }
                DoServerRead(socket); // 继续读取
            } else {
                XRPC_LOG_INFO("Client disconnected: {}", ec.message());
            }
        });
}

bool AsioTransport::Send(const std::string& data, std::string& response) {
    if (!client_socket_ || !client_socket_->is_open()) {
        XRPC_LOG_ERROR("Client not connected");
        return false;
    }

    response_received_ = false;
    std::string data_with_end = data + '\0'; // 添加结束符
    boost::asio::write(*client_socket_, boost::asio::buffer(data_with_end));
    XRPC_LOG_DEBUG("Sent {} bytes", data_with_end.size());

    // 同步读取响应
    boost::system::error_code ec;
    size_t bytes_transferred = boost::asio::read_until(*client_socket_, read_buffer_, '\0', ec);
    if (ec) {
        XRPC_LOG_ERROR("Read failed: {}", ec.message());
        return false;
    }

    response.assign((std::istreambuf_iterator<char>(&read_buffer_)), {});
    response.resize(bytes_transferred - 1); // 移除结束符
    response_received_ = true;
    XRPC_LOG_DEBUG("Received {} bytes", response.size());
    return true;
}

void AsioTransport::Run() {
    io_context_->run();
}

} // namespace xrpc