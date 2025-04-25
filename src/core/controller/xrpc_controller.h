#ifndef XRPC_CONTROLLER_H
#define XRPC_CONTROLLER_H

#include <google/protobuf/service.h>
#include <string>
#include <atomic>

namespace xrpc {

class XrpcController : public google::protobuf::RpcController {
public:
    XrpcController();
    ~XrpcController() override = default;

    // 重置状态
    void Reset() override;

    // 检查是否失败
    bool Failed() const override;

    // 获取错误信息
    std::string ErrorText() const override;

    // 设置失败状态
    void SetFailed(const std::string& reason) override;

    // 取消相关（预留）
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

private:
    std::string error_text_;
    std::atomic<bool> failed_;
    std::atomic<bool> canceled_;
};

} // namespace xrpc

#endif // XRPC_CONTROLLER_H
