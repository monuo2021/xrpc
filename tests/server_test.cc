#include "core/server/xrpc_server.h"
#include "user_service.pb.h"
#include <gtest/gtest.h>

namespace xrpc {

class MockUserService : public example::UserService {
public:
    void Login(google::protobuf::RpcController* controller,
               const example::LoginRequest* request,
               example::LoginResponse* response,
               google::protobuf::Closure* done) override {
        response->set_success(true);
        response->set_token("mock_token");
        done->Run();
    }
};

TEST(XrpcServerTest, StartAndRegister) {
    XrpcServer server;
    MockUserService service;
    server.AddService(&service);
    ASSERT_NO_THROW(server.Start());
}

} // namespace xrpc
