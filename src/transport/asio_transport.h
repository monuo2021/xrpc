#ifndef XRPC_TRANSPORT_ASIO_TRANSPORT_H
#define XRPC_TRANSPORT_ASIO_TRANSPORT_H

#include "core/common/xrpc_logger.h"
#include <boost/asio.hpp>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <map>

namespace xrpc {

class AsioTransport {
public:
    AsioTransport();
    ~AsioTransport();

    // 启动服务器
    void StartServer(const std::string& address);

    // 设置请求回调
    void SetRequestCallback(std::function<void(const std::string&, const std::string&)> callback);

    // 发送请求（客户端，同步）
    std::string SendRequest(const std::string& address, const std::string& data);

    // 发送响应（服务器）
    void SendResponse(const std::string& address, const std::string& data);

private:
    void DoAccept();
    void HandleRead(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                    const boost::system::error_code& error,
                    size_t bytes_transferred,
                    std::vector<char>& buffer);
    void HandleClientConnect(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                            const boost::system::error_code& error);
    void HandleClientRead(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket,
                         const boost::system::error_code& error,
                         size_t bytes_transferred,
                         std::vector<char>& buffer);

    std::unique_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::shared_ptr<boost::asio::ip::tcp::socket> client_socket_;
    std::function<void(const std::string&, const std::string&)> request_callback_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::string response_data_;
    bool response_received_;
    std::map<std::string, std::shared_ptr<boost::asio::ip::tcp::socket>> connections_;
};

} // namespace xrpc

#endif // XRPC_TRANSPORT_ASIO_TRANSPORT_H