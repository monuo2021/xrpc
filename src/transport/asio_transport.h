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
    // 客户端：处理连接和消息
    void DoClientConnect(const std::string& ip, int port, boost::system::error_code& ec);
    void DoClientRead();

    // 服务端：接受连接和处理消息
    void DoAccept();
    void DoServerRead(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::tcp::socket> client_socket_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::function<void(const std::string&, std::string&)> server_callback_;
    std::string response_;
    bool response_received_;
    boost::asio::streambuf read_buffer_;
};

} // namespace xrpc

#endif // ASIO_TRANSPORT_H