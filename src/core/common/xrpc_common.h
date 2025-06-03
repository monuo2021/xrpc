#ifndef XRPC_COMMON_H
#define XRPC_COMMON_H

#include <google/protobuf/service.h>
#include <string>

namespace xrpc {

// 服务描述符：存储服务和方法信息
struct ServiceDescriptor {
    std::string service_name;
    std::string method_name;
    const google::protobuf::MethodDescriptor* method_descriptor;
};

// 错误码
enum class ErrorCode {
    OK = 0,
    TIMEOUT = 1,
    NOT_FOUND = 2,
    SERVER_ERROR = 3,
    CLIENT_ERROR = 4,
    CANCELLED = 5
};

} // namespace xrpc

#endif // XRPC_COMMON_H