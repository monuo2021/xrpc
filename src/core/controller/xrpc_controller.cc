#include "core/controller/xrpc_controller.h"
#include "core/common/xrpc_logger.h"

namespace xrpc {

XrpcController::XrpcController() : failed_(false), canceled_(false) {
    XRPC_LOG_DEBUG("XrpcController initialized");
}

void XrpcController::Reset() {
    failed_ = false;
    canceled_ = false;
    error_text_.clear();
    XRPC_LOG_DEBUG("XrpcController reset");
}

bool XrpcController::Failed() const {
    return failed_;
}

std::string XrpcController::ErrorText() const {
    return error_text_;
}

void XrpcController::SetFailed(const std::string& reason) {
    failed_ = true;
    error_text_ = reason;
    XRPC_LOG_ERROR("XrpcController set failed: {}", reason);
}

void XrpcController::StartCancel() {
    canceled_ = true;
    XRPC_LOG_WARN("XrpcController cancel requested (not implemented)");
    // 待第 9-10 天实现
}

bool XrpcController::IsCanceled() const {
    return canceled_;
}

void XrpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
    XRPC_LOG_WARN("NotifyOnCancel not implemented");
    // 待第 9-10 天实现
    if (callback) callback->Run();
}

} // namespace xrpc
