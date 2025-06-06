#ifndef ASIO_TRANSPORT_H
#define ASIO_TRANSPORT_H

#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include <set>

namespace xrpc {

class AsioTransport {
public:
    AsioTransport();
    ~AsioTransport();

    void Connect(const std::string& ip, int port);
    void StartServer(const std::string& ip, int port, std::function<void(const std::string&, std::string&)> callback);
    bool Send(const std::string& data, std::string& response);
    void SendAsync(const std::string& data, std::function<void(const std::string&, bool)> callback);
    void Run();
    void Stop();

private:
    void DoClientRead();
    void HandleClientRead(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void DoClientAsyncRead(std::function<void(const std::string&, bool)> callback);
    void HandleClientAsyncRead(std::function<void(const std::string&, bool)> callback,
                              const boost::system::error_code& ec,
                              std::size_t bytes_transferred);
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
    std::set<std::shared_ptr<boost::asio::ip::tcp::socket>> server_sockets_;
};

} // namespace xrpc

#endif // ASIO_TRANSPORT_H