#ifndef ASIO_TRANSPORT_H
#define ASIO_TRANSPORT_H

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>

namespace xrpc {

class AsioTransport {
public:
    AsioTransport();
    ~AsioTransport();

    // 客户端：连接到服务器
    void Connect(const std::string& ip, int port);

    // 服务端：启动服务器
    void StartServer(const std::string& ip, int port, std::function<void(const std::string&, std::string&)> callback);

    // 发送请求并接收响应（客户端）
    bool Send(const std::string& data, std::string& response);

    // 运行事件循环
    void Run();

private:
    // 客户端处理
    void DoClientRead();
    void HandleClientRead(const boost::system::error_code& ec, std::size_t bytes_transferred);

    // 服务端处理
    void DoAccept();
    void HandleAccept(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const boost::system::error_code& ec);
    void DoServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void HandleServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket, 
                         const boost::system::error_code& ec, 
                         std::size_t bytes_transferred);

    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::tcp::socket> client_socket_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> server_acceptor_;
    std::function<void(const std::string&, std::string&)> server_callback_;
    std::string response_;
    bool response_received_;
    char read_buffer_[8192];
};

} // namespace xrpc

#endif // ASIO_TRANSPORT_H