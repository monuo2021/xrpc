#ifndef XRPC_TRANSPORT_MUDUO_TRANSPORT_H
#define XRPC_TRANSPORT_MUDUO_TRANSPORT_H

#include "core/common/xrpc_logger.h"
#include <muduo/net/TcpClient.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Buffer.h>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace xrpc {

class MuduoTransport {
public:
    MuduoTransport();
    ~MuduoTransport();

    // 启动服务器
    void StartServer(const std::string& address);

    // 设置请求回调
    void SetRequestCallback(std::function<void(const std::string&, const std::string&)> callback);

    // 发送请求（客户端，同步）
    std::string SendRequest(const std::string& address, const std::string& data);

    // 发送响应（服务器）
    void SendResponse(const std::string& address, const std::string& data);

private:
    void OnConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time);
    void OnClientConnection(const muduo::net::TcpConnectionPtr& conn);
    void OnClientMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buf, muduo::Timestamp time);

    std::unique_ptr<muduo::net::EventLoop> loop_;
    std::unique_ptr<muduo::net::TcpServer> server_;
    std::unique_ptr<muduo::net::TcpClient> client_;
    std::function<void(const std::string&, const std::string&)> request_callback_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::string response_data_;
    bool response_received_;
};

} // namespace xrpc

#endif // XRPC_TRANSPORT_MUDUO_TRANSPORT_H
