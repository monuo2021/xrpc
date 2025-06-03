#ifndef XRPC_CONTROLLER_H
#define XRPC_CONTROLLER_H

#include "core/common/xrpc_common.h"
#include <google/protobuf/service.h>
#include <string>

namespace xrpc {

class XrpcController : public google::protobuf::RpcController {
public:
    XrpcController();
    ~XrpcController() override = default;

    // 重置状态
    void Reset() override;

    // 是否失败
    bool Failed() const override;

    // 错误信息
    std::string ErrorText() const override;

    // 设置失败
    void SetFailed(const std::string& reason) override;

    // 取消相关（预留）
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

private:
    bool failed_;
    std::string error_text_;
    bool canceled_;
    google::protobuf::Closure* cancel_callback_;
};

} // namespace xrpc

#endif // XRPC_CONTROLLER_H