#include "core/server/xrpc_server.h"
#include "user_service.pb.h"
#include <iostream>

namespace xrpc {

class UserServiceImpl : public example::UserService {
public:
    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        std::cout << "Received Login request: username=" << request->username() << std::endl;
        response->set_success(true);
        response->set_token("server_token_" + request->username());
        done->Run();
    }
};

} // namespace xrpc

int main() {
    xrpc::XrpcServer server;
    xrpc::UserServiceImpl service;
    server.AddService(&service);
    server.Start();
    std::cout << "Server running on 0.0.0.0:8080. Press Ctrl+C to stop." << std::endl;
    std::this_thread::sleep_for(std::chrono::hours(1)); // 保持运行
    return 0;
}